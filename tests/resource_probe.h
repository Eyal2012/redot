#pragma once
// Read-only resource census for owned verification processes. Not shipping UI.
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <cstdint>
struct ResourceProbe {
    uint64_t privateBytes{}, workingSet{}, pagefileCommit{}, peakCommit{};
    DWORD handles{}, threads{};
    uint64_t privateRegions{}, mappedRegions{}, imageRegions{}, committedRegions{};
    unsigned regions{};
};
inline ResourceProbe probeResources(HANDLE process=GetCurrentProcess(),DWORD pid=GetCurrentProcessId(),bool regions=false) {
    ResourceProbe result;PROCESS_MEMORY_COUNTERS_EX memory{sizeof(memory)};
    if(GetProcessMemoryInfo(process,reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))){
        result.privateBytes=memory.PrivateUsage;result.workingSet=memory.WorkingSetSize;
        result.pagefileCommit=memory.PagefileUsage;result.peakCommit=memory.PeakPagefileUsage;
    }
    GetProcessHandleCount(process,&result.handles);
    auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(snapshot!=INVALID_HANDLE_VALUE){THREADENTRY32 entry{sizeof(entry)};
        if(Thread32First(snapshot,&entry))do{if(entry.th32OwnerProcessID==pid)++result.threads;}while(Thread32Next(snapshot,&entry));CloseHandle(snapshot);}
    if(regions){uintptr_t address{};MEMORY_BASIC_INFORMATION info{};
        while(VirtualQueryEx(process,reinterpret_cast<void*>(address),&info,sizeof(info))==sizeof(info)){
            ++result.regions;if(info.State==MEM_COMMIT){result.committedRegions+=info.RegionSize;
                if(info.Type==MEM_PRIVATE)result.privateRegions+=info.RegionSize;
                else if(info.Type==MEM_MAPPED)result.mappedRegions+=info.RegionSize;
                else if(info.Type==MEM_IMAGE)result.imageRegions+=info.RegionSize;}
            auto next=reinterpret_cast<uintptr_t>(info.BaseAddress)+info.RegionSize;if(next<=address)break;address=next;
        }
    }
    return result;
}
