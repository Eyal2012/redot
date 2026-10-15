#include "audio_resampler.h"
#include "audio_timeline.h"
#include "audio_pcm.h"
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <numeric>
void require(bool pass,const char* text){if(!pass)throw std::runtime_error(text);}
std::vector<float> tone(size_t frames,double rate){std::vector<float> x(frames*2);for(size_t i=0;i<frames;++i){x[i*2]=float(.35*std::sin(i*2*std::numbers::pi*6301/rate)+.1*std::sin(i*2*std::numbers::pi*337/rate));x[i*2+1]=float(.23*std::sin(i*2*std::numbers::pi*2111/rate));}return x;}
std::vector<float> legacy(const std::vector<float>& input,double rate){std::vector<float> out;size_t first=0;double remainder=0;unsigned packet=0;while(first<input.size()/2){size_t count=std::min<size_t>((packet++%2)?441:257,input.size()/2-first);double exact=count*48000/rate+remainder;size_t n=static_cast<size_t>(exact);remainder=exact-n;for(size_t j=0;j<n;++j){double pos=std::min<double>(count-1,j*rate/48000);size_t a=static_cast<size_t>(pos),b=std::min(a+1,count-1);for(size_t ch=0;ch<2;++ch){auto va=input[(first+a)*2+ch],vb=input[(first+b)*2+ch];out.push_back(va+(vb-va)*float(pos-a));}}first+=count;}return out;}
int wmain(int argc,wchar_t** argv){try{
    require(argc==2,"give fresh build evidence directory");
    const auto root=std::filesystem::absolute(argv[1]);require(!std::filesystem::exists(root),"evidence exists");std::filesystem::create_directories(root);
    for(double rate:{32000.,44100.,48000.,96000.,47988.,48024.}){
        auto input=tone(static_cast<size_t>(rate)*2,rate);AudioResampler ref,partitioned;
        auto expected=ref.process(input,rate);auto tail=ref.drain();expected.insert(expected.end(),tail.begin(),tail.end());
        std::vector<float> actual;size_t first=0;unsigned packet=0;
        while(first<input.size()/2){size_t count=std::min<size_t>((packet++%2)?441:257,input.size()/2-first);auto part=partitioned.process(std::span(input).subspan(first*2,count*2),rate);actual.insert(actual.end(),part.begin(),part.end());first+=count;}
        tail=partitioned.drain();actual.insert(actual.end(),tail.begin(),tail.end());std::cout<<"partition_counts rate="<<rate<<" expected="<<expected.size()/2<<" actual="<<actual.size()/2<<std::endl;require(actual.size()==expected.size(),"partition frame count");double max=0;for(size_t i=0;i<actual.size();++i)max=std::max(max,double(std::abs(actual[i]-expected[i])));require(max<.00002,"packet partition changed PCM");
        if(rate==48000)require(actual==input,"48k identity PCM ordering");
        std::cout<<"rate="<<rate<<" output_frames="<<actual.size()/2<<" partition_max_error="<<max<<'\n';
        if(rate==44100){const auto old=legacy(input,rate);double square=0,peak=0;for(size_t i=0;i<std::min(old.size(),expected.size());++i){double e=old[i]-expected[i];square+=e*e;peak=std::max(peak,std::abs(e));}std::cout<<"before_legacy_44100_error_rms="<<std::sqrt(square/old.size())<<" peak="<<peak<<" (deterministic tone; not physical listening)\n";require(peak>.02,"legacy boundary defect not reproduced");}
    }
    // Silence is part of the stream, not a skipped SRC packet. Packet split
    // and drift-rate changes must retain the same phase as a streaming source.
    AudioResampler res;uint64_t longFrames=0;for(unsigned p=0;p<60000;++p){std::vector<float> zero(882);auto x=res.process(zero,44100);longFrames+=x.size()/2;require(std::all_of(x.begin(),x.end(),[](float v){return v==0;}),"silent PCM corrupt");}longFrames+=res.drain().size()/2;require(longFrames==600ULL*48000,"ten-minute silent SRC duration");std::cout<<"ten_minute_frames="<<longFrames<<" retained_src_frames=1\n";
    std::vector<float> stream=tone(44100,44100);std::fill(stream.begin()+20000,stream.begin()+30000,0.0f);
    AudioResampler whole,split;auto expected=whole.process(stream,44100);auto end=whole.drain();expected.insert(expected.end(),end.begin(),end.end());std::vector<float> result;
    for(size_t first=0;first<stream.size();first+=200){auto x=split.process(std::span(stream).subspan(first,std::min<size_t>(200,stream.size()-first)),44100);result.insert(result.end(),x.begin(),x.end());}end=split.drain();result.insert(result.end(),end.begin(),end.end());require(result.size()==expected.size(),"silence frame count");for(size_t i=0;i<result.size();++i)require(std::abs(result[i]-expected[i])<.00002,"silence transition phase");
    WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_IEEE_FLOAT;format.nChannels=2;format.wBitsPerSample=32;format.nBlockAlign=8;format.nSamplesPerSec=48000;format.nAvgBytesPerSec=384000;
    float native[]={.25f,-.5f,1.2f,-1.1f};auto pair=stereoPcm(reinterpret_cast<BYTE*>(native),1,&format);require(pair[0]==native[2]&&pair[1]==native[3],"float/headroom/stride corruption");
    WAVEFORMATEXTENSIBLE quad{};quad.Format=format;quad.Format.wFormatTag=WAVE_FORMAT_EXTENSIBLE;quad.Format.nChannels=4;quad.Format.nBlockAlign=16;quad.Format.cbSize=22;quad.SubFormat=KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;quad.dwChannelMask=KSAUDIO_SPEAKER_QUAD;float q[]={0,0,1,0};pair=stereoPcm(reinterpret_cast<BYTE*>(q),0,&quad.Format);require(std::abs(pair[0]-.7071f)<1e-6&&pair[1]==0,"quad channel mask");
    RecordingIngress stats,oldStats;AudioTimeline timeline,oldTimeline;RecordingAudioOptions options;options.gameMode=GameAudioMode::WholeSystem;options.gameVolume=1;options.microphoneEnabled=false;
    auto origin=std::chrono::steady_clock::now();std::vector<int16_t> mix,game,mic;
    for(uint64_t block=0;block<100;++block){RecordedAudio packet;packet.source=AudioSourceKind::Game;packet.stereo48k=std::vector<float>(2048,.125f);packet.continuousClock=true;packet.clockAnchor=origin;packet.firstFrame48k=block*1024;packet.capturedAt=origin+std::chrono::nanoseconds((block*1024+(block%2?1:0))*1'000'000'000ULL/48000);
        timeline.ingest(packet,origin,stats,options);packet.continuousClock=false;oldTimeline.ingest(packet,origin,oldStats,options);
        timeline.build(block*1024,1024,options,mix,game,mic,&stats);require(std::all_of(mix.begin(),mix.end(),[](int16_t v){return v==4095;}),"timeline jitter damaged samples");oldTimeline.build(block*1024,1024,options,mix,game,mic,&oldStats);
    }
    require(stats.audioTimelineMissing==0&&stats.audioTimelineOverwrites==0&&stats.lateAudioFrames==0,"continuous timeline gap/duplicate");require(oldStats.audioTimelineMissing>0,"legacy timestamp hole not reproduced");std::cout<<"timestamp_jitter_before_missing="<<oldStats.audioTimelineMissing<<" late="<<oldStats.lateAudioFrames<<" after_missing="<<stats.audioTimelineMissing<<" after_late="<<stats.lateAudioFrames<<'\n';
    // Four lossless checkpoints exercise the real bounded writer, with native
    // samples before conversion and final mixer samples after quantization.
    auto trace=std::make_shared<AudioCaptureTrace>(root/L"four-checkpoints");RecordingIngress traced;traced.audioTrace=trace;trace->origin(origin);AudioTimeline tracedTimeline;
    options.microphoneEnabled=true;options.microphoneVolume=1;
    for(uint64_t block=0;block<48;++block){auto pcm=tone(1024,48000);for(auto& v:pcm)v*=.2f;
        for(unsigned source=0;source<2;++source){trace->raw(source,origin+std::chrono::nanoseconds(block*1024*1'000'000'000ULL/48000),block*1024,0,&format,std::span(reinterpret_cast<const char*>(pcm.data()),pcm.size()*sizeof(float)));
            RecordedAudio packet;packet.source=source?AudioSourceKind::Microphone:AudioSourceKind::Game;packet.stereo48k=pcm;packet.continuousClock=true;packet.clockAnchor=origin;packet.firstFrame48k=block*1024;tracedTimeline.ingest(packet,origin,traced,options);}
        tracedTimeline.build(block*1024,1024,options,mix,game,mic,&traced);
    }
    trace->finish();require(traced.audioMixClipped==0,"unexpected amplification clipping");
    // Explicit deliberate sum clipping is reported, not hidden by a limiter.
    RecordingIngress clipped;AudioTimeline ct;for(auto kind:{AudioSourceKind::Game,AudioSourceKind::Microphone}){RecordedAudio p;p.source=kind;p.capturedAt=origin;p.stereo48k={.8f,.8f};ct.ingest(p,origin,clipped,options);}ct.build(0,1,options,mix,game,mic,&clipped);require(clipped.audioMixClipped==1&&mix[0]==32767,"mix clipping counter");
    auto blocked=root/L"blocked-file";{std::ofstream f(blocked);f<<"controlled failure";}AudioCaptureTrace broken(blocked/L"trace");broken.origin(origin);broken.mix(0,std::vector<int16_t>(2048));broken.finish();require(!broken.active()&&broken.failed(),"diagnostic failure worker stuck/hidden");
    std::cout<<"PASS SRC ordering/count/packet boundary/silence, 10min duration, PCM stride/channel mask, timestamp jitter, gains/clipping, four WAV finalization, bounded diagnostic failure.\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
