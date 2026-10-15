// Quiet, targeted calls on the shipping widgets; no global desktop input.
void nativeControlClick(QPushButton* control,QPoint local=QPoint(-1,-1),bool expectedActivation=true){
    if(qEnvironmentVariableIntValue("PANDACLIP_QT_HIDDEN_CHECK")){control->click();return;}
    QWidget* host=control->nativeParentWidget();if(!host)host=this;
    const auto point=control->mapTo(host,local.x()<0?control->rect().center():local);const auto scale=host->devicePixelRatioF();
    const auto param=MAKELPARAM(int(point.x()*scale),int(point.y()*scale));
    bool fired=false;auto connection=QObject::connect(control,&QPushButton::clicked,control,[&]{fired=true;});
    HWND hwnd=reinterpret_cast<HWND>(host->winId());
    SendMessageW(hwnd,WM_MOUSEMOVE,0,param);SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,param);SendMessageW(hwnd,WM_LBUTTONUP,0,param);
    // Native Qt mouse events are queued as user input. Drain them only on the
    // inactive owned desktop; excluding input gave premature false failures.
    QApplication::processEvents();QObject::disconnect(connection);
    check(fired==expectedActivation,(expectedActivation?"native client coordinates activate visible Qt control ":"outside native click dismisses popup without activating ")+control->objectName());
}
void editVisibleText(QComboBox* combo,const QString& value){
    check(combo&&combo->isEditable(),"editable canonical quick preference exists");if(!combo||!combo->isEditable())return;
    combo->lineEdit()->setText(value);QMetaObject::invokeMethod(combo->lineEdit(),"textEdited",Q_ARG(QString,value));
}
void setDashboardAudio(bool mic,bool enabled){
    const auto kind=mic?Scenes::Kind::Microphone:Scenes::Kind::DesktopAudio;QString definition;for(auto& d:sceneModel_->data().definitions)if(d.kind==kind&&d.settings["verificationFixture"].toBool())definition=d.id;
    if(definition.isEmpty()){check(false,"owned scene audio fixture exists");return;}QString assignment;for(auto& a:sceneModel_->current()->audio)if(a.sourceId==definition)assignment=a.id;
    if(enabled&&assignment.isEmpty())sceneModel_->addAudio(definition);else if(!enabled&&!assignment.isEmpty())sceneModel_->removeAudio(assignment);tick();
    check((mic?backend_->state().micEnabled:backend_->state().gameEnabled)==enabled,"scene audio assignment controls enabled source");
}
void clickDashboardMicCheck(){quickGears_[4]->click();auto button=quick_->findChild<QPushButton*>("setting1037");check(button&&button->isEnabled(),"visible microphone Quick Settings Check Mic action");if(button)button->click();quick_->hide();}
void liveMuteCheck(){
    auto state=backend_->state();
    check(gameMute_->isEnabled()==state.gameEnabled&&micMute_->isEnabled()==state.micEnabled,"dashboard mute availability follows selected scene assignments while encoding");
    for(auto button:{gameMute_,micMute_})if(button->isEnabled())nativeControlClick(button);
    const unsigned expected=(gameMute_->isChecked()?1u:0u)|(micMute_->isChecked()?2u:0u);
    check(backend_->verificationLiveMutes()==expected,"visible mute edits reach scene snapshot consumed by audio worker");
    for(auto button:{gameMute_,micMute_})if(button->isEnabled())nativeControlClick(button);
    check(backend_->verificationLiveMutes()==((gameMute_->isChecked()?1u:0u)|(micMute_->isChecked()?2u:0u)),"live unmute/restore updates scene without restarting capture");
}
void quickPopoverCheck(int kind){
    check(quick_&&quick_->isVisible()&&quick_->property("quickKind").toInt()==kind,"separate Quick Settings gear opens correct feature "+QString::number(kind));
    if(!quick_)return;
    auto hwnd=reinterpret_cast<HWND>(winId());
    const auto foreground=GetForegroundWindow();
    const auto before=property("desktopBackdropMaterial");
    check(SendMessageW(hwnd,WM_NCACTIVATE,FALSE,0)==TRUE,"custom caption permits real deactivation (does not prevent focus loss)");
    SendMessageW(hwnd,WM_ACTIVATEAPP,FALSE,GetCurrentThreadId());
    check(before==property("desktopBackdropMaterial")&&GetForegroundWindow()==foreground,"deactivation notification retains material without changing actual foreground");
    check(!property("desktopBackdropAccepted").toBool()||property("desktopBackdropMaterial").toBool(),"accepted Acrylic is retained even when another application owns focus");
    check(!quick_->isWindow()&&!quick_->testAttribute(Qt::WA_NativeWindow),"Quick Settings is a non-native child overlay (no activation transfer)");
    check(!QApplication::activePopupWidget(),"Quick Settings has no native popup input grab");
    if(auto combo=quick_->findChild<QComboBox*>()){
        const auto material=property("desktopBackdropMaterial");
        combo->showPopup();QApplication::processEvents();
        auto popup=QApplication::activePopupWidget();
        check(popup&&DesktopBackdrop::sameActivationFamily(reinterpret_cast<HWND>(winId()),reinterpret_cast<HWND>(popup->winId())),"native choice popup retains main activation family");
        check(property("desktopBackdropMaterial")==material,"opening choice preserves main material state on owned desktop");
        combo->hidePopup();
    }
    check(!backend_->state().encoding&&!backend_->state().pendingStart,"gear never starts recording/replay");
    check(findChildren<QDialog*>("quickSettings").size()==1,"only one quick popover is owned");
    const auto action=quickGears_[kind];QRect button(action->mapToGlobal(QPoint()),action->size());
    check(!QRect(quick_->mapToGlobal(QPoint()),quick_->size()).intersects(button),"popover does not cover opening action");
    const QRect window(mapToGlobal(QPoint()),size());check(window.contains(QRect(quick_->mapToGlobal(QPoint()),quick_->size())),"anchored popover fits current main-window bounds");
    const std::array<QString,5> shots={"recording-quick","replay-quick","save-quick","game-audio-quick","microphone-quick"};shot(quick_,shots[kind]);
    if(kind==0){
        auto fps=quick_->findChild<QComboBox*>("setting2022");fps->setCurrentIndex(1);check(backend_->control(2022).text==L"60","quick FPS uses canonical recording preference");
        auto track=quick_->findChild<QComboBox*>("quickTrackLayout");track->setCurrentIndex(1);check(backend_->control(2033).checked&&!backend_->control(2034).checked&&!backend_->control(2035).checked,"quick Mix-only edits canonical stream flags");track->setCurrentIndex(0);fps->setCurrentIndex(0);
        quick_->findChild<QPushButton*>("applyQuickSettings")->click();
        quick_->findChild<QPushButton*>("advancedSettingsLink")->click();
        check(settings_&&settings_->isVisible(),"quick Advanced link opens relevant Settings");
        check(DesktopBackdrop::sameActivationFamily(reinterpret_cast<HWND>(winId()),reinterpret_cast<HWND>(settings_->winId())),"Settings belongs to main native activation family");
        auto unrelated=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Owned family test",WS_POPUP,0,0,10,10,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(unrelated&&!DesktopBackdrop::sameActivationFamily(reinterpret_cast<HWND>(winId()),unrelated),"unrelated same-process root cannot retain active material");if(unrelated)DestroyWindow(unrelated);
        auto advancedFps=settings_->findChild<QLineEdit*>("setting2022");check(advancedFps&&advancedFps->text()=="30","advanced and quick FPS agree");settings_->hide();
    }else if(kind==1){
        auto duration=quick_->findChild<QComboBox*>("setting1025");editVisibleText(duration,"90");check(backend_->control(1025).text==L"90","custom replay seconds reach canonical duration");
        editVisibleText(duration,"4");check(backend_->control(1025).text==L"90","out-of-range quick duration does not replace valid preference");editVisibleText(duration,"90");
        editVisibleText(quick_->findChild<QComboBox*>("setting1028"),"512");quick_->findChild<QPushButton*>("applyQuickSettings")->click();
    }else if(kind==2){
        auto duration=quick_->findChild<QComboBox*>("setting1025");check(duration->currentText()=="90","Save Clip and Replay share one custom-duration preference");editVisibleText(duration,"60");
        auto shortcut=quick_->findChild<QKeySequenceEdit*>("setting2200");check(shortcut!=nullptr,"custom Save Clip shortcut editor exists");
        if(shortcut){shortcut->setKeySequence(QKeySequence(Qt::CTRL|Qt::ALT|Qt::Key_F22));check(LOBYTE(backend_->control(2200).value)==VK_F22,"quick shortcut reaches existing binding model");shortcut->setKeySequence(QKeySequence(Qt::CTRL|Qt::SHIFT|Qt::Key_F9));}
        quick_->findChild<QPushButton*>("applyQuickSettings")->click();
    }else if(kind==3||kind==4){check(!quick_->findChild<QComboBox*>(QString("setting%1").arg(kind==3?1014:1016)),"audio source selection belongs only to Scenes");}
    if(quick_&&quick_->isVisible()){QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(quick_,&escape);check(!quick_->isVisible(),"Escape dismisses quick popover");}
}

void dashboardSmallChecks(){
    QSize original=size();resize(860,620);QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    checkReport_.write(QString("SMALL requested=860x620 actual=%1x%2\n").arg(width()).arg(height()).toUtf8());
    check(size()==QSize(860,620),"dashboard actually fits its advertised minimum window size");
    check(navigationDock_->width()==66,"small dashboard retains accessible icon navigation");
    const QRect audioBounds(mixerDock_->mapTo(this,QPoint()),mixerDock_->size());
    for(auto action:{record_,replay_,save_}){
        const QRect bounds(action->mapTo(this,QPoint()),action->size());
        check(!bounds.intersects(audioBounds),"small-window recording/replay action stays outside audio strip "+action->objectName());
    }
    if(transparencySettings_->isVisible()){
        const QRect actionBounds(transparencySettings_->mapTo(this,QPoint()),transparencySettings_->size());
        check(!actionBounds.intersects(audioBounds)&&transparencySettings_->parentWidget()==statusBar(),"Windows transparency action remains in footer and cannot cover gain sliders");
    }
    check(QRect(time_->mapTo(this,QPoint()),time_->size()).bottom()<audioBounds.top(),"compact recording clock remains above audio strip");
    for(int kind=0;kind<5;++kind){
        quickGears_[kind]->click();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QRect bounds(mapToGlobal(QPoint()),size()),opener(quickGears_[kind]->mapToGlobal(QPoint()),quickGears_[kind]->size());
        check(quick_->isVisible()&&bounds.contains(QRect(quick_->mapToGlobal(QPoint()),quick_->size()))&&!opener.intersects(QRect(quick_->mapToGlobal(QPoint()),quick_->size())),"small-window quick placement "+QString::number(kind));
        auto area=quick_->findChild<QScrollArea*>("quickScroll");auto bar=area->verticalScrollBar();bar->setValue(bar->maximum());
        check(bar->maximum()==0||bar->value()==bar->maximum(),"every quick preference reachable in small viewport "+QString::number(kind));
        if(auto combo=quick_->findChild<QComboBox*>()){int value=combo->currentIndex();bar->setValue(0);QWheelEvent wheel(QPointF(10,10),QPointF(combo->mapToGlobal(QPoint(10,10))),QPoint(),QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(combo,&wheel);check(combo->currentIndex()==value&&(bar->maximum()==0||bar->value()>0),"quick wheel scroll leaves choice unchanged "+QString::number(kind));}
        if(kind==4)shot(quick_,"microphone-quick-small");
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(quick_,&escape);QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        check(!quick_->isVisible(),"small popover Escape dismisses "+QString::number(kind));
    }
    resize(original);QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    quickGears_[0]->click();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    QKeyEvent tab(QEvent::KeyPress,Qt::Key_Tab,Qt::NoModifier);QApplication::sendEvent(quick_,&tab);
    check(quick_->focusWidget()!=nullptr,"quick keyboard Tab targets an actual child editor");
    QPoint local(15,height()-30),global=mapToGlobal(local);QMouseEvent outside(QEvent::MouseButtonPress,local,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(this,&outside);
    check(!quick_->isVisible(),"outside targeted click dismisses popup without desktop input");
}

ULONGLONG renderCpuStart_{};
QElapsedTimer resourceClock_;
ULONGLONG processCpu(){FILETIME a{},b{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&a,&b,&kernel,&user);ULARGE_INTEGER k{},u{};k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;return k.QuadPart+u.QuadPart;}
void resourceStart(){renderCpuStart_=processCpu();resourceClock_.start();}
void resourceReport(const QString& mode){PROCESS_MEMORY_COUNTERS_EX memory{sizeof(memory)};GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));auto cpu=(processCpu()-renderCpuStart_)/10000.0;checkReport_.write(QString("RESOURCE %1 window=%2x%3 wall=%4 ms CPU=%5 ms one-core=%6%% private=%7 MiB working-set=%8 MiB; whole test process, DWM excluded\n").arg(mode).arg(width()).arg(height()).arg(resourceClock_.elapsed()).arg(cpu,0,'f',2).arg(cpu/std::max<qint64>(1,resourceClock_.elapsed())*100,0,'f',2).arg(memory.PrivateUsage/1048576.0,0,'f',2).arg(memory.WorkingSetSize/1048576.0,0,'f',2).toUtf8());}
