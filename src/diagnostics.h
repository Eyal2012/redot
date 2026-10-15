#pragma once
#include "recording.h"
#include "logging.h"
#include <psapi.h>
#include <sstream>
#include <iomanip>

inline std::wstring pipelineReport(const std::shared_ptr<RecordingIngress>& ingress,
    const ReplayBuffer& replay, uint64_t elapsed, bool preview, const std::wstring& state,
    uint64_t failures, const std::deque<std::wstring>& errors) {
    std::wostringstream out;
    out << std::fixed << std::setprecision(3);
    out << L"ReDot diagnostics (synthetic/idle measurements are not gameplay benchmarks)\r\n";
    out << L"State: " << state << L"; preview rendering: " << (preview ? L"visible" : L"hidden") << L"\r\n";
    out << L"Session output duration: " << elapsed / 1000.0 << L" s; " << (ingress ? ingress->video.description() : L"no active encoder") << L" SDR, direct NVENC\r\n";
    if (ingress) {
        std::lock_guard lock(ingress->mutex);
        const double span = std::chrono::duration<double>(ingress->lastSourceAt - ingress->firstSourceAt).count();
        const auto frames = ingress->encodedFrames.load();
        const double availableSpan = std::chrono::duration<double>(ingress->lastAvailableAt - ingress->firstAvailableAt).count();
        out << L"WGC available before GPU copy: " << ingress->availableFrames.load() << L"; cadence: " << (availableSpan > 0 ? (ingress->availableFrames.load() - 1) / availableSpan : 0) << L" fps; largest gap: " << ingress->maxAvailableGapMicros.load() / 1000.0 << L" ms; GPU pool drops: " << ingress->gpuPoolDrops.load() << L"\r\n";
        out << L"Accepted GPU images: " << ingress->sourceFrames.load() << L" (cached startup images: " << ingress->startupSnapshots.load() << L"); cadence: " << (span > 0 ? (ingress->sourceFrames.load() - 1) / span : 0) << L" fps. Fresh WGC availability is above.\r\n";
        out << L"WGC largest timestamp gap: " << ingress->maxSourceGapMicros.load() / 1000.0 << L" ms; largest callback delivery delay: " << ingress->maxCaptureDeliveryMicros.load() / 1000.0 << L" ms\r\n";
        out << L"CFR frames: " << frames << L"; repeated: " << ingress->repeatedFrames.load() << L"; late/replaced: " << ingress->replacedFrames.load() << L"; dropped: " << ingress->queueDrops.load() << L"\r\n";
        out << L"Encoder+GPU+mux call avg/max: " << (frames ? ingress->totalEncodeMicros.load() / static_cast<double>(frames) / 1000 : 0) << L"/" << ingress->maxEncodeMicros.load() / 1000.0 << L" ms; calls over one configured frame interval: " << ingress->slowEncodeFrames.load() << L"\r\n";
        out << L"Queue video now/high/cap: " << ingress->frames.size() << L"/" << ingress->videoQueueHighWater.load() << L"/" << RecordingIngress::Capacity << L"; audio: " << ingress->audio.size() << L"/" << ingress->audioQueueHighWater.load() << L"/" << RecordingIngress::AudioCapacity << L"\r\n";
        out << L"Audio queue loss: " << ingress->audioQueueDrops.load() << L"; late PCM samples: " << ingress->lateAudioFrames.load() << L"; audio/video timestamp fallbacks: " << ingress->timestampFallbacks.load() << L"/" << ingress->captureTimestampFallbacks.load() << L"\r\n";
        out << L"Encoder source views created/reused: " << ingress->encoderViewsCreated.load() << L"/" << ingress->encoderViewsReused.load() << L" (canonical scene frames reuse their leased GPU view)\r\n";
        out << L"Game/mic rate correction: " << ingress->gameDriftPpm.load() << L"/" << ingress->microphoneDriftPpm.load() << L" ppm; audio failures/retry attempts: " << ingress->audioFailures.load() << L"/" << ingress->audioReconnects.load() << L"\r\n";
        out << L"Audio timeline overwritten frames/missing enabled-source slots/mix clipped frames: " << ingress->audioTimelineOverwrites.load() << L"/" << ingress->audioTimelineMissing.load() << L"/" << ingress->audioMixClipped.load() << L" (missing includes startup, device silence recovery and tail padding)\r\n";
        for(size_t s=0;s<2;++s)out << (s?L"Mic":L"Game") << L" discontinuity flags/missing native frames/duplicate native frames/clock resets/over-range input frames: " << ingress->audioDiscontinuities[s].load()<<L"/"<<ingress->audioMissingInputFrames[s].load()<<L"/"<<ingress->audioDuplicateInputFrames[s].load()<<L"/"<<ingress->audioTimestampJumps[s].load()<<L"/"<<ingress->audioSourceClipping[s].load()<<L"\r\n";
        if (!ingress->audioError.empty()) out << L"Audio status: " << ingress->audioError << L"\r\n";
        for (size_t source = 0; source < 2; ++source) {
            out << (source ? L"Microphone" : L"Game") << L" input: endpoint=" << ingress->audioEndpointIds[source]
                << L"; name=" << ingress->audioEndpointNames[source] << L"; open=" << ingress->audioSourceOpen[source]
                << L"; Windows mute/volume=" << ingress->audioEndpointMuted[source] << L"/" << ingress->audioEndpointVolume[source]
                << L"; packets=" << ingress->audioPackets[source].load() << L"; input frames=" << ingress->audioInputFrames[source].load()
                << L"; silent-flag frames=" << ingress->audioSilentFrames[source].load() << L"; nonzero resampled frames=" << ingress->audioNonzeroFrames[source].load() << L"\r\n";
            const auto meter=ingress->rawMeters[source].snapshot(GetTickCount64());
            out<<L"Raw PCM meter RMS/peak="<<std::scientific<<meter.rms<<L"/"<<meter.peak<<std::fixed<<L"; sample frames="<<meter.frames<<L"; meter packets="<<meter.packets<<L" (before ReDot gain/mute)\r\n";
        }
    }
    out << L"Replay history: " << replay.availableSeconds() << L" s; accounted packet RAM/cap: " << replay.memoryBytes() / 1048576.0 << L"/" << replay.memoryLimitBytes() / 1048576.0 << L" MiB; pending exports: " << replay.pendingSaves() << L"\r\n";
    out << L"Exports successful/failed: " << replay.exportsCompleted.load() << L"/" << replay.exportFailures.load() << L"; last/max export time: " << replay.lastExportMilliseconds.load() << L"/" << replay.maxExportMilliseconds.load() << L" ms\r\n";
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
        out << L"Process private bytes/working set: " << memory.PrivateUsage / 1048576.0 << L"/" << memory.WorkingSetSize / 1048576.0 << L" MiB\r\n";
    out << L"Lifecycle/recording failures: " << failures << L"\r\n";
    const auto logger = redot_logging::stats();
    out << L"Diagnostic log queue pending/cap: " << logger.pending << L"/" << redot_logging::Writer::Capacity
        << L"; dropped messages/write errors: " << logger.dropped << L"/" << logger.errors << L"\r\n";
    for (const auto& error : errors) out << L"  " << error << L"\r\n";
    out << L"Timestamps establish clock continuity; flash/beep and listening checks establish perceptual alignment.\r\n";
    return out.str();
}
