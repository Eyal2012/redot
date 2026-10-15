#pragma once
#include <windows.h>
#include <mmreg.h>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>
#include <cmath>
#include <algorithm>

// Opt-in lossless checkpoints. All writes and WAV finalization are on one
// bounded worker; recording never waits for diagnostic I/O at packet delivery.
class AudioCaptureTrace {
    using Clock=std::chrono::steady_clock;
    struct Event {
        unsigned stage{};Clock::time_point at{};uint64_t position{},frame{};DWORD flags{};
        bool indexed{};std::vector<char> format,data;
    };
    struct Wave {
        std::fstream file;std::vector<char> format;uint64_t frames{},overlaps{},gaps{};
        DWORD rate{};WORD align{};char silence{};std::streamoff sizeAt{},dataAt{};
        bool anchored{};uint64_t initialPosition{},expectedPosition{};int64_t initialFrame{};
        static void u32(std::ostream& out,uint32_t value){out.write(reinterpret_cast<const char*>(&value),4);}
        bool open(const std::filesystem::path& path,const std::vector<char>& fmt){
            if(fmt.size()<sizeof(WAVEFORMATEX))return false;
            format=fmt;WAVEFORMATEX f{};memcpy(&f,fmt.data(),sizeof(f));rate=f.nSamplesPerSec;align=f.nBlockAlign;
            if(!rate || !align || align>256)return false;
            silence=f.wFormatTag==WAVE_FORMAT_PCM && f.wBitsPerSample==8?std::bit_cast<char>(uint8_t(128)):0;
            file.open(path,std::ios::binary|std::ios::in|std::ios::out|std::ios::trunc);
            file.write("RIFF",4);u32(file,0);file.write("WAVEfmt ",8);u32(file,static_cast<uint32_t>(fmt.size()));file.write(fmt.data(),fmt.size());
            if(fmt.size()%2)file.put(0);
            file.write("fact",4);u32(file,4);u32(file,0);
            file.write("data",4);sizeAt=file.tellp();u32(file,0);dataAt=file.tellp();return bool(file);
        }
        void zero(uint64_t count){
            std::vector<char> silenceBlock(4096*align,silence);
            while(count){const auto n=std::min<uint64_t>(4096,count);file.write(silenceBlock.data(),n*align);frames+=n;count-=n;}
        }
        bool put(int64_t first,std::span<const char> data,uint64_t limit){
            if(data.size()%align)return false;
            uint64_t count=data.size()/align,skip{};
            if(first<0){skip=std::min<uint64_t>(count,static_cast<uint64_t>(-first));first+=skip;count-=skip;}
            if(!count || static_cast<uint64_t>(first)>=limit)return true;
            if(static_cast<uint64_t>(first)<frames){const auto overlap=std::min(count,frames-static_cast<uint64_t>(first));overlaps+=overlap;skip+=overlap;first+=overlap;count-=overlap;}
            if(!count)return true;
            if(static_cast<uint64_t>(first)>frames){const auto gap=std::min<uint64_t>(limit-frames,static_cast<uint64_t>(first)-frames);gaps+=gap;zero(gap);}
            count=std::min(count,limit-frames);file.write(data.data()+skip*align,count*align);frames+=count;return bool(file);
        }
        bool finish(uint64_t end){
            if(!file.is_open())return true;
            if(frames<end)zero(end-frames);
            const auto bytes=frames*align;file.seekp(sizeAt);u32(file,static_cast<uint32_t>(bytes));
            file.seekp(dataAt-12);u32(file,static_cast<uint32_t>(frames));
            file.seekp(4);u32(file,static_cast<uint32_t>(dataAt+bytes-8));file.flush();const bool ok=bool(file);file.close();return ok;
        }
    };
    static constexpr size_t QueueBytes=16*1024*1024, QueueEvents=2048;
    static constexpr uint64_t Seconds=120;
    std::filesystem::path folder_;
    std::mutex mutex_;std::condition_variable wake_;std::deque<Event> queue_;size_t bytes_{};
    bool stop_{},originReady_{};Clock::time_point origin_{};
    std::atomic_bool accepting_{true},failed_{};std::atomic_uint64_t lost_{};
    std::thread worker_;
    void run(){
        try {
            std::filesystem::create_directories(folder_);
            std::ofstream csv(folder_/L"packets.csv");csv<<"stage,qpc_relative_ns,device_position,frames,flags,file_first_frame\n";
            Wave waves[4];uint64_t mixEnd{};bool failed=false;
            const wchar_t* names[]={L"01-Raw Desktop.wav",L"02-Processed Desktop.wav",L"03-Raw Microphone.wav",L"04-Final Mix.wav"};
            for(;;){
                Event e;
                {std::unique_lock lock(mutex_);wake_.wait(lock,[&]{return stop_ || (originReady_&&!queue_.empty());});
                 if(queue_.empty() || !originReady_)break;e=std::move(queue_.front());queue_.pop_front();bytes_-=e.data.size()+e.format.size();}
                auto& w=waves[e.stage];
                if(!w.file.is_open() && !w.open(folder_/names[e.stage],e.format)){failed=true;break;}
                if(w.format!=e.format){failed=true;break;} // never combine incompatible native formats
                const auto ns=e.indexed?static_cast<int64_t>(e.frame*1'000'000'000ULL/48000):std::chrono::duration_cast<std::chrono::nanoseconds>(e.at-origin_).count();
                int64_t first=e.indexed?static_cast<int64_t>(e.frame):std::llround(static_cast<long double>(ns)*w.rate/1'000'000'000);
                if(!e.indexed){
                    if(!w.anchored || e.position<w.initialPosition || (e.position>w.expectedPosition && e.position-w.expectedPosition>w.rate*2ULL)){
                        w.anchored=true;w.initialPosition=e.position;w.initialFrame=first;
                    }
                    // Device positions preserve native samples; CSV retains exact
                    // QPC timing. If a virtual source supplies no progression,
                    // append by frame count rather than overwriting position zero.
                    if(e.position==0 && w.expectedPosition>0)first=static_cast<int64_t>(w.frames);
                    else first=w.initialFrame+static_cast<int64_t>(e.position-w.initialPosition);
                    w.expectedPosition=e.position+e.data.size()/w.align;
                }
                csv<<e.stage<<','<<ns<<','<<e.position<<','<<e.data.size()/w.align<<','<<e.flags<<','<<first<<'\n';
                const uint64_t capacity=std::min<uint64_t>(Seconds*w.rate,64*1024*1024/w.align);
                if(!w.put(first,e.data,capacity)){failed=true;break;}
                if(w.frames==capacity && capacity<Seconds*w.rate){failed=true;break;}
                if(e.stage==3){mixEnd=std::min<uint64_t>(Seconds*48000,e.frame+e.data.size()/w.align);if(mixEnd==Seconds*48000)accepting_=false;}
            }
            for(auto& w:waves)if(w.file.is_open())failed |= !w.finish(std::min<uint64_t>(std::min<uint64_t>(Seconds*w.rate,64*1024*1024/w.align),(mixEnd*w.rate+47999)/48000));
            csv.flush();failed |= !csv || lost_.load()!=0 || !originReady_ || mixEnd==0;
            std::ofstream summary(folder_/L"summary.txt");
            summary<<"finalized="<<!failed<<"\ndropped_events="<<lost_<<"\nlimit_seconds="<<Seconds<<"\nmix_frames="<<mixEnd<<'\n';
            for(unsigned s=0;s<4;++s)summary<<"stage_"<<s<<"_rate="<<waves[s].rate<<"\nstage_"<<s<<"_frames="<<waves[s].frames<<"\nstage_"<<s<<"_overlap_frames="<<waves[s].overlaps<<"\nstage_"<<s<<"_gap_frames="<<waves[s].gaps<<'\n';
            summary<<"Raw WAV rates are native and samples unmodified. Start padding aligns the common clock within one native sample; device drift is retained in raw samples and exact QPC is in packets.csv. Processed/Mix are 48kHz on the recording clock. No listening verification implied. Files limited to 64 MiB each.\n";
            summary.flush();failed |= !summary;failed_=failed;
        }catch(...){++lost_;failed_=true;}
        accepting_=false;
    }
    void enqueue(Event e){
        if(!accepting_)return;
        const auto size=e.data.size()+e.format.size();std::lock_guard lock(mutex_);
        if(stop_ || bytes_+size>QueueBytes || queue_.size()>=QueueEvents){++lost_;return;}
        bytes_+=size;queue_.push_back(std::move(e));wake_.notify_one();
    }
    static std::vector<char> fmt(WORD type,WORD bits){WAVEFORMATEX f{};f.wFormatTag=type;f.nChannels=2;f.nSamplesPerSec=48000;f.wBitsPerSample=bits;f.nBlockAlign=2*bits/8;f.nAvgBytesPerSec=f.nSamplesPerSec*f.nBlockAlign;const auto p=reinterpret_cast<const char*>(&f);return {p,p+sizeof(f)};}
public:
    explicit AudioCaptureTrace(std::filesystem::path folder):folder_(std::move(folder)),worker_([this]{run();}){}
    ~AudioCaptureTrace(){finish();}
    static std::shared_ptr<AudioCaptureTrace> fromEnvironment(){
        wchar_t path[32768]{};if(!GetEnvironmentVariableW(L"PANDACLIP_AUDIO_CAPTURE_TRACE",path,32768))return {};
        std::error_code ec;const auto root=std::filesystem::weakly_canonical(path,ec),build=std::filesystem::weakly_canonical(std::filesystem::current_path()/L"build",ec);
        auto relative=root.lexically_relative(build);if(ec || relative.empty() || *relative.begin()==L"..")return {};
        static std::atomic_uint sessions{};const auto id=++sessions;if(id>4)return {};
        const auto folder=root/(L"session-"+std::to_wstring(id));if(std::filesystem::exists(folder))return {};
        return std::make_shared<AudioCaptureTrace>(folder);
    }
    const std::filesystem::path& folder()const{return folder_;}
    bool active()const{return accepting_;}
    bool failed()const{return failed_;}
    uint64_t droppedEvents()const{return lost_;}
    void origin(Clock::time_point at){std::lock_guard lock(mutex_);if(!originReady_){origin_=at;originReady_=true;}wake_.notify_one();}
    void raw(unsigned source,Clock::time_point at,uint64_t position,DWORD flags,const WAVEFORMATEX* format,std::span<const char> data){
        if(!active())return;Event e;e.stage=source==0?0:2;e.at=at;e.position=position;e.flags=flags;
        const auto p=reinterpret_cast<const char*>(format);e.format.assign(p,p+sizeof(*format)+format->cbSize);e.data.assign(data.begin(),data.end());enqueue(std::move(e));
    }
    void processed(uint64_t first,std::span<const float> data){
        if(!active())return;Event e;e.stage=1;e.indexed=true;e.frame=first;e.format=fmt(WAVE_FORMAT_IEEE_FLOAT,32);
        const auto p=reinterpret_cast<const char*>(data.data());e.data.assign(p,p+data.size_bytes());enqueue(std::move(e));
    }
    void mix(uint64_t first,std::span<const int16_t> data){
        if(!active())return;Event e;e.stage=3;e.indexed=true;e.frame=first;e.format=fmt(WAVE_FORMAT_PCM,16);
        const auto p=reinterpret_cast<const char*>(data.data());e.data.assign(p,p+data.size_bytes());enqueue(std::move(e));
    }
    void finish(){if(worker_.joinable()){ {std::lock_guard lock(mutex_);stop_=true;}wake_.notify_one();worker_.join();}}
};
