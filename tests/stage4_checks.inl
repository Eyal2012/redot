// Compiled only into quiet verification; never registers an icon in Explorer.
uint64_t stage4HiddenFrames_{};
QStringList stage4BeforeExitFiles_;
void stage4Preference(int id,bool enabled){
    properties(0);auto widget=settings_->findChild<QCheckBox*>(QString("setting%1").arg(id));
    check(widget&&widget->isVisible()&&widget->isEnabled(),"Stage 4 actual General checkbox is accessible");
    if(widget&&widget->isChecked()!=enabled)widget->click();
    check((id==2039?backend_->state().minimizeToTray:backend_->state().closeToTray)==enabled,"Stage 4 actual Qt checkbox reaches the correct preference");
    settings_->hide();
}
void stage4TrayChecks() {
    verificationTrayAvailable_=true;
    check(!backend_->state().encoding&&!backend_->state().replay,"Stage 4 startup restores scene preview without recording/replay automation");
    check(!backend_->state().minimizeToTray,"Stage 4 absent minimize preference defaults off");
    const auto neutral=tray_->icon().cacheKey();
    FrontendState state;state.taskbarMode=unsigned(Branding::Mode::Idle);
    check(trayOperation(state)=="Idle","Stage 4 tray Idle");
    state.encoding=state.replay=true;
    check(!trayOperation(state).contains("running"),"Stage 4 requested replay is not reported running before encoded frames");
    state.encoderReady=true;state.taskbarMode=unsigned(Branding::Mode::Replay);updateTray(state);
    check(trayState_->text()=="Replay running"&&tray_->toolTip().contains("Replay running"),"Stage 4 replay text from confirmed engine mode");
    state.recording=true;state.taskbarMode=unsigned(Branding::Mode::Recording);updateTray(state);
    check(trayState_->text()=="Recording and replay running","Stage 4 both confirmed operations reflected");
    state.replay=false;updateTray(state);check(trayState_->text()=="Recording","Stage 4 recording text");
    state.taskbarMode=unsigned(Branding::Mode::Idle);check(!trayOperation(state).startsWith("Recording"),"Stage 4 drain/failure does not advertise recording success");
    state.recovery=true;check(trayOperation(state)=="Recovering","Stage 4 recovery text");
    state.closing=true;check(trayOperation(state)=="Finalizing and exiting","Stage 4 shutdown text");
    check(tray_->icon().cacheKey()==neutral&&findChildren<QSystemTrayIcon*>().size()==1&&!tray_->isVisible(),"Stage 4 one neutral tray object; quiet test publishes no shell icon");
    updateTray(backend_->state());
    const auto before=normalGeometry();
    stage4Preference(2039,false);showMinimized();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    check(isVisible()&&isMinimized()&&!hiddenToTray_,"Stage 4 minimize disabled retains taskbar window");
    trayOpen_->trigger();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    check(isVisible()&&!isMinimized(),"Stage 4 tray Open restores ordinary minimized window");
    stage4Preference(2039,true);showMinimized();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    check(!isVisible()&&hiddenToTray_&&backend_->alive(),"Stage 4 minimize enabled hides without closing");
    trayOpen_->trigger();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    check(isVisible()&&!isMinimized()&&normalGeometry()==before,"Stage 4 restore retains normal geometry");
    showMaximized();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);showMinimized();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    check(hiddenToTray_,"Stage 4 maximized window minimizes to tray");trayOpen_->trigger();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    check(isMaximized()&&isVisible(),"Stage 4 restore retains maximized state");showNormal();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    stage4Preference(2038,true);QCloseEvent event;QApplication::sendEvent(this,&event);
    check(!event.isAccepted()&&!isVisible()&&hiddenToTray_&&backend_->alive(),"Stage 4 Close enabled hides without exit");
    trayOpen_->trigger();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    verificationTrayAvailable_=false;showMinimized();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    check(isVisible()&&isMinimized()&&!hiddenToTray_,"Stage 4 unavailable tray prevents unreachable minimized app");trayOpen_->trigger();
    verificationTrayAvailable_=true;showMinimized();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    verificationTrayAvailable_=false;updateTray(backend_->state());QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    check(isVisible()&&!hiddenToTray_&&backend_->alive(),"Stage 4 notification-area loss resurfaces app without shutdown");
    verificationTrayAvailable_=true;stage4Preference(2038,false);stage4Preference(2039,false);
    check(!backend_->state().closeToTray&&!backend_->state().minimizeToTray,"Stage 4 independent General preferences update immediately");
    updateTray(backend_->state());
}
void stage4SortChecks() {
    const auto actual=backend_->clips();
    auto titles=[this]{QStringList result;for(int i=0;i<mediaController_->rowCount();++i)result<<mediaController_->data(mediaController_->index(i),MediaController::TitleRole).toString();return result;};
    for(const auto& clip:actual)check(clip.bytes==quint64(QFileInfo(q(clip.path)).size()),"Stage 4 cached size equals actual file bytes: "+q(clip.name));
    for(const auto& order:QStringList{"Size (largest first)","Size (smallest first)"}) {
        auto sorted=actual;std::sort(sorted.begin(),sorted.end(),[&](const ClipInfo&a,const ClipInfo&b){return a.bytes==b.bytes?a.path<b.path:order=="Size (largest first)"?a.bytes>b.bytes:a.bytes<b.bytes;});
        QStringList expected;for(auto& c:sorted)expected<<q(c.name);mediaController_->query("","All",order);
        check(titles()==expected,"Stage 4 actual file-size ordering: "+order);
        mediaController_->query("mic","Replay",order);bool matches=true;for(auto& title:titles())matches&=title.contains("mic",Qt::CaseInsensitive)&&title.startsWith("ReDot_Clip_");
        check(matches&&titles().size()==1,"Stage 4 search and replay filter combine with "+order);
    }
    std::vector<ClipInfo> fixture(3);for(int i=0;i<3;++i){fixture[i].name=std::wstring(1,wchar_t(L'c'-i))+L".mp4";fixture[i].path=(checkRoot_+"/missing/"+q(fixture[i].name)).toStdWString();fixture[i].bytes=123;fixture[i].error=L"Unavailable fixture";}
    mediaController_->verificationClips(fixture);mediaController_->query("","All","Size (largest first)");
    check(titles()==QStringList{"a.mp4","b.mp4","c.mp4"},"Stage 4 equal-size missing files use deterministic path order");mediaController_->query("","All","Size (smallest first)");
    check(titles()==QStringList{"a.mp4","b.mp4","c.mp4"},"Stage 4 reverse size keeps equal-size tie order");
    mediaController_->open(0);check(mediaController_->status().contains("missing",Qt::CaseInsensitive)||mediaController_->status().contains("unavailable",Qt::CaseInsensitive),"Stage 4 missing sorted file fails playback safely");mediaController_->back();
    mediaController_->verificationClips(actual);mediaController_->query("","All","Newest");
}
void stage4HideEncoding() {
    verificationTrayAvailable_=true;stage4Preference(2039,true);stage4HiddenFrames_=backend_->verificationEncodedFrames();
    showMinimized();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);updateTray(backend_->state());
    check(hiddenToTray_&&!isVisible()&&backend_->state().encoding,"Stage 4 active encoding minimizes into tray");
    check(trayRecord_->text()==(backend_->state().recording?"Stop Recording":"Start Recording")&&trayReplay_->text()==(backend_->state().replay?"Disable Replay Buffer":"Enable Replay Buffer"),"Stage 4 tray commands reflect actual current actions");
    check(backend_->quietHotkeyProbe().starts_with(L"PASS"),"Stage 4 shortcut registration/conflicts preserved while hidden");
}
void stage4RestoreEncoding() {
    updateTray(backend_->state());
    check(backend_->verificationEncodedFrames()>stage4HiddenFrames_,"Stage 4 encoded frames continue while hidden");
    check(trayState_->text()==trayOperation(backend_->state()),"Stage 4 hidden actual engine status reaches tray text");
    trayOpen_->trigger();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    check(isVisible()&&!hiddenToTray_&&backend_->state().encoding,"Stage 4 tray restore preserves active encoding");stage4Preference(2039,false);
}
