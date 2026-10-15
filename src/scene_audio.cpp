#include "scene_audio.h"
#include "audio_capture.h"
#include "audio_timeline.h"
#include "application_picker.h"
#include <QSet>
#include <map>

namespace Scenes {
namespace {
class Session final:public SceneAudioProvider {
    struct Input {
        Definition definition;
        std::shared_ptr<RecordingIngress> ingress=std::make_shared<RecordingIngress>();
        std::unique_ptr<AudioCapture> capture;
        std::unique_ptr<AudioTimeline> timeline;
        std::wstring endpoint,key;DWORD pid{};unsigned attempts{};
        float gain{};int sync{};std::vector<float> pcm;
        uint64_t meterPackets{};
        std::chrono::steady_clock::time_point retry{};
    };
    Snapshot initial_;std::function<Snapshot()> current_;quintptr desktop_;
    std::vector<std::unique_ptr<Input>> inputs_;
    std::vector<std::wstring> names_;
    std::map<std::wstring,float> mixGains_;
    std::shared_ptr<RecordingIngress> output_;
    std::chrono::steady_clock::time_point origin_{};
    bool stopped_{};
    bool fixture_{};
    QString configurationError_;
    std::atomic_bool stopping_{};
    std::thread manager_;
    mutable std::mutex statusMutex_;
    std::vector<std::pair<std::wstring,std::wstring>> statuses_;
    static int16_t pcm(float v){return int16_t(std::clamp(v,-1.f,1.f)*32767.f);}
    void open(Input& input){
        auto now=std::chrono::steady_clock::now();if(input.capture||input.attempts>=3||now<input.retry)return;
        auto kind=input.definition.kind;GameAudioMode mode=GameAudioMode::Off;DWORD pid=0;
        if(kind==Kind::ApplicationAudio){auto found=discoverApplications(desktop_,GetCurrentProcessId());QSet<quint32> pids;for(auto& w:found.windows)if(w.executable.compare(input.definition.settings["executable"].toString(),Qt::CaseInsensitive)==0&&w.title==input.definition.settings["target"].toString())pids.insert(w.processId);
            if(pids.size()!=1){input.retry=now+std::chrono::seconds(2);std::lock_guard lock(output_->mutex);output_->audioError=L"Scene application audio unavailable: "+input.definition.name.toStdWString()+L". Reopen the selected application; no system fallback.";return;}pid=*pids.begin();mode=GameAudioMode::SelectedApplication;
        }else if(kind==Kind::DesktopAudio)mode=GameAudioMode::WholeSystem;
        if(pid)for(auto& other:inputs_)if(other.get()!=&input&&other->capture&&other->pid==pid){input.attempts=3;std::lock_guard lock(output_->mutex);output_->audioError=L"Application audio selections now resolve to the same process. Restart recording/replay to share capture; the additional source remains silent.";return;}
        auto capture=std::make_unique<AudioCapture>(kind==Kind::Microphone?AudioSourceKind::Microphone:AudioSourceKind::Game,input.ingress,pid,mode,input.endpoint);
        ++input.attempts;std::wstring error;if(capture->start(error)){input.capture=std::move(capture);input.pid=pid;}
        else{input.retry=now+std::chrono::seconds(2);std::lock_guard lock(output_->mutex);output_->audioError=L"Scene audio '"+input.definition.name.toStdWString()+L"': "+error;}
    }
public:
    Session(Snapshot initial,std::function<Snapshot()> current,quintptr desktop):initial_(std::move(initial)),current_(std::move(current)),desktop_(desktop){
        QSet<QString> used;for(auto& s:initial_.scenes)for(auto& a:s.audio)used.insert(a.sourceId);
        for(auto& d:initial_.definitions)if(used.contains(d.id)&&isAudio(d.kind)){
            auto input=std::make_unique<Input>();input->definition=d;
            // Sync is frozen at this encoding-session boundary; switching gains
            // and mutes does not remap existing sample indexes.
            bool haveSync=false;
            for(auto& s:initial_.scenes)for(auto& a:s.audio)if(a.sourceId==d.id){
                auto sync=int(std::round(a.syncMs));if(haveSync&&sync!=input->sync)configurationError_="A shared audio definition has different sync adjustments across scenes. Use the same adjustment or separate definitions.";
                input->sync=sync;haveSync=true;
            }
            names_.push_back(d.name.toStdWString());inputs_.push_back(std::move(input));
        }
    }
    ~Session()override{stop();}
    std::vector<std::wstring> stems()const override{return names_;}
    std::vector<bool> microphoneStems()const override{std::vector<bool> kinds;for(auto& input:inputs_)kinds.push_back(input->definition.kind==Kind::Microphone);return kinds;}
#ifdef REDOT_TEST_FIXTURE
    void fixture(){fixture_=true;}
    std::vector<unsigned> verificationAttempts()const{std::vector<unsigned> result;if(!stopped_)return result;for(auto& input:inputs_)result.push_back(input->attempts);return result;}
    bool submit(const QString& id,RecordedAudio packet){for(auto& input:inputs_)if(input->definition.id==id){input->ingress->push(std::move(packet));return true;}return false;}
#endif
    std::vector<std::pair<std::wstring,std::wstring>> status()const override{std::lock_guard lock(statusMutex_);return statuses_;}
    int advanceMilliseconds()const override{int n=0;for(auto& input:inputs_)n=std::max(n,-input->sync);return n;}
    bool start(std::shared_ptr<RecordingIngress> out,std::wstring& error)override{
        output_=std::move(out);if(inputs_.size()>16){error=L"At most 16 audio source definitions across scenes can be encoded in one session.";return false;}
        if(!configurationError_.isEmpty()){error=configurationError_.toStdWString();return false;}
        std::map<std::wstring,Input*> opened;
        bool applications=false;for(auto& input:inputs_)applications|=input->definition.kind==Kind::ApplicationAudio;
        auto discovered=applications&&!fixture_?discoverApplications(desktop_,GetCurrentProcessId()):ApplicationDiscovery{};
        for(auto& input:inputs_){input->timeline=std::make_unique<AudioTimeline>();auto& d=input->definition;
            if(d.kind!=Kind::ApplicationAudio){input->endpoint=d.settings["deviceId"].toString().toStdWString();if(input->endpoint.empty()&&!fixture_)input->endpoint=defaultAudioEndpointId(d.kind==Kind::Microphone);if(input->endpoint.empty()){error=L"No Windows default endpoint for scene source '"+d.name.toStdWString()+L"'. Select an available device in Scenes; no fallback.";return false;}}
            input->key=d.kind==Kind::ApplicationAudio?L"app:"+d.settings["executable"].toString().toStdWString()+L":"+d.settings["target"].toString().toStdWString():(d.kind==Kind::Microphone?L"mic:":L"desktop:")+input->endpoint;
            if(d.kind==Kind::ApplicationAudio){QSet<quint32> pids;for(auto& window:discovered.windows)if(window.executable.compare(d.settings["executable"].toString(),Qt::CaseInsensitive)==0&&window.title==d.settings["target"].toString())pids.insert(window.processId);
#ifdef REDOT_TEST_FIXTURE
                if(fixture_&&d.settings["verificationProcessId"].toInt()>0)pids.insert(quint32(d.settings["verificationProcessId"].toInt()));
#endif
                if(pids.size()==1)input->key=L"app-process:"+std::to_wstring(*pids.begin());
            }
            if(!opened.contains(input->key))opened[input->key]=input.get();
            else if(opened[input->key]->sync!=input->sync){error=L"Definitions sharing one physical audio endpoint must use the same sync adjustment.";return false;}
        }
        if(fixture_)return true;
        manager_=std::thread([this]{
            CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            std::map<std::wstring,Input*> unique;for(auto& input:inputs_)unique.try_emplace(input->key,input.get());
            while(!stopping_){for(auto& [key,input]:unique){if(stopping_)break;
                if(input->capture){auto index=input->definition.kind==Kind::Microphone?1:0;bool disconnected=false;{std::lock_guard lock(input->ingress->mutex);disconnected=!input->ingress->audioSourceOpen[index];}
                    if(input->pid){auto process=OpenProcess(SYNCHRONIZE,FALSE,input->pid);if(!process||WaitForSingleObject(process,0)==WAIT_OBJECT_0)disconnected=true;if(process)CloseHandle(process);}
                    if(disconnected){input->capture->stop();input->capture.reset();input->retry=std::chrono::steady_clock::now()+std::chrono::seconds(2);}}
                open(*input);
            }std::this_thread::sleep_for(std::chrono::milliseconds(100));}
            for(auto& input:inputs_)if(input->capture){input->capture->stop();input->capture.reset();}
            CoUninitialize();
        });return true;
    }
    void origin(std::chrono::steady_clock::time_point value)override{origin_=value;}
    void render(uint64_t first,uint64_t frames,std::vector<int16_t>& mix,std::vector<std::vector<int16_t>>& stems)override{
        auto snapshot=current_();const Scene* scene=nullptr;for(auto& s:snapshot.scenes)if(s.id==snapshot.selected)scene=&s;
        std::map<std::wstring,Input*> unique;for(auto& input:inputs_)unique.try_emplace(input->key,input.get());
        // Fan out one physical capture packet to per-definition timelines. A
        // source used twice in one scene is mixed only once.
        struct Group {bool open{},muted{},changed{};double square{};float peak{},volume{1};uint64_t frames{},time{};std::wstring names,ids,status;};std::array<Group,2> groups;
        for(auto& [key,input]:unique){
            std::deque<RecordedAudio> packets;std::array<std::wstring,2> statuses,names;std::array<bool,2> isOpen{},muted{};std::array<float,2> volume{};std::array<std::wstring,2> endpointIds;
            {std::lock_guard lock(input->ingress->mutex);packets.swap(input->ingress->audio);statuses=input->ingress->audioSourceStatus;names=input->ingress->audioEndpointNames;isOpen=input->ingress->audioSourceOpen;muted=input->ingress->audioEndpointMuted;volume=input->ingress->audioEndpointVolume;endpointIds=input->ingress->audioEndpointIds;}
            for(auto& lane:inputs_)if(lane->key==key){RecordingAudioOptions options;options.gameSyncMilliseconds=options.microphoneSyncMilliseconds=lane->sync;for(auto& packet:packets)lane->timeline->ingest(packet,origin_,*output_,options);}
            bool mic=input->definition.kind==Kind::Microphone;auto index=mic?1:0;auto meter=input->ingress->rawMeters[index].snapshot(GetTickCount64());
            bool changed=meter.packets!=input->meterPackets;input->meterPackets=meter.packets;
            bool assigned=false;if(scene)for(auto& assignment:scene->audio)for(auto& lane:inputs_)if(lane->key==key&&lane->definition.id==assignment.sourceId)assigned=true;
            if(assigned){auto& group=groups[index];group.changed|=changed;group.open|=isOpen[index];group.muted|=muted[index];group.volume=std::min(group.volume,volume[index]);group.square+=meter.square;group.frames+=meter.frames;group.peak=std::max(group.peak,meter.peak);group.time=std::max(group.time,meter.timeMs);if(!names[index].empty()){if(!group.names.empty())group.names+=L"; ";group.names+=names[index];}if(!group.status.empty())group.status+=L"; ";group.status+=statuses[index];if(group.ids.empty())group.ids=endpointIds[index];}
            // Transfer raw counters once per physical capture, never per alias.
            auto& source=*input->ingress;
            for(auto member:{&RecordingIngress::audioPackets,&RecordingIngress::audioInputFrames,&RecordingIngress::audioSilentFrames,&RecordingIngress::audioNonzeroFrames,&RecordingIngress::audioDiscontinuities,&RecordingIngress::audioMissingInputFrames,&RecordingIngress::audioDuplicateInputFrames,&RecordingIngress::audioTimestampJumps,&RecordingIngress::audioSourceClipping})(output_.get()->*member)[index]+=(source.*member)[index].exchange(0);
            output_->audioLastPacketMilliseconds[index]=source.audioLastPacketMilliseconds[index].load();
            output_->audioQueueDrops+=source.audioQueueDrops.exchange(0);output_->timestampFallbacks+=source.timestampFallbacks.exchange(0);
            auto high=source.audioQueueHighWater.load();auto prior=output_->audioQueueHighWater.load();while(high>prior&&!output_->audioQueueHighWater.compare_exchange_weak(prior,high)){}

        }
        for(size_t index=0;index<groups.size();++index){auto& group=groups[index];if(group.changed&&group.frames)output_->rawMeters[index].publish(group.square,group.peak,group.frames,group.time);std::lock_guard lock(output_->mutex);output_->audioSourceOpen[index]=group.open;output_->audioEndpointNames[index]=group.names;output_->audioSourceStatus[index]=group.status;output_->audioEndpointIds[index]=group.ids;output_->audioEndpointMuted[index]=group.muted;output_->audioEndpointVolume[index]=group.volume;}
        std::vector<std::pair<std::wstring,std::wstring>> statuses;
        for(auto& lane:inputs_){auto physical=unique.at(lane->key);auto index=lane->definition.kind==Kind::Microphone?1:0;auto meter=physical->ingress->rawMeters[index].snapshot(GetTickCount64());std::lock_guard lock(physical->ingress->mutex);auto text=physical->ingress->audioSourceOpen[index]?L"Open: "+physical->ingress->audioEndpointNames[index]+(meter.displayedPeak>0.00003f?L" · raw signal":L" · silent"):L"Unavailable/opening · "+physical->ingress->audioSourceStatus[index];statuses.emplace_back(lane->definition.id.toStdWString(),text);}
        {std::lock_guard lock(statusMutex_);statuses_=std::move(statuses);}
        stems.resize(inputs_.size());mix.assign(frames*2,0);std::vector<float> sum(frames*2,0);
        std::map<std::wstring,std::pair<Input*,float>> mixedInputs;
        for(size_t n=0;n<inputs_.size();++n){auto& input=*inputs_[n];input.timeline->extract(first,frames,input.definition.kind==Kind::Microphone?AudioSourceKind::Microphone:AudioSourceKind::Game,input.pcm,*output_);
            float target=0;if(scene)for(auto& assignment:scene->audio)if(assignment.sourceId==input.definition.id&&!assignment.muted)target=float(assignment.gain);
            auto [entry,inserted]=mixedInputs.try_emplace(input.key,&input,target);
            if(!inserted&&entry->second.second==0&&target>0)entry->second={&input,target};
            auto& stem=stems[n];stem.resize(frames*2);auto old=input.gain;
            for(uint64_t f=0;f<frames;++f){float gain=old+(target-old)*float(std::min<uint64_t>(f+1,240))/240.f;for(unsigned channel=0;channel<2;++channel){auto index=f*2+channel;stem[index]=pcm(input.pcm[index]*gain);}}
            input.gain=target;
        }
        // One gain transition per physical stream, even when scenes assign
        // different aliases of that device. Never sum two aliases at a switch.
        for(auto& [key,entry]:mixedInputs){auto [input,target]=entry;auto& old=mixGains_[key];
            for(uint64_t f=0;f<frames;++f){float gain=old+(target-old)*float(std::min<uint64_t>(f+1,240))/240.f;for(unsigned c=0;c<2;++c)sum[f*2+c]+=input->pcm[f*2+c]*gain;}old=target;
        }
        uint64_t clipped=0;for(size_t n=0;n<sum.size();++n){if(std::abs(sum[n])>1)++clipped;mix[n]=pcm(sum[n]);}output_->audioMixClipped+=clipped;
        if(scene)for(auto& assignment:scene->audio){auto original=std::find_if(inputs_.begin(),inputs_.end(),[&](auto& i){return i->definition.id==assignment.sourceId;});bool changed=original==inputs_.end();
            if(!changed){auto definition=std::find_if(snapshot.definitions.begin(),snapshot.definitions.end(),[&](auto& d){return d.id==assignment.sourceId;});changed=definition==snapshot.definitions.end()||definition->kind!=(*original)->definition.kind||definition->settings!=(*original)->definition.settings||int(std::round(assignment.syncMs))!=(*original)->sync;}
            if(changed){std::lock_guard lock(output_->mutex);output_->audioError=L"New/changed scene audio device or sync is saved for the next encoding session. Live gain/mute and existing scene switching continue.";}
        }
    }
    void stop()override{if(stopped_)return;stopped_=true;stopping_=true;if(manager_.joinable())manager_.join();}
};
}
std::shared_ptr<SceneAudioProvider> makeAudioSession(const Snapshot& initial,std::function<Snapshot()> current,quintptr desktop){return std::make_shared<Session>(initial,std::move(current),desktop);}
#ifdef REDOT_TEST_FIXTURE
std::shared_ptr<SceneAudioProvider> makeVerificationAudioSession(const Snapshot& initial,std::function<Snapshot()> current){auto session=std::make_shared<Session>(initial,std::move(current),0);session->fixture();return session;}
bool submitVerificationAudio(const std::shared_ptr<SceneAudioProvider>& provider,const QString& id,RecordedAudio packet){auto session=dynamic_cast<Session*>(provider.get());return session&&session->submit(id,std::move(packet));}
std::vector<unsigned> verificationAudioOpenAttempts(const std::shared_ptr<SceneAudioProvider>& provider){auto session=dynamic_cast<Session*>(provider.get());return session?session->verificationAttempts():std::vector<unsigned>{};}
#endif
}
