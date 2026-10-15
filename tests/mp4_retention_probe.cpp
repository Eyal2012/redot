// Isolates compressed MP4 muxing from Qt, capture, GPU, NVENC and audio DSP.
// Accelerated media time is NOT a wall-time soak. Uses owned build fixtures.
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>
#include <algorithm>
#include "resource_probe.h"
using Microsoft::WRL::ComPtr;
static void checked(HRESULT hr){if(FAILED(hr))throw hr;}
struct Packet{DWORD stream{};LONGLONG pts{},duration{};UINT32 clean{};std::vector<BYTE> bytes;};
int wmain(int argc,wchar_t** argv){
    if(argc!=5)return 2;
    const std::filesystem::path input=argv[1],root=argv[2];const bool mux=std::wstring(argv[3])==L"mux";
    const unsigned seconds=std::clamp(unsigned(std::stoul(argv[4])),10u,14400u);
    auto build=std::filesystem::weakly_canonical(std::filesystem::current_path()/L"build");
    auto owned=[&](const auto& p){auto relative=std::filesystem::weakly_canonical(p).lexically_relative(build);return !relative.empty()&&*relative.begin()!=L"..";};
    if(!owned(input)||!owned(root)||std::filesystem::exists(root))return 2;
    std::filesystem::create_directories(root);checked(CoInitializeEx(nullptr,COINIT_MULTITHREADED));checked(MFStartup(MF_VERSION));
    try{
        std::map<DWORD,ComPtr<IMFMediaType>> types;std::vector<Packet> packets;LONGLONG end{},origin=MAXLONGLONG;
        {
            ComPtr<IMFSourceReader> reader;checked(MFCreateSourceReaderFromURL(input.c_str(),nullptr,&reader));
            for(DWORD n=0;n<32;++n){ComPtr<IMFMediaType> type;if(FAILED(reader->GetNativeMediaType(n,0,&type)))break;
                GUID major{},subtype{};checked(type->GetGUID(MF_MT_MAJOR_TYPE,&major));checked(type->GetGUID(MF_MT_SUBTYPE,&subtype));
                if(subtype!=MFVideoFormat_H264&&subtype!=MFAudioFormat_AAC)throw E_INVALIDARG;
                checked(reader->SetCurrentMediaType(n,nullptr,type.Get()));checked(reader->SetStreamSelection(n,TRUE));types[n]=type;
            }
            uint64_t cachedBytes{};
            while(true){DWORD stream{},flags{};LONGLONG timestamp{};ComPtr<IMFSample> sample;
                checked(reader->ReadSample(MF_SOURCE_READER_ANY_STREAM,0,&stream,&flags,&timestamp,&sample));
                if(flags&MF_SOURCE_READERF_ENDOFSTREAM){bool all=true;for(auto& [id,type]:types){BOOL selected{};reader->GetStreamSelection(id,&selected);if(selected){if(id==stream)reader->SetStreamSelection(id,FALSE);else all=false;}}if(all)break;}
                if(!sample)continue;Packet packet;packet.stream=stream;checked(sample->GetSampleTime(&packet.pts));checked(sample->GetSampleDuration(&packet.duration));sample->GetUINT32(MFSampleExtension_CleanPoint,&packet.clean);
                ComPtr<IMFMediaBuffer> buffer;checked(sample->ConvertToContiguousBuffer(&buffer));BYTE* data{};DWORD length{};checked(buffer->Lock(&data,nullptr,&length));packet.bytes.assign(data,data+length);buffer->Unlock();
                cachedBytes+=length;if(cachedBytes>64ull*1024*1024||packets.size()>10000)throw E_INVALIDARG;
                origin=std::min(origin,packet.pts);end=std::max(end,packet.pts+packet.duration);packets.push_back(std::move(packet));
            }
        }
        if(packets.empty()||end<=origin)throw E_INVALIDARG;
        std::stable_sort(packets.begin(),packets.end(),[](auto& a,auto& b){return a.pts<b.pts;});
        const auto period=end-origin;ComPtr<IMFSinkWriter> writer;std::map<DWORD,DWORD> streams;
        if(mux){checked(MFCreateSinkWriterFromURL((root/L"mux-retention.mp4").c_str(),nullptr,nullptr,&writer));
            for(auto& [id,type]:types){DWORD output{};checked(writer->AddStream(type.Get(),&output));checked(writer->SetInputMediaType(output,type.Get(),nullptr));streams[id]=output;}checked(writer->BeginWriting());}
        std::ofstream csv(root/L"resources.csv");csv<<"media_seconds,packets,private_bytes,working_set_bytes,handles,threads,private_region_commit,phase\n";
        auto sampleMemory=[&](double time,uint64_t count,const char* phase){auto m=probeResources(GetCurrentProcess(),GetCurrentProcessId(),true);csv<<time<<","<<count<<","<<m.privateBytes<<","<<m.workingSet<<","<<m.handles<<","<<m.threads<<","<<m.privateRegions<<","<<phase<<"\n";csv.flush();};
        uint64_t count{};sampleMemory(0,0,"started");const auto wall=GetTickCount64();
        for(LONGLONG epoch=0;epoch<LONGLONG(seconds)*10000000;epoch+=period){
            for(auto& p:packets){auto pts=epoch+p.pts-origin;if(pts>=LONGLONG(seconds)*10000000)continue;
                ComPtr<IMFMediaBuffer> buffer;checked(MFCreateMemoryBuffer(DWORD(p.bytes.size()),&buffer));BYTE* data{};checked(buffer->Lock(&data,nullptr,nullptr));CopyMemory(data,p.bytes.data(),p.bytes.size());buffer->Unlock();checked(buffer->SetCurrentLength(DWORD(p.bytes.size())));
                ComPtr<IMFSample> sample;checked(MFCreateSample(&sample));checked(sample->AddBuffer(buffer.Get()));checked(sample->SetSampleTime(pts));checked(sample->SetSampleDuration(p.duration));if(p.clean)checked(sample->SetUINT32(MFSampleExtension_CleanPoint,TRUE));
                if(writer)checked(writer->WriteSample(streams[p.stream],sample.Get()));++count;
            }
            if(epoch% (period*10)==0)sampleMemory(double(epoch)/10000000,count,"writing");
        }
        sampleMemory(seconds,count,"before-finalize");if(writer)checked(writer->Finalize());writer.Reset();sampleMemory(seconds,count,"writer-released");
        for(unsigned n=0;n<5;++n){Sleep(1000);sampleMemory(seconds,count,"post-finalize");}
        std::ofstream report(root/L"result.txt");report<<"PASS media_seconds="<<seconds<<" packets="<<count<<" tracks="<<types.size()<<" elapsed_ms="<<GetTickCount64()-wall<<" mux="<<mux<<"\nAccelerated mux-only attribution probe; not multi-hour wall-time operation or valid uninterrupted source content. Repeated fixture boundaries can have AAC/GOP seams.\n";
        MFShutdown();CoUninitialize();return 0;
    }catch(HRESULT hr){std::cerr<<"FAIL HRESULT="<<std::hex<<hr<<std::endl;MFShutdown();CoUninitialize();return 1;}
}
