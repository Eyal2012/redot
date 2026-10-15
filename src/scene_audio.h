#pragma once
#include "recording.h"
#include "scene_model.h"
#include <functional>
namespace Scenes {
std::shared_ptr<SceneAudioProvider> makeAudioSession(const Snapshot& initial,std::function<Snapshot()> current,quintptr desktop);
#ifdef REDOT_TEST_FIXTURE
// Deterministic PCM enters the production scene timelines/mixer, never WASAPI.
std::shared_ptr<SceneAudioProvider> makeVerificationAudioSession(const Snapshot&,std::function<Snapshot()>);
bool submitVerificationAudio(const std::shared_ptr<SceneAudioProvider>&,const QString&,RecordedAudio);
// Read only after stop() has joined the manager; no live worker-state race.
std::vector<unsigned> verificationAudioOpenAttempts(const std::shared_ptr<SceneAudioProvider>&);
#endif
}
