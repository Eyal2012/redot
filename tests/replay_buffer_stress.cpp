#include "replay.h"
#include <iostream>
#include <chrono>

namespace {
bool check(bool condition, const wchar_t* message) {
    if (!condition) std::wcerr << L"FAIL: " << message << L"\n";
    return condition;
}
}

int wmain() {
    constexpr uint64_t cap = 128ull * 1024 * 1024;
    ReplayBuffer buffer(cap);
    buffer.setEnabled(true);
    buffer.reset({}, {}, false, false);

    // Stress byte eviction before reaching the 120-second age limit.
    for (uint64_t i = 0; i < 100; ++i) {
        ReplayPacket packet; packet.track = ReplayTrack::Video;
        packet.pts = packet.dts = static_cast<int64_t>(i) * 10'000'000;
        packet.duration = 10'000'000; packet.keyframe = i % 4 == 0;
        packet.bytes.resize(2 * 1024 * 1024, static_cast<uint8_t>(i));
        buffer.push(std::move(packet));
    }
    bool ok = check(buffer.memoryBytes() <= cap, L"byte cap is never exceeded during high-bitrate packet insertion") &&
        check(buffer.packetCount() < 100, L"byte pressure evicts compressed packets") &&
        check(buffer.availableSeconds() < 120.0, L"byte pressure may shorten available history");

    // With small packets, time eviction should keep the newest two-minute window.
    buffer.reset({}, {}, false, false);
    for (uint64_t i = 0; i < 200; ++i) {
        ReplayPacket packet; packet.track = ReplayTrack::Video;
        packet.pts = packet.dts = static_cast<int64_t>(i) * 10'000'000;
        packet.duration = 10'000'000; packet.keyframe = i % 2 == 0;
        packet.bytes.resize(32 * 1024, static_cast<uint8_t>(i));
        buffer.push(std::move(packet));
    }
    ok = check(buffer.memoryBytes() <= cap, L"duration eviction remains under the byte cap") &&
        check(buffer.availableSeconds() <= 120.0 && buffer.availableSeconds() >= 118.0, L"duration cap retains at most 120 seconds") &&
        check(buffer.packetCount() < 200, L"duration pressure evicts old packets") && ok;

    ok = check(buffer.reserveSave() && buffer.reserveSave() && !buffer.reserveSave(), L"at most two saves can reserve snapshot memory") && ok;
    buffer.setMemoryLimitBytes(256ull * 1024 * 1024);
    ok = check(buffer.memoryLimitBytes() == cap, L"cap cannot change underneath retained export reservations") && ok;
    buffer.finishSave(0); buffer.finishSave(0);
    ok = check(buffer.pendingSaves() == 0, L"save slots are released") && ok;
    buffer.reserveSave();
    ReplaySnapshot snapshot; std::wstring error;
    ok = check(buffer.snapshot(60, snapshot, error), L"a 60-second snapshot is available after warm-up") && ok;
    if (ok) {
        ok = check(!snapshot.packets.empty() && snapshot.packets.front()->track == ReplayTrack::Video && snapshot.packets.front()->keyframe,
            L"snapshot begins with a keyframe") && ok;
        ok = check(snapshot.durationSeconds <= 60.0 && snapshot.durationSeconds > 58.0, L"snapshot duration is keyframe aligned") && ok;
    }
    if (ok) {
        const auto firstPacket = snapshot.packets.front();
        buffer.clearHistory();
        ok = check(!firstPacket->bytes.empty() && buffer.memoryBytes() >= snapshot.bytes, L"eviction keeps shared snapshots alive and accounted") && ok;
        buffer.finishSave(snapshot.bytes);
        ok = check(buffer.memoryBytes() == 0 && buffer.pendingSaves() == 0, L"completed export releases retained memory and slots") && ok;
    }
    for (unsigned seconds : {5u, 15u, 30u, 37u, 60u, 120u, 300u, 600u}) {
        buffer.setHistorySeconds(seconds); buffer.reset({}, {}, false, false);
        for (unsigned i = 0; i < seconds + 10; ++i) {
            ReplayPacket p; p.track=ReplayTrack::Video; p.pts=p.dts=int64_t(i)*10'000'000;
            p.duration=10'000'000;p.keyframe=i%2==0;p.bytes.resize(1024);buffer.push(std::move(p));
        }
        ok = check(buffer.availableSeconds() <= seconds && buffer.availableSeconds() >= seconds-2, L"custom/preset history bounds") && ok;
        buffer.reserveSave();ReplaySnapshot durationSnapshot;std::wstring durationError;
        bool got=buffer.snapshot(seconds,durationSnapshot,durationError);
        ok=check(got && durationSnapshot.durationSeconds<=seconds && durationSnapshot.durationSeconds>seconds-2,L"custom/preset IDR snapshot")&&ok;
        buffer.finishSave(got?durationSnapshot.bytes:0);
    }
    if (ok) std::wcout << L"PASS: byte and duration caps, eviction, keyframe snapshot, and clip duration.\n";
    // A moved vector owns capacity even when its compressed payload is short.
    buffer.setHistorySeconds(600);buffer.clearHistory();
    for(unsigned i=0;i<24;++i){ReplayPacket p;p.track=ReplayTrack::Video;p.pts=p.dts=int64_t(i)*10'000'000;p.duration=10'000'000;p.keyframe=true;p.bytes.reserve(8*1024*1024);p.bytes.resize(1);buffer.push(std::move(p));}
    ok=check(buffer.packetCount()<=15&&buffer.memoryBytes()<=cap,L"spare vector capacity is charged and evicted under the real RAM budget")&&ok;
    buffer.clearHistory();ok=check(buffer.memoryBytes()==0,L"capacity-accounted history clears without phantom retained bytes")&&ok;
#ifdef REDOT_REPLAY_TEST_ALLOC_FAILURE
    {ReplayPacket p;p.track=ReplayTrack::Video;p.keyframe=true;p.duration=10'000'000;p.bytes.resize(1024);buffer.push(std::move(p));}
    const auto beforeBytes=buffer.memoryBytes(),beforeCount=buffer.packetCount();bool rejected=false;
    SetEnvironmentVariableW(L"PANDACLIP_TEST_REPLAY_INSERT_ALLOCATION",L"1");
    try{ReplayPacket p;p.track=ReplayTrack::Video;p.pts=p.dts=10'000'000;p.bytes.resize(1024);buffer.push(std::move(p));}catch(const std::bad_alloc&){rejected=true;}
    SetEnvironmentVariableW(L"PANDACLIP_TEST_REPLAY_INSERT_ALLOCATION",nullptr);
    ok=check(rejected&&buffer.memoryBytes()==beforeBytes&&buffer.packetCount()==beforeCount,L"insertion allocation failure leaves replay accounting/history unchanged")&&ok;buffer.clearHistory();
#endif
    // Two logical hours at 60 video + 2*45 AAC-like packets/s. This is an
    // accelerated compressed-buffer test, not two hours of WGC/NVENC operation.
    buffer.setHistorySeconds(120);const auto start=std::chrono::steady_clock::now();uint64_t pushes=0,saves=0,peak=0;
    ReplaySnapshot retained;uint64_t heldBytes=0;
    for(uint64_t f=0;f<7200*60;++f){
        ReplayPacket p;p.track=ReplayTrack::Video;p.pts=p.dts=int64_t(f)*10'000'000/60;p.duration=10'000'000/60;p.keyframe=f%120==0;p.bytes.resize(4096);buffer.push(std::move(p));++pushes;
        if(f%4!=0)for(auto track:{ReplayTrack::Game,ReplayTrack::Microphone}){ReplayPacket a;a.track=track;a.pts=a.dts=int64_t(f)*10'000'000/60;a.duration=213333;a.bytes.resize(512);buffer.push(std::move(a));++pushes;}
        if(f%600==300&&f>120){if(heldBytes){retained={};buffer.finishSave(heldBytes);heldBytes=0;}if(buffer.reserveSave()){std::wstring failure;if(buffer.snapshot(60,retained,failure)){heldBytes=retained.bytes;++saves;}else buffer.finishSave(0);}}
        peak=std::max(peak,buffer.memoryBytes());
        if(buffer.memoryBytes()>cap||buffer.pendingSaves()>2){ok=false;break;}
    }
    if(heldBytes){retained={};buffer.finishSave(heldBytes);}buffer.clearHistory();
    ok=check(buffer.memoryBytes()==0&&buffer.pendingSaves()==0&&saves>700,L"two logical hours with retained snapshots stay bounded and release all history")&&ok;
    std::wcout<<L"logical_seconds=7200 packet_pushes="<<pushes<<L" snapshots="<<saves<<L" peak_charged_bytes="<<peak<<L" wall_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<L"\n";
    return ok ? 0 : 1;
}
