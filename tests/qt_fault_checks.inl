// Verification target only. Actual visible Qt action routing with process-local
// hooks; all partial/final files live in the fresh owned profile under build.
int faultPhase_{};
uint64_t faultBeforeExport_{};
void finishFaultChecks(){
    SetEnvironmentVariableW(L"PANDACLIP_TEST_RECORD_FAIL_FRAME",nullptr);
    SetEnvironmentVariableW(L"PANDACLIP_TEST_EXPORT_FAIL_PACKET",nullptr);
    checkReport_.write(QString("RESULT failures=%1 controlled frontend writes\n").arg(failures_).toUtf8());checkReport_.flush();
    QFile result(checkRoot_+"/result.txt");result.open(QIODevice::WriteOnly);result.write(QByteArray::number(failures_));result.close();
    checks_.stop();exiting_=true;close();
}
void faultCheckStep(){
    if(phaseClock_.elapsed()>(faultPhase_==8?30000:15000)){check(false,"bounded frontend fault phase timeout "+QString::number(faultPhase_));finishFaultChecks();return;}
    auto next=[this]{++faultPhase_;phaseClock_.restart();};
    switch(faultPhase_){
    case 0:{
        if(phaseClock_.elapsed()<800)return;
        check(sceneModel_->current()&&!sceneModel_->current()->visuals.isEmpty(),"fault profile has its canonical composed fixture");
        backend_->generatedSource(8);
        SetEnvironmentVariableW(L"PANDACLIP_TEST_RECORD_FAIL_FRAME",L"15");nativeControlClick(record_);next();return;
    }
    case 1:
        if(backend_->state().encoding||phaseClock_.elapsed()<700)return;
        tick();if(!record_->isEnabled())return;
        check(status_->text().contains("Controlled recording write failure"),"recording write failure reaches visible Qt status and releases Start");
        check(!q(backend_->state().recordStatus).startsWith("Saved")&&QDir(checkRoot_+"/output").entryList({"*.mp4"},QDir::Files).isEmpty(),"failed recording is not reported Saved and no final MP4 is published");
        SetEnvironmentVariableW(L"PANDACLIP_TEST_RECORD_FAIL_FRAME",nullptr);nativeControlClick(record_);next();return;
    case 2:
        if(phaseClock_.elapsed()<1000||backend_->verificationEncodedFrames()<20)return;
        check(!status_->text().contains("Recording failed"),"retry clears stale write error while encoding");nativeControlClick(record_);next();return;
    case 3:
        if(backend_->state().encoding)return;tick();if(!record_->isEnabled())return;
        check(q(backend_->state().recordStatus).startsWith("Saved"),"recording retry finalizes through actual Qt Stop control");nativeControlClick(replay_);next();return;
    case 4:
        if(phaseClock_.elapsed()<1500||backend_->state().historySeconds<1)return;
        SetEnvironmentVariableW(L"PANDACLIP_TEST_EXPORT_FAIL_PACKET",L"1");nativeControlClick(save_);faultBeforeExport_=backend_->verificationEncodedFrames();next();return;
    case 5:
        if(phaseClock_.elapsed()<700||backend_->state().pendingSaves)return;tick();
        check(q(backend_->state().replayStatus).contains("Controlled replay write failure"),"replay write failure reaches visible frontend feedback");
        check(backend_->state().replay&&save_->isEnabled()&&backend_->verificationEncodedFrames()>faultBeforeExport_,"failed export leaves healthy replay encoding and Save responsive");
        check(QDir(checkRoot_+"/output").entryList({"ReDot_Clip_*.mp4"},QDir::Files).isEmpty(),"failed export publishes no final clip");
        SetEnvironmentVariableW(L"PANDACLIP_TEST_EXPORT_FAIL_PACKET",nullptr);nativeControlClick(save_);next();return;
    case 6:
        if(phaseClock_.elapsed()<700||backend_->state().pendingSaves)return;
        check(q(backend_->state().replayStatus).startsWith("Saved clip:"),"replay retry reports Saved only after MP4 finalization");nativeControlClick(replay_);next();return;
    case 7:
        if(backend_->state().encoding)return;
        check(QDir(checkRoot_+"/output").entryList({"*.mp4"},QDir::Files).size()==2,"exactly two successful retry files remain intact");backend_->verificationInterrupt(true);next();return;
    case 8:
        if(backend_->state().recovery)return;
        check(backend_->verificationRecoveryAttempts()==3&&q(backend_->state().captureStatus).contains("exhausted"),"scene recovery stops after three controlled attempts");next();return;
    case 9:
        if(phaseClock_.elapsed()<2500)return;
        check(!backend_->state().recovery&&backend_->verificationRecoveryAttempts()==3,"exhausted scene recovery does not restart endlessly on health ticks");backend_->verificationRecoveryFailure(false);backend_->action(1002);next();return;
    case 10:
        if(phaseClock_.elapsed()<1000)return;
        check(backend_->state().capture&&!backend_->state().recovery,"explicit preview restart recovers preserved scene after exhausted retries");nativeControlClick(record_);next();return;
    case 11:
        if(phaseClock_.elapsed()<1000||backend_->verificationEncodedFrames()<20)return;
        nativeControlClick(record_);next();return;
    case 12:
        if(backend_->state().encoding)return;
        check(q(backend_->state().recordStatus).startsWith("Saved")&&QDir(checkRoot_+"/output").entryList({"*.mp4"},QDir::Files).size()==3,"recovered canonical scene records again while preserving previous saved files");
        tick();backend_->setFolder(L"");nativeControlClick(record_);tick();
        check(q(backend_->state().recordStatus).contains("Choose an output folder")&&!backend_->state().encoding&&property("taskbarMode").toUInt()==unsigned(Branding::Mode::Idle),"rejected recording initialization never shows red");
        nativeControlClick(replay_);tick();
        check(q(backend_->state().replayStatus).contains("Choose an output folder")&&!backend_->state().encoding&&!backend_->state().replay&&property("taskbarMode").toUInt()==unsigned(Branding::Mode::Idle),"rejected replay initialization never shows green");
        backend_->setFolder((checkRoot_+"/output").toStdWString());finishFaultChecks();return;
    }
}
