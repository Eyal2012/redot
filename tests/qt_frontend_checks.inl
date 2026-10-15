bool checkedSharedReturn_{};
int previousBrandMode_{-1};
void verifyBrand(const FrontendState& state){
    if(checkRoot_.isEmpty()||previousBrandMode_==int(state.taskbarMode))return;
    previousBrandMode_=int(state.taskbarMode);QFile trace(checkRoot_+"/branding-transitions.txt");trace.open(QIODevice::WriteOnly|QIODevice::Append);
    auto hwnd=reinterpret_cast<HWND>(effectiveWinId());bool valid=property("taskbarMode").toUInt()==state.taskbarMode&&SendMessageW(hwnd,WM_GETICON,ICON_SMALL,0)&&SendMessageW(hwnd,WM_GETICON,ICON_BIG,0);
    trace.write(QString("mode=%1 encoding=%2 ready=%3 replay=%4 encoded=%5 native=%6 updates=%7\n").arg(state.taskbarMode).arg(state.encoding).arg(state.encoderReady).arg(state.replay).arg(backend_->verificationEncodedFrames()).arg(valid).arg(property("taskbarIconUpdates").toULongLong()).toUtf8());
    check(valid,"actual engine state installs native ReDot taskbar icon");
}
// Included only in the owned verification build. No global input or activation.
QString checkRoot_;
#include "dashboard_checks.inl"
#include "qt_wgc_checks.inl"
#include "qt_fault_checks.inl"
#include "stage4_checks.inl"
QFile checkReport_;
int failures_{}, checkPhase_{};
int checkedMicSelection_{};
int consentCheck_{};
AudioMeterSnapshot appliedMicMeter_,appliedGameMeter_;
bool appliedMicOpen_{},appliedGameOpen_{};
bool visualChecksStarted_{};
bool brightPresented_{};
quint64 capturePolicyGeneration_{},mediaPolicyGeneration_{};
quint64 captureNativeApplies_{},mediaNativeApplies_{};
QImage workspacePattern_;
QMenu* inspectedMenu_{};
QtRenderProbe* workspaceProbe_{};
QList<QPoint> materialPoints_;
QElapsedTimer phaseClock_;
int hiddenHoldMs()const{return std::clamp(qEnvironmentVariableIntValue("PANDACLIP_TEST_HIDDEN_HOLD_SECONDS"),0,600)*1000;}
int fixtureSeconds(int ordinary)const{return hiddenHoldMs()?ordinary+hiddenHoldMs()/1000*3+30:ordinary;}
int hiddenSampleSecond_{-1},hiddenSamplePhase_{-1};
void sampleExtendedHidden(){
    if(!hiddenHoldMs()||(checkPhase_!=11&&checkPhase_!=14&&checkPhase_!=17))return;
    auto second=int(phaseClock_.elapsed()/1000);if(second==hiddenSampleSecond_&&checkPhase_==hiddenSamplePhase_)return;
    hiddenSampleSecond_=second;hiddenSamplePhase_=checkPhase_;
    PROCESS_MEMORY_COUNTERS_EX m{sizeof(m)};GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m));DWORD handles{};GetProcessHandleCount(GetCurrentProcess(),&handles);
    QFile csv(checkRoot_+"/hidden-live.csv");bool first=!csv.exists();csv.open(QIODevice::WriteOnly|QIODevice::Append);
    if(first)csv.write("phase,seconds,private_bytes,working_set_bytes,commit_bytes,handles,encoded,pending,history_seconds,visible,encoding,recording,replay\n");auto s=backend_->state();
    csv.write(QString("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12,%13\n").arg(checkPhase_).arg(second).arg(m.PrivateUsage).arg(m.WorkingSetSize).arg(m.PagefileUsage).arg(handles).arg(backend_->verificationEncodedFrames()).arg(s.pendingSaves).arg(s.historySeconds).arg(isVisible()).arg(s.encoding).arg(s.recording).arg(s.replay).toUtf8());
    if(second&&second%60==0&&s.replay&&traySave_->isEnabled())traySave_->trigger();
}
QTimer checks_;
QtRenderProbe *renderProbe_{};
QTimer renderDrive_;
QByteArray checkedLayout_;
void check(bool ok, const QString &name)
{
    QByteArray line = (QString(ok ? "PASS " : "FAIL ") + name + "\n").toUtf8();
    checkReport_.write(line);
    checkReport_.flush();
    if (!ok)
        ++failures_;
}
void captionCheck(HWND hwnd,const QString& name){
    RECT window{},client{};GetWindowRect(hwnd,&window);GetClientRect(hwnd,&client);POINT origin{};ClientToScreen(hwnd,&origin);
    checkReport_.write(QString("CAPTION %1 client origin delta=%2,%3; window=%4x%5 client=%6x%7\n").arg(name).arg(origin.x-window.left).arg(origin.y-window.top).arg(window.right-window.left).arg(window.bottom-window.top).arg(client.right).arg(client.bottom).toUtf8());
    check(origin.x==window.left&&origin.y==window.top&&client.bottom==window.bottom-window.top,"native caption removed; custom title is full client area "+name);
    if(name=="workspace"||name=="settings"||name=="diagnostics"){
        QWidget* owner=name=="workspace"?static_cast<QWidget*>(this):name=="settings"?static_cast<QWidget*>(settings_):static_cast<QWidget*>(diagnostics_);
        auto logicalOrigin=owner->mapToGlobal(QPoint());const auto scale=owner->devicePixelRatioF();
        checkReport_.write(QString("QT/NATIVE %1 logical widget=%2x%3 scaled=%4x%5 physical client=%6x%7\n").arg(name).arg(owner->width()).arg(owner->height()).arg(owner->width()*scale).arg(owner->height()*scale).arg(client.right).arg(client.bottom).toUtf8());
        check(std::abs(client.right-owner->width()*scale)<2&&std::abs(client.bottom-owner->height()*scale)<2,"Qt and native client geometry agree without stale caption margins "+name);
        Q_UNUSED(logicalOrigin);
    }
}
void shot(QWidget *window, const QString &name)
{
    auto screen = window->grab();
    if (window == this)
    {
        std::vector<uint32_t> pixels;
        unsigned width{}, height{};
        if (backend_->previewReadback(pixels, width, height))
        {
            QImage preview(reinterpret_cast<const uchar *>(pixels.data()), width, height, QImage::Format_RGB32);
            QPainter painter(&screen);
            auto origin = canvas_->mapTo(this, QPoint(0, 0));
            if(!canvas_->mask().isEmpty())painter.setClipRegion(canvas_->mask().translated(origin));
            painter.drawImage(QRect(origin, canvas_->size()), preview);
        }
    }
    check(screen.save(checkRoot_ + "/screenshots/" + name + ".png"), "screenshot " + name);
}
void mediaShot(const QString &name)
{
    auto image = media_->grabWindow();
    check(!image.isNull() && image.save(checkRoot_ + "/screenshots/" + name + ".png"), "GPU screenshot " + name);
    if(!image.isNull()) {
        double d=media_->devicePixelRatio();
        image.copy(QRect(24*d,16*d,430*d,145*d)).save(checkRoot_+"/screenshots/"+name+"-detail.png");
        if(name.startsWith("playback")) {
            auto bar=media_->rootObject()->findChild<QQuickItem*>("playbackGlass");
            image.copy(QRect((bar->x()-8)*d,(bar->y()-8)*d,480*d,(bar->height()+24)*d)).save(checkRoot_+"/screenshots/"+name+"-detail.png");
        }
        if(name=="media-popover")
            image.copy(QRect(70*d,135*d,235*d,250*d)).save(checkRoot_+"/screenshots/media-popover-detail.png");
        if(name=="playback") {
            auto pixel=image.pixelColor(image.width()/2,image.height()/2);
            check(pixel.red()+pixel.green()+pixel.blue()>100,"actual decoded video pixels in Qt scene screenshot");
        }
    }
}
void scrollChecks(const QString &mode)
{
    properties(5);
    settings_->resize(710, 490);
    auto area = settings_->findChild<QScrollArea *>("settingsScroll5");
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    auto bar = area->verticalScrollBar();
    check(bar->maximum() > 0 && bar->pageStep() > 0, "scroll range/page " + mode);
    auto combo = settings_->findChild<QComboBox *>("setting2030");
    // Numeric and choice controls deliberately forward wheel input to the viewport.
    auto edit = settings_->findChild<QKeySequenceEdit *>();
    auto stamp = QFileInfo(checkRoot_ + "/local/PandaClip/settings-v1.txt").lastModified();
    auto before = edit ? edit->keySequence() : QKeySequence();
    std::vector<double> timings;
    for (int i = 0; i < 100; ++i)
    {
        bar->setValue(0);
        QElapsedTimer clock;
        clock.start();
        QWheelEvent wheel(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QApplication::sendEvent(edit ? static_cast<QObject *>(edit) : area->viewport(), &wheel);
        timings.push_back(clock.nsecsElapsed() / 1000000.0);
    }
    std::sort(timings.begin(), timings.end());
    checkReport_.write(QString("MEASURE %1 wheel median=%2 p95=%3 max=%4 ms\n")
                           .arg(mode)
                           .arg(timings[50], 0, 'f', 4)
                           .arg(timings[95], 0, 'f', 4)
                           .arg(timings.back(), 0, 'f', 4)
                           .toUtf8());
    check(bar->value() > 0, "wheel over editor moves viewport " + mode);
    check(!edit || edit->keySequence() == before, "wheel leaves shortcut unchanged " + mode);
    (void)combo;
    bar->setSliderPosition(bar->maximum());
    check(bar->value() == bar->maximum(), "scrollbar slider reaches bottom " + mode);
    QKeyEvent home(QEvent::KeyPress, Qt::Key_Home, Qt::NoModifier);
    QApplication::sendEvent(bar, &home);
    check(bar->value() == 0, "keyboard Home " + mode);
    QKeyEvent end(QEvent::KeyPress, Qt::Key_End, Qt::NoModifier);
    QApplication::sendEvent(bar, &end);
    check(bar->value() == bar->maximum(), "keyboard End " + mode);
    for (int category = 0; category < 7; ++category)
    {
        settings_->category(category);
        auto scroll = settings_->findChild<QScrollArea *>(QString("settingsScroll%1").arg(category));
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        check(scroll->verticalScrollBar()->pageStep() > 0,
              "category reachable " + QString::number(category) + " " + mode);
    }
    properties(2);
    auto recordingArea = settings_->findChild<QScrollArea *>("settingsScroll2");
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    auto recordingBar = recordingArea->verticalScrollBar();
    recordingBar->setValue(0);
    int selected = combo->currentIndex();
    QWheelEvent comboWheel(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                           Qt::NoScrollPhase, false);
    QApplication::sendEvent(combo, &comboWheel);
    check(recordingBar->value() > 0 && combo->currentIndex() == selected,
          "wheel over dropdown scrolls without changing value " + mode);
    auto numeric = settings_->findChild<QLineEdit *>("setting2020");
    auto value = numeric->text();
    recordingBar->setValue(0);
    QWheelEvent editWheel(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
    QApplication::sendEvent(numeric, &editWheel);
    check(recordingBar->value() > 0 && numeric->text() == value,
          "wheel over numeric input scrolls without changing value " + mode);
    check(QFileInfo(checkRoot_ + "/local/PandaClip/settings-v1.txt").lastModified() == stamp,
          "scrolling does not write settings " + mode);
    settings_->resize(900, 700);
}
void nextCheckPhase()
{
    ++checkPhase_;
    phaseClock_.restart();
}
void beginChecks()
{
    if(!sceneModel_->ready()){QTimer::singleShot(150,this,[this]{beginChecks();});return;}
    checkRoot_ = qEnvironmentVariable("PANDACLIP_QT_TEST_ROOT");
    QDir().mkpath(checkRoot_ + "/screenshots");
    checkReport_.setFileName(checkRoot_ + (qEnvironmentVariableIntValue("PANDACLIP_QT_RESTORE_ONLY")
                                               ? "/restore-checks.txt"
                                               : "/checks.txt"));
    checkReport_.open(QIODevice::WriteOnly | QIODevice::Text);
    if(qEnvironmentVariableIntValue("PANDACLIP_QT_REAL_WGC"))
        checkReport_.write("Owned moving HWND -> real WGC, no generated video substitution. No game audio, speaker stimuli, physical input or personal session control.\n");
    else
        checkReport_.write("Qt frontend on inactive owned desktop. Synthetic GPU/PCM recording and quiet WASAPI mic-open check; no real WGC/game audio, speaker "
                           "audio, physical input or personal session control.\n");
    if (qEnvironmentVariableIntValue("PANDACLIP_QT_RESTORE_ONLY"))
    {
        check(dockWidgetArea(sourceDock_) == Qt::RightDockWidgetArea,
              "dock arrangement restored across process launches");
        check(mixerDock_->isFloating(), "floating mixer restored across process launches");
        check(backend_->control(2020).text == L"1280", "recording settings retained across launch");
        check(backend_->state().minimizeToTray,"Stage 4 minimize preference persisted across process launches");
        if(auto expected=qEnvironmentVariable("REDOT_EXPECT_RECORD_FORMAT");!expected.isEmpty()){
            const auto format=backend_->control(2040);
            check(format.kind==FrontendControlKind::Choice&&format.choices.size()==2&&format.choices[0].rfind(L"MKV",0)==0&&format.choices[1].rfind(L"MP4",0)==0,"Recording format offers MKV (recommended) first and MP4");
            check(format.selection==expected.toInt(),"Recording format selection after launch matches the owned profile (absent/invalid -> MKV, explicit MP4 persists)");
            checkReport_.write(QString("DETAIL recording format selection=%1 expected=%2\n").arg(format.selection).arg(expected).toUtf8());
        }
        check(backend_->state().closeToTray,"Stage 4 Close preference persisted independently across process launches");
        check(!backend_->state().encoding&&!backend_->state().replay,"Stage 4 restored preferences do not auto-start recording or replay");
        check(!backend_->control(2072).checked&&!backend_->control(2073).checked,"reduced transparency/motion retained across launch");
        QFile result(checkRoot_ + "/restore-result.txt");
        result.open(QIODevice::WriteOnly);
        result.write(QByteArray::number(failures_));
        result.close();
        QTimer::singleShot(700, this, [this] {
            backend_->edit(2038,L"",0,false);
            verificationTrayAvailable_=true;
            close();
            check(exiting_&&backend_->state().closing&&!hiddenToTray_,"Stage 4 Close disabled requests real drain instead of hiding");
            checkReport_.flush();QFile result(checkRoot_+"/restore-result.txt");result.open(QIODevice::WriteOnly);result.write(QByteArray::number(failures_));
        });
        return;
    }
    backend_->setFolder((checkRoot_ + "/output").toStdWString());
    if(qEnvironmentVariableIntValue("PANDACLIP_QT_REAL_WGC")){
        checkReport_.write("This separate run overrides the fixture scope above: real moving HWND/WGC, generated-source substitution disabled; border policy simulated denied, no Windows consent request.\n");
        phaseClock_.start();checks_.setInterval(150);QObject::connect(&checks_,&QTimer::timeout,this,[this]{wgcCheckStep();});checks_.start();return;
    }
    // The fixture is a visual source in the production scene model. Its frames
    // enter Runtime, never the encoder directly. Saved definitions remain real.
    auto source=sceneModel_->addDefinition(Scenes::Kind::Game,"Owned GPU scene source",{{"target","Unavailable verification window"},{"executable","__never_live__"},{"verificationFixture",true}},{640,360});sceneModel_->addVisual(source);
    for(auto kind:{Scenes::Kind::DesktopAudio,Scenes::Kind::Microphone})sceneModel_->addDefinition(kind,kind==Scenes::Kind::Microphone?"Fixture microphone":"Fixture desktop",{{"deviceId",kind==Scenes::Kind::Microphone?"fixture-mic":"fixture-desktop"},{"verificationFixture",true}});
    backend_->generatedSource(3);
    if(qEnvironmentVariableIntValue("PANDACLIP_QT_FAULT_ONLY")){
        checkReport_.write("Controlled write failures in this owned process/output only. Generated GPU input; no WGC or audible input.\n");
        phaseClock_.start();checks_.setInterval(150);QObject::connect(&checks_,&QTimer::timeout,this,[this]{faultCheckStep();});checks_.start();return;
    }
    stage4TrayChecks();
    qApp->setProperty("verificationBackdropAlpha",true);
    checkReport_.write("Foreground-alpha fixture enabled; inactive-desktop composition unavailable. Desktop transmission NOT VERIFIED.\n");
    checkPhase_=-9;
    phaseClock_.start();
    checks_.setInterval(150);
    QObject::connect(&checks_, &QTimer::timeout, this, [this] { checkStep(); });
    checks_.start();
}
void checkStep()
{
    sampleExtendedHidden();
    const bool extendedHidden=checkPhase_==11||checkPhase_==14||checkPhase_==17;
    if (phaseClock_.elapsed() > 20000+(extendedHidden?hiddenHoldMs():0))
    {
        check(false, "phase timeout " + QString::number(checkPhase_));
        checks_.stop();
        exiting_ = true;
        close();
        return;
    }
    switch (checkPhase_)
    {
    case -9: case -8: case -7: case -6: case -5: case -4: {
        if(phaseClock_.elapsed()<1300)return;
        if(checkPhase_==-9){
            check(navigationDock_->isVisible()&&!sourceDock_->isVisible()&&!advanced_,"dashboard is the default, no empty Sources dock");
            check(!findChild<QQuickWidget*>("workspaceGlassLayer")->testAttribute(Qt::WA_NativeWindow),"native preview does not force the GPU material into a native widget");
            shot(this,"dashboard-empty");nativeControlClick(sourceSelector_,QPoint(sourceSelector_->width()-18,sourceSelector_->height()/2));
            check(QApplication::activePopupWidget()!=nullptr,"scene selector arrow opens the scene menu");if(auto popup=QApplication::activePopupWidget())popup->hide();
            nativeControlClick(quickGears_[0]);nextCheckPhase();return;
        }
        int kind=checkPhase_+8;quickPopoverCheck(kind);if(kind==4)dashboardSmallChecks();
        if(kind<4)QTimer::singleShot(200,this,[this,kind]{nativeControlClick(quickGears_[kind+1]);});
        nextCheckPhase();return;
    }
    case -3: {
        if(phaseClock_.elapsed()<800)return;
        if(qEnvironmentVariableIntValue("PANDACLIP_QT_DASHBOARD_ONLY")){
            captionCheck(reinterpret_cast<HWND>(winId()),"workspace");shot(this,"dashboard-scaled");
            properties(2);captionCheck(reinterpret_cast<HWND>(settings_->winId()),"settings");shot(settings_,"settings-scaled");settings_->hide();
            diagnostics();captionCheck(reinterpret_cast<HWND>(diagnostics_->winId()),"diagnostics");shot(diagnostics_,"diagnostics-scaled");diagnostics_->hide();
            checkReport_.write(QString("RESULT failures=%1 DPI=%2\n").arg(failures_).arg(devicePixelRatioF()*96).toUtf8());checkReport_.flush();
            QFile result(checkRoot_+"/result.txt");result.open(QIODevice::WriteOnly);result.write(QByteArray::number(failures_));result.close();checks_.stop();exiting_=true;close();return;
        }
        properties(1);check(!settings_->findChild<QComboBox*>("setting1001"),"direct recording-source picker removed from production Settings");settings_->hide();
        setDashboardAudio(false,true);setDashboardAudio(true,true);checkedMicSelection_=backend_->control(1016).selection;
        gameGain_->setValue(73);micGain_->setValue(129);
        gameMute_->setChecked(true);micMute_->setChecked(true);
        check(backend_->control(1019).value==73&&backend_->control(1020).value==129,"mixer volume controls");
        check(backend_->control(1017).checked&&backend_->control(1018).checked,"mixer mute controls");
        auto hotkey=backend_->quietHotkeyProbe();checkReport_.write((q(hotkey)+"\n").toUtf8());
        if(hotkey.starts_with(L"FAIL"))++failures_;
        clickDashboardMicCheck();nextCheckPhase();break;
    }
    case -2: {
        if(phaseClock_.elapsed()<2200)return;
        auto s=backend_->state();
        checkReport_.write(QString("MIC CHECK opened=%1 raw=%2 status=%3\n").arg(q(s.openedMic)).arg(s.micLevel).arg(q(s.micStatus)).toUtf8());
        checkReport_.write(QString("CAPTURED WASAPI METER packets=%1 frames=%2 square=%3 rms=%4 peak=%5 displayed=%6\n").arg(s.micMeter.packets).arg(s.micMeter.frames).arg(s.micMeter.square,0,'g',10).arg(s.micMeter.rms,0,'g',10).arg(s.micMeter.peak,0,'g',10).arg(s.micMeter.displayedRms,0,'g',10).toUtf8());
        if(s.micMeter.frames){check(std::abs(s.micMeter.rms-std::sqrt(s.micMeter.square/(2.0*s.micMeter.frames)))<0.000001,"actual captured PCM square/count agrees with direct RMS snapshot");check(s.micMeter.peak+0.000001>=s.micMeter.rms,"actual captured PCM peak is at least RMS");}
        check(micMeter_->property("rawRms").isValid()&&micMeter_->property("rawRms").toFloat()==(appliedMicOpen_?appliedMicMeter_.displayedRms:0),"captured microphone snapshot reaches actual Qt widget exactly");
        if(appliedMicOpen_&&appliedMicMeter_.displayedRms>0)check(micMeter_->property("rawRms").toFloat()>0,"quiet real WASAPI input is not quantized to zero by Qt meter");
        check(!s.openedMic.empty()||!s.micStatus.empty(),"mic check reports opened endpoint or startup error");
        check(backend_->control(1016).selection==checkedMicSelection_,"mic check does not switch selected device");
        settings_->hide();shot(this,"workspace-mic-check");
        if(backend_->control(1037).text.find(L"Stop")!=std::wstring::npos)clickDashboardMicCheck();
        nextCheckPhase();break;
    }
    case -1:
        if(phaseClock_.elapsed()<800)return;
        check(!backend_->state().encoding,"pre-record mic check opens no encoder");
        checkReport_.write("RETIRED: direct-source pending-start/source-popup assertions. Border permission state/property tests run in ReDotBorderPolicyTest; scene preview validates sources before consent.\n");
        gameGain_->setValue(100);micGain_->setValue(100);gameMute_->setChecked(false);micMute_->setChecked(false);
        setDashboardAudio(false,false);setDashboardAudio(true,false);settings_->hide();nextCheckPhase();break;
    case 0:
        if (phaseClock_.elapsed() < 500)
            return;
        if(!sceneModel_->ready())return;
        {
            const auto captureState=saveState(1);const auto previewSetting=backend_->control(2028).checked;
            navigationButtons_[1]->click();
            check(scenesVisible_&&pages_->currentWidget()==scenes_&&navigationButtons_[1]->isChecked(),"Scenes navigation opens a dedicated editor page");
            check(!mixerDock_->isVisible()&&!controlDock_->isVisible()&&!canvas_->isVisible(),"Scenes hides Capture controls and native preview without a competing pipeline");
            check(backend_->control(2028).checked==previewSetting,"Scenes preserves saved Capture preview setting");
            auto source=sceneModel_->addDefinition(Scenes::Kind::Text,"Owned editor title",{{"text","ReDot · Scenes"},{"fontSize",72}},{800,160});
            auto instance=sceneModel_->addVisual(source);
            check(!instance.isEmpty()&&scenes_->sourceList()->count()==2,"production editor uses real scene instances");
            auto v=*sceneModel_->visual(instance);v.x=100;v.y=90;sceneModel_->updateVisual(v);
            check(scenes_->sourceList()->currentItem()->data(Qt::UserRole).toString()==instance,"production editor synchronizes selection");
            const auto applies=property("backdropNativeApplies");const auto material=property("desktopBackdropMaterial");
            auto root=reinterpret_cast<HWND>(winId());const auto wasActive=GetActiveWindow()==root;
            SendMessageW(root,WM_NCACTIVATE,FALSE,0);SendMessageW(root,WM_ACTIVATE,WA_INACTIVE,0);SendMessageW(root,WM_ACTIVATEAPP,FALSE,0);
            check(property("desktopBackdropMaterial")==material&&property("backdropNativeApplies")==applies,"Scenes focus notifications do not change material or recreate backdrop");
            if(wasActive){SendMessageW(root,WM_ACTIVATEAPP,TRUE,GetCurrentThreadId());SendMessageW(root,WM_ACTIVATE,WA_ACTIVE,0);SendMessageW(root,WM_NCACTIVATE,TRUE,0);}
            navigationButtons_[0]->click();
            check(!scenesVisible_&&pages_->currentIndex()==0&&saveState(1)==captureState,"returning to Capture restores its exact dock layout");
            check(backend_->control(2028).checked==previewSetting,"returning to Capture preserves preview preference");
        }
        check(canvas_->width()>600&&canvas_->height()>300,"dashboard preview occupies the large central region");
        advancedAction_->setChecked(true);
        check(sourceDock_->isVisible() && mixerDock_->isVisible() && controlDock_->isVisible(), "real docks visible");
        check(canvas_->width() > 350 && canvas_->height() > 200, "central canvas occupies remaining space");
        check(titleBar_->findChild<QToolButton*>("Close")&&titleBar_->findChild<QToolButton*>("Minimize"),"accessible custom main caption buttons");
        {
            auto target=reinterpret_cast<HWND>(winId());
            // Cancel only this owned window's native move loop. No global input
            // or cursor placement: this checks actual client-event routing,
            // not physical snap/drag movement on the input desktop.
            std::jthread cancel([target]{Sleep(100);PostMessageW(target,WM_CANCELMODE,0,0);PostMessageW(target,WM_LBUTTONUP,0,0);});
            QPointF local(titleBar_->width()/2,22);
            QMouseEvent press(QEvent::MouseButtonPress,local,QPointF(titleBar_->mapToGlobal(local.toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(titleBar_,&press);
            check(titleBar_->property("moveRequests").toInt()>0&&titleBar_->property("moveAccepted").toBool(),"client title-bar press reaches supported native system-move request (cancelled on owned desktop)");
            QMouseEvent twice(QEvent::MouseButtonDblClick,local,QPointF(titleBar_->mapToGlobal(local.toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(titleBar_,&twice);check(isMaximized(),"client title-bar double-click maximizes");QApplication::sendEvent(titleBar_,&twice);check(!isMaximized(),"client title-bar double-click restores");
        }
        {
            auto hwnd=reinterpret_cast<HWND>(winId());RECT client{};GetClientRect(hwnd,&client);
            captionCheck(hwnd,"workspace");
            auto hit=[&](int x,int y){POINT p{x,y};ClientToScreen(hwnd,&p);return SendMessageW(hwnd,WM_NCHITTEST,0,MAKELPARAM(p.x,p.y));};
            check(hit(1,1)==HTTOPLEFT&&hit(client.right-2,client.bottom-2)==HTBOTTOMRIGHT,"custom frame retains native corner resize hit tests");
            check(hit(120,20)==HTCAPTION,"custom caption supports native drag/double-click/system menu");
            auto maximum=titleBar_->findChild<QToolButton*>("Maximize");auto point=maximum->mapTo(this,maximum->rect().center());
            check(hit(int(point.x()*devicePixelRatioF()),int(point.y()*devicePixelRatioF()))==HTMAXBUTTON,"Windows snap-layout maximize hit target");
            SendMessageW(hwnd,WM_NCLBUTTONDBLCLK,HTCAPTION,0);check(isMaximized(),"native caption double-click reaches maximize instead of dock-layout input");
            SendMessageW(hwnd,WM_NCLBUTTONDBLCLK,HTCAPTION,0);check(!isMaximized(),"native caption double-click restores");
            maximum->click();check(isMaximized(),"Qt custom maximize control");
            MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);POINT origin{};ClientToScreen(hwnd,&origin);GetClientRect(hwnd,&client);
            check(origin.x==monitor.rcWork.left&&origin.y==monitor.rcWork.top&&client.right==monitor.rcWork.right-monitor.rcWork.left&&client.bottom==monitor.rcWork.bottom-monitor.rcWork.top,"maximized client uses monitor work area without covering taskbar");
            maximum->click();check(!isMaximized(),"Qt custom restore control");
            auto minimum=titleBar_->findChild<QToolButton*>("Minimize");minimum->click();check(isMinimized(),"Qt custom minimize control");showNormal();
        }
        shot(this, "workspace");
        { auto actualMenu=findChild<QMenuBar*>("workspaceMenuBar");
        check(actualMenu&&!actualMenu->actions().isEmpty(),"workspace actions retained through custom-header application menu");
        inspectedMenu_=actualMenu&&!actualMenu->actions().isEmpty()?actualMenu->actions().front()->menu():nullptr; }
        checkedLayout_ = saveState(1);
        mixerDock_->setFloating(true);
        mixerDock_->resize(620, 230);
        nextCheckPhase();
        break;
    case 1:
        if (phaseClock_.elapsed() < 450)
            return;
        check(mixerDock_->isFloating(), "mixer floats");
        if(inspectedMenu_){
            inspectedMenu_->popup(mapToGlobal(QPoint(18,72)));check(inspectedMenu_->isVisible(),"actual File popup remains usable with consistent solid fallback");
            QKeyEvent down(QEvent::KeyPress,Qt::Key_Down,Qt::NoModifier);QApplication::sendEvent(inspectedMenu_,&down);check(inspectedMenu_->activeAction()!=nullptr,"native popup preserves keyboard navigation");
            shot(inspectedMenu_,"menu-solid-fallback");inspectedMenu_->hide();
        }
        shot(this, "workspace-floated");
        shot(mixerDock_, "mixer-floated");
        mixerDock_->setFloating(false);
        addDockWidget(Qt::RightDockWidgetArea, sourceDock_);
        controlDock_->hide();
        check(!controlDock_->isVisible(), "dock hide");
        controlDock_->show();
        checkedLayout_ = saveState(1);
        saveLayout();
        nextCheckPhase();
        break;
    case 2: {
        if (layoutStore_.busy())
            return;
        std::wstring warning;
        auto disk = layoutStore_.load(warning);
        auto state = QByteArray::fromBase64(q(disk["qtDockState"]).toLatin1());
        check(state == checkedLayout_, "dock state persisted to separate settings file");
        defaults();
        check(restoreState(state, 1), "restore persisted layout");
        check(dockWidgetArea(sourceDock_) == Qt::RightDockWidgetArea, "saved arrangement restored");
        lock_->setChecked(true);
        check(!(mixerDock_->features() & QDockWidget::DockWidgetMovable), "layout lock stops moving");
        lock_->setChecked(false);
        defaults();
        if(!backend_->verificationSettingsSettled())return;
        advancedAction_->setChecked(false);
        scrollChecks("idle");
        nextCheckPhase();
        break;
    }
    case 3:
        if (phaseClock_.elapsed() < 500)
            return;
        shot(settings_, "settings");
        captionCheck(reinterpret_cast<HWND>(settings_->winId()),"settings");
        settings_->hide();
        shot(this,"workspace-settings-closed");
        navigationButtons_[2]->click();
        capturePolicyGeneration_=property("backdropRefreshGeneration").toULongLong();
        mediaPolicyGeneration_=media_->property("backdropRefreshGeneration").toULongLong();
        captureNativeApplies_=property("backdropNativeApplies").toULongLong();
        mediaNativeApplies_=media_->property("backdropNativeApplies").toULongLong();
        SendMessageW(reinterpret_cast<HWND>(winId()),WM_SETTINGCHANGE,0,0);
        SendMessageW(reinterpret_cast<HWND>(media_->winId()),WM_SETTINGCHANGE,0,0);
        nextCheckPhase();
        break;
    case 4:
        if(phaseClock_.elapsed()<300)return;
        if(capturePolicyGeneration_){
            checkReport_.write(QString("POLICY generations Capture %1 -> %2; Media %3 -> %4\n").arg(capturePolicyGeneration_).arg(property("backdropRefreshGeneration").toULongLong()).arg(mediaPolicyGeneration_).arg(media_->property("backdropRefreshGeneration").toULongLong()).toUtf8());
            check(property("backdropRefreshGeneration").toULongLong()>capturePolicyGeneration_,"Capture re-queries effective policy after owned settings notification");
            check(media_->property("backdropRefreshGeneration").toULongLong()>mediaPolicyGeneration_,"Media re-queries effective policy after owned settings notification");
            check(property("backdropNativeApplies").toULongLong()==captureNativeApplies_,"unchanged policy notification does not reconstruct Capture backdrop");
            check(media_->property("backdropNativeApplies").toULongLong()==mediaNativeApplies_,"unchanged policy notification does not reconstruct Media backdrop");
            check(media_->format().alphaBufferSize()==8&&media_->color()==Qt::transparent,"deployed Media surface retains alpha and transparent clear color");
            check(mediaController_->desktopGlass()==media_->property("desktopBackdropMaterial").toBool(),"Media desktop foreground follows effective OS/application policy");
            check(mediaController_->metaObject()->indexOfProperty("backdrop")==-1,"Media exposes no whole-window thumbnail backdrop source");
            check(transparencySettings_->isVisible()==(property("windowsTransparencyDisabled").toBool()&&backend_->state().transparency),"main Windows transparency action reflects effective policy");
            check(visualItem(media_->rootObject(),"desktopBackdropFeedback")!=nullptr,"Media exposes Windows transparency feedback/action");
            capturePolicyGeneration_=0;
        }
        if (media_->status() == QQuickView::Error)
        {
            check(false, "QML load");
            nextCheckPhase();
            break;
        }
        if (mediaController_->rowCount() < 8)
        {
            if (phaseClock_.elapsed() > 19000)
            {
                checkReport_.write(QString("DETAIL count=%1 backend=%2 rev=%3 folder=%4\n")
                                       .arg(mediaController_->rowCount())
                                       .arg(backend_->clips().size())
                                       .arg(backend_->state().libraryRevision)
                                       .arg(q(backend_->state().folder))
                                       .toUtf8());
                checkReport_.flush();
            }
            return;
        }
        check(media_->status() == QQuickView::Ready, "QML material loads");
        check(mediaController_->rowCount() == 8, "eight independently encoded fixture clips");
        stage4SortChecks();
        mediaController_->query("", "Replay", "Newest");
        check(mediaController_->rowCount() == 4, "replay filter");
        mediaController_->query("", "Has audio", "Newest");
        check(mediaController_->rowCount() == 6, "audio filter");
        mediaController_->query("mic", "All", "Name");
        check(mediaController_->rowCount() == 2, "media search");
        mediaController_->query("", "All", "Newest");
        nextCheckPhase();
        break;
    case 5:
        if (phaseClock_.elapsed() < 1800)
            return;
        if(visualChecksStarted_)return;
        visualChecksStarted_=true;
        mediaShot("media-glass");
        {
            auto panel=media_->rootObject()->findChild<QObject*>("navigationGlass");
            check(panel&&!panel->property("sourceItem").isValid(),"shared material has no scene sampler");
            QTimer::singleShot(250,this,[this,panel]{
                check(!panel->property("sampleUpdates").isValid(),"retired local-image blur path is absent");
                auto choice=media_->rootObject()->findChild<QObject*>("filterChoice");
                auto popup=choice->property("popup").value<QObject*>();
                check(popup!=nullptr,"filter popover exists");
                if(popup)popup->setProperty("visible",true);
                QTimer::singleShot(250,this,[this,popup]{
                    mediaShot("media-popover");if(popup)popup->setProperty("visible",false);
                    properties(6);
                    auto reduce=settings_->findChild<QCheckBox*>("setting2072");reduce->setChecked(true);
                    check(!backend_->control(2072).checked,"Reduce transparency checkbox enables solid surfaces");
                    auto motion=settings_->findChild<QCheckBox*>("setting2073");motion->setChecked(true);
                    check(!backend_->control(2073).checked,"Reduce motion checkbox disables transitions");
                    settings_->hide();nextCheckPhase();
                });
            });
        }
        captionCheck(reinterpret_cast<HWND>(media_->winId()),"View Media");
        break;
    case 6:
        // Policy and MediaController refresh on independent timers. A GPU grab
        // under concurrent decoder load can delay them beyond a fixed 650 ms.
        // Await the observable result, with a strict deadline; do not force the
        // policy or pass a stale material state.
        if (phaseClock_.elapsed() < 650 ||
            ((mediaController_->desktopGlass() || mediaController_->motion()) && phaseClock_.elapsed() < 3000))
            return;
        checkReport_.write(QString("Reduced appearance observed after %1 ms\n").arg(phaseClock_.elapsed()).toUtf8());
        mediaShot("media-solid");
        checkReport_.write(QString("DETAIL reduced appearance: controller desktop=%1 motion=%2 visible=%3; backend transparency=%4 motion=%5 material=%6\n").arg(mediaController_->desktopGlass()).arg(mediaController_->motion()).arg(media_->isVisible()).arg(backend_->state().transparency).arg(backend_->state().motion).arg(media_->property("desktopBackdropMaterial").toBool()).toUtf8());
        check(!mediaController_->desktopGlass()&&!mediaController_->motion(),"media scene receives reduced appearance settings");
        settings_->findChild<QCheckBox*>("setting2072")->setChecked(false);
        backend_->edit(2072,L"",0,true);
        settings_->findChild<QCheckBox*>("setting2073")->setChecked(false);
        // MKV flow: open the MKV recording itself (the Recording filter excludes replay clips) so playback, seeking and
        // track selection below demonstrably run on a Matroska file; the MP4 flow keeps its original query.
        mediaController_->query("both", qEnvironmentVariableIntValue("REDOT_QUIET_FORMAT_SWITCH") ? "Recording" : "All", "Newest");
        mediaController_->open(0);
        nextCheckPhase();
        break;
    case 7:
        if (phaseClock_.elapsed() < 1700)
            return;
        if (qEnvironmentVariableIntValue("REDOT_QUIET_FORMAT_SWITCH"))
            check(mediaController_->title().endsWith(".mkv", Qt::CaseInsensitive), "Media window is playing an MKV recording");
        check(mediaController_->playing() && mediaController_->duration() > 3, "silent Qt playback ready/duration");
        check(mediaController_->gpuFrames()>0,"playback has RHI texture frames");
        checkReport_.write(QString("PLAYBACK GPU texture frames=%1 CPU frames=%2\n").arg(mediaController_->gpuFrames()).arg(mediaController_->cpuFrames()).toUtf8());
        check(mediaController_->tracks().size() == 3, "Mix/Game/Microphone playback tracks");
        mediaController_->toggle();
        mediaController_->seek(2);
        nextCheckPhase();
        break;
    case 8:
        if (phaseClock_.elapsed() < 700)
            return;
        if((mediaController_->videoStartTimeUs()<1950000||mediaController_->videoStartTimeUs()>2100000)&&phaseClock_.elapsed()<3000)return;
        check(mediaController_->position() > 1.8 && mediaController_->position() < 2.3, "paused seeking");
        checkReport_.write(QString("PAUSED SEEK position=%1 presented=%2 us serial=%3\n").arg(mediaController_->position()).arg(mediaController_->videoStartTimeUs()).arg(mediaController_->frameSerial()).toUtf8());
        check(mediaController_->videoStartTimeUs()>=1950000&&mediaController_->videoStartTimeUs()<=2100000,"paused seek presents requested decoded frame");
        mediaShot("playback");
        {
            check(!media_->rootObject()->findChild<QObject*>("playbackGlassSample"),"production playback material never samples video/background pixels");
            check(media_->rootObject()->findChild<QQuickItem*>("playbackGlass")!=nullptr,"playback shares the production GlassPanel component");
        }
        mediaController_->volume(0.25);
        check(mediaController_->playbackVolume()==0.25,"playback volume");
        mediaController_->mute();check(mediaController_->muted(),"playback mute");mediaController_->mute();
        backend_->generatedSource(4);
        mediaController_->track(1);
        mediaController_->fullscreen();
        nextCheckPhase();
        break;
    case 9:
        if (phaseClock_.elapsed() < 900)
            return;
        // Media track selection is asynchronous; wait within an explicit budget,
        // rather than asserting that a fixed 900 ms has completed the operation.
        if ((mediaController_->position() <= 1.8 || mediaController_->position() >= 2.3) &&
            phaseClock_.elapsed() < 3000)
            return;
        checkReport_.write(QString("DETAIL track selection position=%1 elapsed=%2 ms\n")
                               .arg(mediaController_->position(), 0, 'f', 3)
                               .arg(phaseClock_.elapsed()).toUtf8());
        check(mediaController_->position() > 1.8 && mediaController_->position() < 2.3,
              "track change preserves position");
        check(media_->visibility() == QWindow::FullScreen, "fullscreen playback window");
        mediaController_->escape();
        check(media_->visibility() != QWindow::FullScreen, "Escape exits fullscreen");
        mediaController_->back();
        mediaController_->query("", "All", "Newest");
        setDashboardAudio(false,true);setDashboardAudio(true,true);settings_->hide();
        backend_->generatedSource(fixtureSeconds(3));nativeControlClick(record_);tick();
        check(backend_->state().encoding,"Qt Start Recording starts only the selected scene compositor stream");
        nextCheckPhase();
        break;
    case 10:
        if (!backend_->state().recording)
            return;
        if(record_->text()!="Stop Recording"&&phaseClock_.elapsed()<1500)return;
        if(!record_->isEnabled()&&phaseClock_.elapsed()<1500)return;
        if(!backend_->verificationSettingsSettled())return;
        quickGears_[0]->click();check(quick_->isVisible()&&backend_->state().recording,"recording gear leaves the active encoder running");
        {auto outputPreference=quick_->findChild<QComboBox*>("setting2030");check(outputPreference&&!outputPreference->isEnabled(),"active recording prevents incompatible encoder preference changes");quick_->hide();}
        scrollChecks("recording");
        navigationButtons_[1]->click();check(scenesVisible_&&backend_->state().recording&&backend_->state().encoding,"Scenes editing leaves ordinary recording and encoder running");
        navigationButtons_[0]->click();check(!scenesVisible_&&backend_->state().recording,"return to Capture retains ordinary recording session");
        check(record_->isEnabled()&&record_->text()=="Stop Recording","Qt recording control reflects active worker");
        check(media_->isVisible(), "media open during recording");
        checkReport_.write("TRACE before recording audio state read\n");checkReport_.flush();
        checkReport_.write(("RECORDING AUDIO "+q(backend_->state().gameStatus)+" | "+q(backend_->state().micStatus)+"\n").toUtf8());
        checkReport_.write("TRACE before recording screenshot/readback\n");checkReport_.flush();shot(this,"workspace-recording");
        settings_->hide();
        stage4HideEncoding();
        nextCheckPhase();
        break;
    case 11:
        if (phaseClock_.elapsed() < std::max(2500,hiddenHoldMs()))
            return;
        stage4RestoreEncoding();
        liveMuteCheck();
        check(gameMeter_->property("rawRms").isValid()&&gameMeter_->property("rawRms").toFloat()==(appliedGameOpen_?appliedGameMeter_.displayedRms:0),"selected process-loopback snapshot reaches Qt game meter exactly");
        checkReport_.write(QString("LIVE WASAPI game packets=%1 rms=%2 peak=%3 mic packets=%4 rms=%5 peak=%6\n").arg(appliedGameMeter_.packets).arg(appliedGameMeter_.rms,0,'g',10).arg(appliedGameMeter_.peak,0,'g',10).arg(appliedMicMeter_.packets).arg(appliedMicMeter_.rms,0,'g',10).arg(appliedMicMeter_.peak,0,'g',10).toUtf8());
        previewAction_->setChecked(false);
        check(!canvas_->isVisible() && backend_->state().encoding, "hidden preview leaves recording active");
        previewAction_->setChecked(true);
        if(qEnvironmentVariableIntValue("REDOT_QUIET_FORMAT_SWITCH")){
            check(backend_->control(2040).selection==0,"MKV is selected when the recording starts");
            backend_->edit(2040,L"",1,false);backend_->action(2010);
            check(backend_->control(2040).selection==1,"MP4 can be chosen while an MKV recording is running (applies to the next recording)");
        }
        nativeControlClick(record_);
        nextCheckPhase();
        break;
    case 12:
        if (backend_->state().encoding)
            return;
        check(q(backend_->state().recordStatus).contains("Saved MP4")||q(backend_->state().recordStatus).contains("Saved MKV"), "ordinary recording finalized");
        setDashboardAudio(true,false);
        nativeControlClick(replay_);tick();
        check(backend_->state().encoding,"Qt replay starts through canonical scene output");
        nextCheckPhase();
        break;
    case 13:
        if (backend_->state().historySeconds < 2)
            return;
        if(!backend_->verificationSettingsSettled())return;
        quickGears_[1]->click();check(quick_->isVisible()&&backend_->state().replay,"replay gear does not interrupt rolling capture");quick_->hide();
        scrollChecks("replay");
        {const auto history=backend_->state().historySeconds;navigationButtons_[1]->click();check(scenesVisible_&&backend_->state().replay&&backend_->state().historySeconds>=history,"Scenes preserves rolling replay and its encoded history");navigationButtons_[0]->click();}
        check(replay_->isEnabled()&&replay_->text()=="Stop Replay Buffer","Qt replay control reflects active worker");
        liveMuteCheck();
        settings_->hide();
        check(!advanced_&&mixerDock_->height()<=170,"dashboard audio cards remain compact after floating/restoring advanced docks");
        shot(this,"workspace-replay");
        check(save_->isEnabled(),"Save Clip control enabled for available history");nativeControlClick(save_);
        stage4HideEncoding();
        nextCheckPhase();
        break;
    case 14:
        // Fast exports can finish before one 30 FPS frame elapses. Observe
        // hidden delivery over several frame periods, not the export speed.
        if (phaseClock_.elapsed() < std::max(250,hiddenHoldMs()) || !q(backend_->state().replayStatus).startsWith("Saved clip:"))
            return;
        stage4RestoreEncoding();
        check(backend_->state().encoding && backend_->state().replay, "replay continues during media/export");
        if(auto close=visualItem(media_->rootObject(),"mediaWindow2"))QMetaObject::invokeMethod(close,"clicked");else check(false,"media custom Close button found");
        check(backend_->state().encoding, "closing media leaves replay active");
        nativeControlClick(replay_);
        nextCheckPhase();
        break;
    case 15:
        if (backend_->state().encoding)
            return;
        setDashboardAudio(false,false);setDashboardAudio(true,true);
        backend_->generatedSource(fixtureSeconds(8));nativeControlClick(record_);nativeControlClick(replay_);
        media();
        nextCheckPhase();
        break;
    case 16:
        if (!backend_->state().recording || backend_->state().historySeconds < 2)
            return;
        if(!backend_->verificationSettingsSettled())return;
        scrollChecks("recording+replay");
        settings_->hide();
        shot(this,"workspace-recording-replay");
        check(property("taskbarMode").toUInt()==unsigned(Branding::Mode::Recording),"native taskbar remains red for ordinary recording plus replay");
        check(save_->isEnabled(),"shared session Save Clip enabled");stage4HideEncoding();backend_->verificationHotkey(0);
        nextCheckPhase();
        break;
    case 17:
        if (phaseClock_.elapsed() < std::max(250,hiddenHoldMs()) || !q(backend_->state().replayStatus).startsWith("Saved clip:"))
            return;
        stage4RestoreEncoding();
        check(q(backend_->state().replayStatus).startsWith("Saved clip:"),"Stage 4 process-local WM_HOTKEY saves while hidden (no physical key injection)");
        check(backend_->state().encoding && backend_->state().recording, "shared ordinary/replay session stays active");
        diagnostics();
        backend_->action(1030);
        nativeControlClick(record_);
        nextCheckPhase();
        break;
    case 18:
        if(!checkedSharedReturn_){
            auto state=backend_->state();if(state.recording||state.taskbarMode!=unsigned(Branding::Mode::Replay)||property("taskbarMode").toUInt()!=unsigned(Branding::Mode::Replay))return;
            check(state.encoding&&state.replay,"stopping ordinary recording returns actual running replay to green");
            checkedSharedReturn_=true;nativeControlClick(replay_);return;
        }
        if (backend_->state().encoding)
            return;
        check(q(backend_->state().recordStatus).contains("Saved MP4")||q(backend_->state().recordStatus).contains("Saved MKV"), "shared ordinary/replay finalized");
        shot(diagnostics_, "diagnostics");
        captionCheck(reinterpret_cast<HWND>(diagnostics_->winId()),"diagnostics");
        check(!QDir(checkRoot_ + "/output").entryList({"ReDot_Diagnostics_*.txt"}, QDir::Files).isEmpty(),
              "diagnostics report exported");
        media();mediaController_->query("both","All","Newest");mediaController_->open(0);
        mediaController_->player()->setLoops(QMediaPlayer::Infinite);
        renderProbe_=new QtRenderProbe(media_);
        renderDrive_.setInterval(16);
        QObject::connect(&renderDrive_,&QTimer::timeout,this,[this]{media_->update();});
        renderDrive_.start();nextCheckPhase();break;
    case 19:
        if(phaseClock_.elapsed()<1500)return;
        if(!backend_->verificationSettingsSettled())return;
        scrollChecks("glass-enabled playback");settings_->hide();
        renderProbe_->stage(1);nextCheckPhase();break;
    case 20:
        if(phaseClock_.elapsed()<4000)return;
        renderProbe_->stage(0);
        checkReport_.write(renderProbe_->report(1,"glass enabled").toUtf8());
        backend_->edit(2072,L"",0,false);nextCheckPhase();break;
    case 21:
        if(phaseClock_.elapsed()<800)return;
        if(!backend_->verificationSettingsSettled())return;
        scrollChecks("glass-disabled playback");settings_->hide();
        renderProbe_->stage(2);nextCheckPhase();break;
    case 22:
        if(phaseClock_.elapsed()<4000)return;
        renderProbe_->stage(0);renderDrive_.stop();
        checkReport_.write(renderProbe_->report(2,"glass disabled").toUtf8());
        mediaController_->back();
        settings_->findChild<QCheckBox*>("setting2072")->setChecked(false);
        backend_->edit(2072,L"",0,true);
        mediaController_->query("video","Recording","Newest");mediaController_->open(0);
        nextCheckPhase();break;
    case 23:
        if(phaseClock_.elapsed()<1600)return;
        check(mediaController_->playing()&&mediaController_->duration()>3,"dark encoded fixture ready");
        if(!mediaController_->paused())mediaController_->toggle();
        mediaController_->seek(2);nextCheckPhase();break;
    case 24:
        if(phaseClock_.elapsed()<650)return;
        mediaShot("playback-dark");mediaController_->seek(1.1);nextCheckPhase();break;
    case 25:
        if(phaseClock_.elapsed()<650)return;
        // Wait for an actual decoded frame, not only the requested position.
        if((mediaController_->videoStartTimeUs()<1030000||mediaController_->videoStartTimeUs()>1167000)&&phaseClock_.elapsed()<3000)return;
        if(!brightPresented_){media_->grabWindow();brightPresented_=true;phaseClock_.restart();return;}
        checkReport_.write(QString("BRIGHT SEEK position=%1 presented frame=%2 us serial=%3\n").arg(mediaController_->position()).arg(mediaController_->videoStartTimeUs()).arg(mediaController_->frameSerial()).toUtf8());
        mediaShot("playback-bright");
        {auto image=media_->grabWindow();auto color=image.pixelColor(image.width()/2,image.height()/2);check(color.red()+color.green()+color.blue()>680,"decoded white flash is present in bright-material screenshot");}
        {auto image=media_->grabWindow();auto panel=media_->rootObject()->findChild<QQuickItem*>("playbackGlass");auto c=image.pixelColor((panel->x()+660)*media_->devicePixelRatio(),(panel->y()+22)*media_->devicePixelRatio());check(c.red()+c.green()+c.blue()<200&&c.red()+c.green()+c.blue()>55,"shared playback material remains readable and does not sample the white video frame");}
        mediaController_->back();
        mediaController_->query("No matching owned test fixture","All","Newest");
        nextCheckPhase();break;
    case 26:
        if(phaseClock_.elapsed()<650)return;
        check(mediaController_->rowCount()==0,"empty filtered gallery");mediaShot("media-empty");
        backend_->edit(2072,L"",0,true);settings_->hide();
        if(auto layer=dynamic_cast<WorkspaceGlassLayer*>(findChild<QQuickWidget*>("workspaceGlassLayer"))){
            check(layer->testAttribute(Qt::WA_TransparentForMouseEvents)&&layer->focusPolicy()==Qt::NoFocus,"GPU background does not intercept widget input");
            check(layer->property("materialRegionCount").toInt()<=128,"workspace material region count is bounded");
            materialPoints_={titleBar_->mapTo(this,QPoint(titleBar_->width()/2,22)),record_->mapTo(this,record_->rect().center())};
            check(!layer->rootObject()->property("pattern").isValid(),"no internal wallpaper or decorative pattern backdrop");nextCheckPhase();return;
        }
        check(false,"workspace material scene exists");nextCheckPhase();return;
    case 27:
        if(phaseClock_.elapsed()<600)return;
        {auto layer=dynamic_cast<WorkspaceGlassLayer*>(findChild<QQuickWidget*>("workspaceGlassLayer"));workspacePattern_=layer->grabFramebuffer();workspacePattern_.save(checkRoot_+"/screenshots/workspace-pattern-a.png");}
        nextCheckPhase();return;
    case 28:
        if(phaseClock_.elapsed()<600)return;
        {auto layer=dynamic_cast<WorkspaceGlassLayer*>(findChild<QQuickWidget*>("workspaceGlassLayer"));auto b=layer->grabFramebuffer();b.save(checkRoot_+"/screenshots/workspace-pattern-b.png");
         check(layer->format().alphaBufferSize()>=8,"workspace GPU surface retains an alpha channel");
         auto color=b.pixelColor(b.width()/2,b.height()/2);check(color.alpha()>0&&color.alpha()<255,"foreground glass preserves transmitted alpha, no opaque fill");
         auto actionPixel=b.pixelColor(record_->mapTo(this,record_->rect().center())*devicePixelRatioF());
         checkReport_.write(QString("PANEL alpha=%1 root alpha=%2 (foreground only, not DWM transmission)\n").arg(actionPixel.alpha()).arg(color.alpha()).toUtf8());
         check(actionPixel.alpha()>0&&actionPixel.alpha()<255,"actual action material stays translucent; no opaque MultiEffect source beneath it");
         checkReport_.write(("BACKDROP "+property("desktopBackdropStatus").toString()+"; compositor-level controlled-background transmission requires active-desktop manual check.\n").toUtf8());
         workspaceProbe_=new QtRenderProbe(layer->quickWindow());workspaceProbe_->stage(1);resourceStart();
         QObject::disconnect(&renderDrive_,nullptr,nullptr,nullptr);QObject::connect(&renderDrive_,&QTimer::timeout,layer,[layer]{auto root=layer->rootObject();root->setProperty("probeTick",root->property("probeTick").toInt()+1);});renderDrive_.start();}
        nextCheckPhase();return;
    case 29:
        if(phaseClock_.elapsed()<3000)return;
        workspaceProbe_->stage(0);resourceReport("workspace foreground glass");checkReport_.write(workspaceProbe_->report(1,"workspace glass").toUtf8());
        backend_->edit(2072,L"",0,false);nextCheckPhase();return;
    case 30:
        if(phaseClock_.elapsed()<600)return;
        {auto layer=dynamic_cast<WorkspaceGlassLayer*>(findChild<QQuickWidget*>("workspaceGlassLayer"));check(!layer->rootObject()->property("glass").toBool(),"workspace reduce-transparency selects deliberate solid background");
         bool fixed=true;int panels=0;std::function<void(QQuickItem*)> visit=[&](QQuickItem* item){for(auto object:item->childItems()){if(object->property("glass").isValid()){++panels;fixed&=object->property("glass").toBool();}visit(object);}};visit(layer->rootObject());
         checkReport_.write(QString("CANONICAL panels=%1 fixed=%2 regions=%3\n").arg(panels).arg(fixed).arg(layer->property("materialRegionCount").toInt()).toUtf8());
         check(panels>0&&fixed,"panel material stays canonical when background falls back to solid");}
        nextCheckPhase();return;
    case 31:
        if(phaseClock_.elapsed()<600)return;
        {auto layer=dynamic_cast<WorkspaceGlassLayer*>(findChild<QQuickWidget*>("workspaceGlassLayer"));workspacePattern_=layer->grabFramebuffer();}
        nextCheckPhase();return;
    case 32:
        if(phaseClock_.elapsed()<600)return;
        {auto layer=dynamic_cast<WorkspaceGlassLayer*>(findChild<QQuickWidget*>("workspaceGlassLayer"));auto b=layer->grabFramebuffer();for(auto point:materialPoints_){auto pixel=point*devicePixelRatioF();auto aColor=workspacePattern_.pixelColor(pixel),bColor=b.pixelColor(pixel);int difference=std::abs(aColor.red()-bColor.red())+std::abs(aColor.green()-bColor.green())+std::abs(aColor.blue()-bColor.blue());check(difference<5,"static solid foreground snapshot remains stable (not a compositor/background-change test), RGB delta="+QString::number(difference));}workspaceProbe_->stage(2);resourceStart();}
        nextCheckPhase();return;
    case 33:
        if(phaseClock_.elapsed()<3000)return;
        workspaceProbe_->stage(0);resourceReport("workspace solid");renderDrive_.stop();checkReport_.write(workspaceProbe_->report(2,"workspace solid").toUtf8());delete workspaceProbe_;workspaceProbe_=nullptr;
        backend_->edit(2072,L"",0,true);
        checkReport_.write("RETIRED: a missing individual source no longer fails the whole recording's first-frame wait. The compositor produces valid blank pixels for it; encoded scene integration checks cover this contract.\n");
        checkPhase_=39;phaseClock_.restart();return;
    case 39:
        if(backend_->state().encoding)return;
        check(q(backend_->state().recordStatus).startsWith("Saved"),"retry after no-frame failure finalizes successfully");
        settings_->findChild<QCheckBox*>("setting2072")->setChecked(true);
        settings_->findChild<QCheckBox*>("setting2073")->setChecked(true);
        backend_->edit(2039,L"",0,true);
        backend_->action(2010);
        stage4BeforeExitFiles_=QDir(checkRoot_+"/output").entryList({"*.mp4","*.mkv"},QDir::Files); // finalized MP4 and MKV; unfinished files are .partial.*
        backend_->generatedSource(8);trayRecord_->trigger();trayReplay_->trigger();
        checkPhase_=40;phaseClock_.restart();return;
    case 40:
        if(backend_->state().taskbarMode!=unsigned(Branding::Mode::Recording)||backend_->state().historySeconds<2)return;
        updateTray(backend_->state());
        check(trayState_->text()=="Recording and replay running","Stage 4 final session confirms recording plus replay tray status");
        if(!traySave_->isEnabled())return;
        // The production command posts WM_APP+16. Let that command run before
        // Exit, then retain the accepted export with the existing bounded hook.
        qputenv("PANDACLIP_TEST_EXPORT_DELAY_MS","3000");
        traySave_->trigger();
        checkPhase_=41;phaseClock_.restart();return;
    case 41:
        if(!backend_->state().pendingSaves&&phaseClock_.elapsed()<2000)return;
        check(backend_->state().pendingSaves>0,"Stage 4 accepted export before explicit Exit: "+q(backend_->state().replayStatus));
        {QFile expected(checkRoot_+"/stage4-exit-expected.txt");expected.open(QIODevice::WriteOnly);expected.write(QByteArray::number(stage4BeforeExitFiles_.size()+2));}
        backend_->edit(2038,L"",0,true);
        check(backend_->state().closeToTray,"Stage 4 explicit Exit tested with Close-to-tray enabled");
        checkReport_.write(QString("MEASURE lightweight UI tick mean=%1 max=%2 us count=%3\n")
                               .arg(tickCount_ ? tickTotalUs_ / tickCount_ : 0)
                               .arg(tickMaxUs_)
                               .arg(tickCount_)
                               .toUtf8());
        checkReport_.write(
            QString("RESULT failures=%1 DPI=%2\n").arg(failures_).arg(devicePixelRatioF() * 96).toUtf8());
        checkReport_.flush();
        advancedAction_->setChecked(true);
        addDockWidget(Qt::RightDockWidgetArea, sourceDock_);
        mixerDock_->setFloating(true);
        saveLayout();
        QFile result(checkRoot_ + "/result.txt");
        result.open(QIODevice::WriteOnly);
        result.write(QByteArray::number(failures_));
        result.close();
        checks_.stop();
        trayExit_->trigger();
        check(backend_->state().closing&&exiting_,"Stage 4 explicit tray Exit bypasses hide and requests real drain");
        result.open(QIODevice::WriteOnly);result.write(QByteArray::number(failures_));result.close();
        break;
    }
}
