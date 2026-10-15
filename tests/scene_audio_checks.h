#pragma once
#include "scene_audio.h"
#include <QFile>
// Silent fixtures exercise production integer timelines/gain/mixing. No capture device.
void audioAndMigrationChecks(const QString& root){
    Definition desktop{id(),"Desktop",Kind::DesktopAudio,{{"deviceId","fixture-desktop"}},{640,360}};
    Definition alias{id(),"Desktop alias",Kind::DesktopAudio,{{"deviceId","fixture-desktop"}},{640,360}};
    Definition mic{id(),"Mic",Kind::Microphone,{{"deviceId","fixture-mic"}},{640,360}};
    Scene scene;scene.id=id();scene.name="Audio";Audio da{id(),desktop.id,1,0,false,false},aa{id(),alias.id,.5,0,false,false},ma{id(),mic.id,.25,0,false,false};scene.audio={da,aa,ma};Scene other=scene;other.id=id();other.name="Other";other.audio[0].muted=true;other.audio[1].gain=.25;other.audio[2].muted=true;
    Snapshot current{{scene,other},{desktop,alias,mic},scene.id};auto provider=makeVerificationAudioSession(current,[&]{return current;});auto out=std::make_shared<RecordingIngress>();std::wstring error;require(provider->start(out,error),"scene audio provider starts bounded deterministic lanes");auto origin=std::chrono::steady_clock::now();provider->origin(origin);
    auto submit=[&](uint64_t first){for(auto definition:{desktop,mic}){RecordedAudio packet;packet.source=definition.kind==Kind::Microphone?AudioSourceKind::Microphone:AudioSourceKind::Game;packet.continuousClock=true;packet.clockAnchor=origin;packet.firstFrame48k=first;packet.stereo48k.assign(2048,definition.kind==Kind::Microphone?.2f:.1f);require(submitVerificationAudio(provider,definition.id,std::move(packet)),"PCM enters physical scene input queue");}};
    std::vector<int16_t> mix;std::vector<std::vector<int16_t>> stems;submit(0);provider->render(0,1024,mix,stems);
    require(stems.size()==3&&std::abs(mix[1200]-int16_t(.15f*32767))<=1,"one physical desktop is mixed once plus mic, no duplicate alias");require(std::abs(stems[1][1200]-int16_t(.05f*32767))<=1,"separate source gain applied to stem");
    submit(1024);provider->render(1024,1024,mix,stems);require(out->audioTimelineMissing==0&&out->audioTimelineOverwrites==0&&out->lateAudioFrames==0,"packet boundaries retain exact integer audio continuity");
    current.selected=other.id;submit(2048);provider->render(2048,1024,mix,stems);require(std::abs(mix[1200]-int16_t(.025f*32767))<=1&&stems[0][1200]==0&&stems[2][1200]==0,"scene switch updates actual mix/mute/stems");require(std::abs(mix[0]-int16_t((.1f*(1.f-.75f/240)+.2f*(.25f-.25f/240))*32767))<=2,"one physical gain ramp prevents double-mix at scene switch");
    current.scenes[1].audio[1].gain=0;submit(3072);provider->render(3072,1024,mix,stems);require(mix[1200]==0,"silent/muted source mix is exactly zero without noise processing");provider->stop();
    {
        // Production WASAPI manager, deliberately nonexistent pinned endpoint.
        // No real input or speaker is opened; no Windows configuration changes.
        auto unavailable=mic;unavailable.id=id();unavailable.name="Owned unavailable microphone";
        unavailable.settings["deviceId"]="ReDot-owned-nonexistent-endpoint-7f41b927";
        Scene faultScene;faultScene.id=id();faultScene.audio={{id(),unavailable.id,1,0,false,false}};
        Snapshot faultState{{faultScene},{unavailable},faultScene.id};auto failed=makeAudioSession(faultState,[&]{return faultState;},0);auto faultOutput=std::make_shared<RecordingIngress>();
        require(failed->start(faultOutput,error),"unavailable pinned microphone starts asynchronous bounded manager");failed->origin(origin);
        std::this_thread::sleep_for(std::chrono::milliseconds(5300));failed->render(0,1024,mix,stems);failed->stop();
        auto attempts=verificationAudioOpenAttempts(failed);
        require(attempts==std::vector<unsigned>{3},"unavailable endpoint retries exactly three times and manager joins");
        {std::lock_guard lock(faultOutput->mutex);require(!faultOutput->audioError.empty()&&!faultOutput->audioSourceOpen[1]&&faultOutput->audioEndpointIds[1].empty(),"pinned endpoint failure is reported with no default microphone fallback");}
        require(stems.size()==1&&std::all_of(mix.begin(),mix.end(),[](auto v){return v==0;})&&std::all_of(stems[0].begin(),stems[0].end(),[](auto v){return v==0;}),"unavailable microphone produces correctly sized silent mix and stem");
    }
    current.scenes[1].audio[0].syncMs=25;auto conflict=makeVerificationAudioSession(current,[&]{return current;});require(!conflict->start(std::make_shared<RecordingIngress>(),error)&&!error.empty(),"incompatible shared-source sync rejected before writing");
    Definition appA{id(),"Application window A",Kind::ApplicationAudio,{{"executable","fixture.exe"},{"target","A"},{"verificationProcessId",21}},{640,360}},appB=appA;appB.id=id();appB.name="Application window B";appB.settings["target"]="B";Scene apps;apps.id=id();apps.name="Application aliases";apps.audio={{id(),appA.id,1,0,false,false},{id(),appB.id,1,0,false,false}};Snapshot appState{{apps},{appA,appB},apps.id};auto appProvider=makeVerificationAudioSession(appState,[&]{return appState;});require(appProvider->start(std::make_shared<RecordingIngress>(),error),"different windows in one process share application audio capture plan");appProvider->origin(origin);RecordedAudio appPacket;appPacket.source=AudioSourceKind::Game;appPacket.continuousClock=true;appPacket.clockAnchor=origin;appPacket.stereo48k.assign(2048,.1f);require(submitVerificationAudio(appProvider,appA.id,std::move(appPacket)),"physical process PCM enters shared application lane");appProvider->render(0,1024,mix,stems);require(stems.size()==2&&stems[0][1200]==stems[1][1200]&&mix[1200]==stems[0][1200],"application aliases retain individual stems but mix one process once");appProvider->stop();
    auto pump=[](Model& m){for(int i=0;i<300&&(!m.ready()||!m.idle());++i){QApplication::processEvents();std::this_thread::sleep_for(std::chrono::milliseconds(10));}};
    auto path=root+"/migration.json";QString existingId;int scenesAfter=0,defsAfter=0;
    {Model model(path);pump(model);existingId=model.data().selected;auto def=model.addDefinition(Kind::Text,"Existing",{{"text","Preserved"}});model.addVisual(def);auto old=encode(model.snapshot());
        model.migrateCapture({{"sourceExe","C:/Owned/game.exe"},{"sourceTitle","Owned Game"},{"gameEnabled","1"},{"gameMode","2"},{"gameGain","73"},{"micEnabled","1"},{"micId","pinned-mic"},{"micGain","129"}});require(model.data().migrationVersion==2&&model.data().selected==existingId&&model.data().scenes[0].visuals.size()==1,"migration preserves nonempty existing scene and selection");require(model.data().scenes.size()==2&&model.data().scenes[1].audio.size()==2&&model.data().scenes[1].visuals.size()==1,"migration creates separate game/desktop/mic scene");require(model.data().scenes[1].audio[0].gain==.73&&model.data().scenes[1].audio[1].gain==1.29,"migration preserves audio gains");scenesAfter=int(model.data().scenes.size());defsAfter=int(model.data().definitions.size());model.flush();pump(model);}
    {Model restored(path);pump(restored);restored.migrateCapture({{"sourceExe","Other"}});require(restored.data().migrationVersion==2&&restored.data().scenes.size()==scenesAfter&&restored.data().definitions.size()==defsAfter&&restored.data().selected==existingId,"migration version/identity survive restart and prevent repeat");}
}
