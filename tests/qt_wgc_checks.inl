// The selected source is an actual moving HWND painted by the owned launcher.
// No generated GPU substitution and no real consent prompt. The current desktop
// never switches; inaccessible compositor capture is reported as BLOCKED.
int wgcPhase_{-1};
bool wgcSharedReplay_{};
uint64_t wgcBeforeExport_{};
void finishWgcCheck(int result,const QString& reason){
    checkReport_.write((reason+"\n").toUtf8());checkReport_.flush();checks_.stop();
    QFile outcome(checkRoot_+"/wgc-result.txt");outcome.open(QIODevice::WriteOnly);outcome.write(QByteArray::number(result));outcome.close();exiting_=true;close();
}
void wgcCheckStep(){
    const bool longSession=qEnvironmentVariableIntValue("PANDACLIP_QT_WGC_LONG");
    if(phaseClock_.elapsed()>(longSession&&wgcPhase_==2?80000:15000)){finishWgcCheck(1,"FAIL: bounded WGC phase timed out");return;}
    switch(wgcPhase_){
    case -1: backend_->generatedSource(13);wgcPhase_++;phaseClock_.restart();return;
    case 0:{
        auto found=Scenes::discoverApplications(reinterpret_cast<quintptr>(GetThreadDesktop(GetCurrentThreadId())),GetCurrentProcessId());
        auto target=std::find_if(found.windows.begin(),found.windows.end(),[](auto& w){return w.title.contains("Owned GPU fixture");});
        if(target==found.windows.end()){finishWgcCheck(1,"FAIL: owned HWND not listed by scene application picker");return;}
        auto definition=sceneModel_->addDefinition(Scenes::Kind::Window,"Owned real WGC source",target->identity(),target->clientSize);
        sceneModel_->addVisual(definition);if(settings_)settings_->hide();
        wgcPhase_++;phaseClock_.restart();return;
    }
    case 1:
        if(!backend_->state().capture&&phaseClock_.elapsed()>700){finishWgcCheck(-1,"BLOCKED real WGC on inactive owned desktop: "+q(backend_->state().captureStatus));return;}
        if(backend_->verificationWgcFrames()<5){
            if(phaseClock_.elapsed()>6000)finishWgcCheck(-1,"BLOCKED: real WGC session yielded no moving-window frames on inactive desktop");return;
        }
        checkReport_.write(QString("REAL WGC published %1 owned GPU copies\n").arg(backend_->verificationWgcFrames()).toUtf8());
        if(longSession){
            setAttribute(Qt::WA_ShowWithoutActivating);
            auto hwnd=reinterpret_cast<HWND>(winId());
            SetWindowLongPtrW(hwnd,GWL_EXSTYLE,GetWindowLongPtrW(hwnd,GWL_EXSTYLE)|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT);
            show();
        }
        record_->click();wgcPhase_++;phaseClock_.restart();return;
    case 2:
        if(phaseClock_.elapsed()<3000)return;
        if(!backend_->state().recording||backend_->verificationEncodedFrames()<30){finishWgcCheck(1,"FAIL: real WGC preview did not reach recorder");return;}
        if(longSession){
            if(!wgcSharedReplay_){replay_->click();wgcSharedReplay_=true;check(backend_->state().recording&&backend_->state().replay,"real WGC ordinary/replay share an active session");}
            if(phaseClock_.elapsed()<65000)return;
            checkReport_.write(QString("REAL WGC long session: encoded=%1 published=%2 history=%3 s\n").arg(backend_->verificationEncodedFrames()).arg(backend_->verificationWgcFrames()).arg(backend_->state().historySeconds).toUtf8());
            save_->click();wgcBeforeExport_=backend_->verificationEncodedFrames();wgcPhase_=7;phaseClock_.restart();return;
        }
        shot(this,"workspace-real-wgc-recording");record_->click();wgcPhase_++;phaseClock_.restart();return;
    case 3:
        if(backend_->state().encoding)return;
        if(!q(backend_->state().recordStatus).startsWith("Saved")){finishWgcCheck(1,"FAIL: real WGC recording did not finalize");return;}
        replay_->click();wgcPhase_++;phaseClock_.restart();return;
    case 4:
        if(phaseClock_.elapsed()<3000||backend_->state().historySeconds<1)return;
        save_->click();wgcPhase_++;phaseClock_.restart();return;
    case 5:
        if(phaseClock_.elapsed()<500||backend_->state().pendingSaves)return;
        if(QDir(checkRoot_+"/output").entryList({"ReDot_Clip_*.mp4"},QDir::Files).isEmpty()){finishWgcCheck(1,"FAIL: real WGC replay did not save");return;}
        replay_->click();wgcPhase_++;phaseClock_.restart();return;
    case 6:
        if(backend_->state().encoding)return;
        // The active-desktop harness hides Qt popups to avoid their input grab.
        // Use the real Qt Advanced Capture button, whose normal health refresh
        // also runs while hidden. Never substitute a Win32 compatibility button.
        advancedAction_->setChecked(true);tick();
        check(capture_->isEnabled(),"Qt Advanced Capture stop is usable after encoder drain");capture_->click();
        wgcPhase_=8;phaseClock_.restart();return;
    case 7:
        if(phaseClock_.elapsed()<500||backend_->state().pendingSaves)return;
        if(!q(backend_->state().replayStatus).startsWith("Saved clip:")){finishWgcCheck(1,"FAIL real WGC shared-session export did not finalize");return;}
        check(backend_->verificationEncodedFrames()>wgcBeforeExport_&&backend_->state().recording,"real WGC encoding continues across asynchronous clip export");
        replay_->click();record_->click();wgcPhase_=3;phaseClock_.restart();return;
    case 8:
        if(phaseClock_.elapsed()<300)return;
        check(!backend_->state().capture&&!backend_->state().encoding,"actual Qt Advanced Capture action stops real WGC preview (hidden owned frontend)");
        finishWgcCheck(failures_?1:0,QString(failures_?"FAIL":"PASS")+" actual Qt controls, real moving HWND -> WGC -> canonical scene -> direct NVENC recording/replay finalization and preview Stop. Decode files separately.");return;
    }
}
