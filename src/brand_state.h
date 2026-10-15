#pragma once
#include <cstdint>
namespace Branding {
enum class Mode : unsigned { Idle, Replay, Recording };
// Requested flags alone never advertise success. A completed encoded frame,
// running worker, and non-stopping ingress are all required.
constexpr Mode engineMode(bool ready,bool stopping,uint64_t frames,bool replayOnly,bool replayEnabled){
    if(!ready||stopping||!frames)return Mode::Idle;
    if(!replayOnly)return Mode::Recording;
    return replayEnabled?Mode::Replay:Mode::Idle;
}
}
