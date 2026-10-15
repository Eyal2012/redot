#include <QCoreApplication>
#include <QMediaPlayer>
#include <QMediaMetaData>
#include <QAudioOutput>
#include <QAudioBufferOutput>
#include <QAudioBuffer>
#include <QAudioSink>
#include <QMediaDevices>
#include <QVideoSink>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTimer>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QLibrary>
#include <cstdio>
#include <cmath>
#include <atomic>

// Runs the shipping Qt decoder/output configuration without a window or audible
// output. Captures decoded PCM before mute/volume; it is not a listening test.
static std::atomic_uint underflows{}, starts{}, stops{}, resamplerErrors{};
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 4 && args.size() != 5) return 2; // optional --exercise or --repeat3
    if(args.size()==5 && args[4]!="--exercise" && args[4]!="--repeat3")return 2;
    const QString root = QDir::cleanPath(QDir(args[3]).absolutePath());
    if (!root.startsWith(QDir::currentPath() + "/build/") || QFile::exists(root)) return 2;
    QDir().mkpath(root);
    // Qt can log from multiple worker threads; FILE serializes individual writes.
    FILE* log{}; _wfopen_s(&log, (root + "/qt.log").toStdWString().c_str(), L"ab");
    static FILE* logFile; logFile=log;
    qInstallMessageHandler([](QtMsgType,const QMessageLogContext&,const QString& text) {
        if(text.contains("Audio client started"))++starts;
        if(text.contains("Audio client stopped"))++stops;
        if(text.contains("Audio sink state: IdleState") && !text.contains("First frame: true"))++underflows;
        if(text.contains("Resampling failed") || text.contains("Failed to get buffer") || text.contains("PandaClip wave output failed"))++resamplerErrors;
        if(logFile){const auto utf8=text.toUtf8();fwrite(utf8.constData(),1,utf8.size(),logFile);fputc('\n',logFile);}
    });
    QLoggingCategory::setFilterRules("qt.multimedia.audiooutput.debug=true\nqt.multimedia.audioresampler.debug=true\nqt.multimedia.ffmpeg.audiorenderer.debug=true");
    if(args[2]=="--sink") {
        const auto device=QMediaDevices::defaultAudioOutput();
        QAudioSink sink(device,device.preferredFormat());sink.setVolume(0);
        sink.setBufferSize(device.preferredFormat().bytesForDuration(110000));
        unsigned trials{}, early{}; qsizetype retainedMax{};
        bool written=false;
        std::function<void()> trial;
        trial=[&]{
            sink.reset();written=false;
            auto io=sink.start();
            if(!io){app.exit(3);return;}
            QByteArray zero(device.preferredFormat().bytesForFrames(1024),0);
            written=io->write(zero)==zero.size();
        };
        QObject::connect(&sink,&QAudioSink::stateChanged,&app,[&](QAudio::State state){
            if(state!=QAudio::IdleState || !written)return;
            const auto retained=sink.bufferSize()-sink.bytesFree();
            if(retained>0){++early;retainedMax=std::max(retainedMax,retained);}
            written=false;
            if(++trials<40)QTimer::singleShot(5,&app,trial);else app.quit();
        });
        QTimer::singleShot(0,&app,trial);QTimer::singleShot(10000,&app,[&]{app.exit(6);});
        const auto result=app.exec();sink.reset();
        const auto text=QString("sink_trials=%1 premature_idle=%2 max_retained_bytes=%3 format_rate=%4 bytes_per_frame=%5 device=%6\n")
            .arg(trials).arg(early).arg(retainedMax).arg(device.preferredFormat().sampleRate()).arg(device.preferredFormat().bytesPerFrame()).arg(device.description());
        QFile report(root+"/report.txt");report.open(QIODevice::WriteOnly);report.write(text.toUtf8());printf("%s",text.toUtf8().constData());
        qInstallMessageHandler(nullptr);if(log)fclose(log);
        return result?result:trials==40?0:1;
    }
    const bool trace = qEnvironmentVariableIsSet("PANDACLIP_PLAYBACK_TRACE");
    if (trace && !qEnvironmentVariableIsSet("PANDACLIP_PLAYBACK_SILENT_SESSION")) return 7;
#ifdef QT_DEBUG
    const QString runtime = "Qt6Multimediad";
#else
    const QString runtime = "Qt6Multimedia";
#endif
    auto flushTrace = reinterpret_cast<void(*)()>(QLibrary::resolve(runtime, "pandaclipFlushPlaybackTrace"));
    if (trace && !flushTrace) return 8;
    QMediaPlayer player; QAudioOutput output; QVideoSink video;
    // A traced run silences its OWN WASAPI session, after the render buffer.
    // Keep normal gain in the submitted samples so its conversion is tested.
    output.setMuted(!trace); output.setVolume(0.7f); player.setAudioOutput(&output); player.setVideoSink(&video);
    QAudioFormat format; format.setSampleRate(48000); format.setChannelCount(2); format.setSampleFormat(QAudioFormat::Float);
    QAudioBufferOutput buffers(format); player.setAudioBufferOutput(&buffers);
    QFile pcm(root+"/decoded.f32"), metadata(root+"/packets.csv");
    pcm.open(QIODevice::WriteOnly); metadata.open(QIODevice::WriteOnly);
    metadata.write("pts_us,frames,arrival_us,track,segment\n"); QElapsedTimer clock; clock.start();
    unsigned segment{};
    QFile commands(root+"/commands.csv"); commands.open(QIODevice::WriteOnly);
    commands.write("arrival_us,action,position_ms,track\n");
    auto command=[&](const char* action){++segment;commands.write(QString("%1,%2,%3,%4\n").arg(clock.nsecsElapsed()/1000).arg(action).arg(player.position()).arg(player.activeAudioTrack()).toUtf8());};
    quint64 frames{}, packets{}, gaps{}; qint64 expected=-1; double square{}; float peak{};
    QObject::connect(&buffers,&QAudioBufferOutput::audioBufferReceived,&app,[&](const QAudioBuffer& buffer){
        if(!buffer.isValid())return;
        auto data=buffer.constData<float>();
        for(int i=0;i<buffer.sampleCount();++i){square+=double(data[i])*data[i];peak=std::max(peak,std::abs(data[i]));}
        pcm.write(buffer.constData<char>(),buffer.byteCount());
        metadata.write(QString("%1,%2,%3,%4,%5\n").arg(buffer.startTime()).arg(buffer.frameCount()).arg(clock.nsecsElapsed()/1000).arg(player.activeAudioTrack()).arg(segment).toUtf8());
        if(expected>=0 && std::abs(buffer.startTime()-expected)>2)++gaps;
        expected=buffer.startTime()+buffer.duration();frames+=buffer.frameCount();++packets;
    });
    bool selected=false, success=false;unsigned completed{};
    QObject::connect(&player,&QMediaPlayer::tracksChanged,&app,[&]{
        if(selected)return; int track=args[2].toInt();
        if(track<0||track>=player.audioTracks().size()){app.exit(3);return;}
        selected=true;player.setActiveAudioTrack(track);player.play();
        if (args.size()==5 && args[4]=="--exercise") {
            QTimer::singleShot(1700,&app,[&]{command("pause");player.pause();});
            QTimer::singleShot(2000,&app,[&]{command("resume");player.play();});
            QTimer::singleShot(3400,&app,[&]{command("seek-forward");player.setPosition(std::min<qint64>(12000,player.duration()/2));});
            QTimer::singleShot(5000,&app,[&]{command("seek-backward");player.setPosition(1200);});
            QTimer::singleShot(7000,&app,[&]{command("other-track");player.setActiveAudioTrack(player.activeAudioTrack()==0?player.audioTracks().size()-1:0);});
            QTimer::singleShot(9000,&app,[&]{command("original-track");player.setActiveAudioTrack(args[2].toInt());});
            QTimer::singleShot(11500,&app,[&]{command("restart");player.setPosition(0);});
        }
    });
    QObject::connect(&player,&QMediaPlayer::mediaStatusChanged,&app,[&](QMediaPlayer::MediaStatus status){
        if(status==QMediaPlayer::EndOfMedia){
            ++completed;
            if(args.size()==5 && args[4]=="--repeat3" && completed<3){
                QTimer::singleShot(50,&app,[&]{command("next-playback");player.setPosition(0);player.play();});
            }else{success=frames>0;app.quit();}
        }
        if(status==QMediaPlayer::InvalidMedia)app.exit(4);
    });
    QObject::connect(&player,&QMediaPlayer::errorOccurred,&app,[&]{app.exit(5);});
    QTimer failureWatch; failureWatch.setInterval(100);
    QObject::connect(&failureWatch,&QTimer::timeout,&app,[&]{if(resamplerErrors.load())app.exit(9);});failureWatch.start();
    QTimer::singleShot(12*60*1000,&app,[&]{app.exit(6);});
    player.setSource(QUrl::fromLocalFile(QFileInfo(args[1]).absoluteFilePath()));
    const int result=app.exec();player.stop();player.setSource({});
    if (trace) flushTrace();
    pcm.close();metadata.close();
    QFile report(root+"/report.txt"); report.open(QIODevice::WriteOnly);
    const auto text=QString("decoded_frames=%1 packets=%2 pts_gaps=%3 peak=%4 rms=%5 sink_starts=%6 sink_stops=%7 idle_syncs=%8 output_errors=%9 success=%10 completed_playbacks=%11\n")
        .arg(frames).arg(packets).arg(gaps).arg(peak).arg(frames?std::sqrt(square/(frames*2)):0).arg(starts.load()).arg(stops.load()).arg(underflows.load()).arg(resamplerErrors.load()).arg(success).arg(completed);
    report.write(text.toUtf8());printf("%s",text.toUtf8().constData());
    qInstallMessageHandler(nullptr);if(log)fclose(log);
    return result?result:success?0:1;
}
