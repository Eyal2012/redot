#include "brand_state.h"
// Included beside App, not inside it. Compatibility controls are hidden state
// adapters only. No legacy workspace is shown by the Qt executable.
class FrontendController final : public FrontendBackend
{
    App app_;
    std::function<void(int,int,bool)> sceneAudioEdit_;
    HWND find(int id)
    {
        struct Find
        {
            int id;
            HWND result{};
        } f{id};
        EnumChildWindows(
            app_.hwnd_,
            [](HWND h, LPARAM p) -> BOOL {
                auto &f = *reinterpret_cast<Find *>(p);
                if (GetDlgCtrlID(h) == f.id)
                {
                    f.result = h;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&f));
        return f.result;
    }
    FrontendControl read(HWND h, std::wstring label = L"", int category = 0)
    {
        FrontendControl c;
        c.id = GetDlgCtrlID(h);
        c.category = category;
        c.label = std::move(label);
        c.enabled = IsWindowEnabled(h);
        c.text = App::uiText(h);
        wchar_t cls[64]{};
        GetClassNameW(h, cls, 64);
        const auto style = GetWindowLongPtrW(h, GWL_STYLE);
        if (wcscmp(cls, L"ComboBox") == 0)
        {
            c.kind = FrontendControlKind::Choice;
            c.editable = (style & 3) == CBS_DROPDOWN;
            c.selection = static_cast<int>(SendMessageW(h, CB_GETCURSEL, 0, 0));
            const auto count = SendMessageW(h, CB_GETCOUNT, 0, 0);
            for (int i = 0; i < count; ++i)
            {
                auto n = SendMessageW(h, CB_GETLBTEXTLEN, i, 0);
                std::wstring s(n + 1, L'\0');
                SendMessageW(h, CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(s.data()));
                s.resize(n);
                c.choices.push_back(std::move(s));
            }
        }
        else if (wcscmp(cls, L"Edit") == 0)
            c.kind = FrontendControlKind::Edit;
        else if (wcscmp(cls, L"Button") == 0)
        {
            c.kind =
                (style & BS_TYPEMASK) == BS_AUTOCHECKBOX ? FrontendControlKind::Toggle : FrontendControlKind::Action;
            c.checked = App::checked(h);
        }
        else if (wcscmp(cls, TRACKBAR_CLASSW) == 0)
        {
            c.kind = FrontendControlKind::Slider;
            c.value = static_cast<int>(SendMessageW(h, TBM_GETPOS, 0, 0));
        }
        else if (wcscmp(cls, PROGRESS_CLASSW) == 0)
        {
            c.kind = FrontendControlKind::Meter;
            c.value = static_cast<int>(SendMessageW(h, PBM_GETPOS, 0, 0));
        }
        else if (wcscmp(cls, HOTKEY_CLASSW) == 0)
        {
            c.kind = FrontendControlKind::Shortcut;
            c.value = static_cast<int>(SendMessageW(h, HKM_GETHOTKEY, 0, 0));
        }
        return c;
    }

  public:
    void onSceneAudioEdit(std::function<void(int,int,bool)> callback)override{sceneAudioEdit_=std::move(callback);}
    void scene(const Scenes::Snapshot& value) override {
        app_.selectedScene_=value;
        if(!app_.engine_.gpuResources()){app_.setStatus(L"D3D11 initialization failed; scene capture unavailable.");return;}
        if(!app_.sceneRuntime_) {
            app_.sceneRuntime_=std::make_unique<Scenes::Runtime>(app_.engine_.gpuResources(),app_.hwnd_,kFrameMessage);
            app_.engine_.sceneFrame=[this]{return app_.sceneRuntime_?app_.sceneRuntime_->latest():RecordedFrame{};};
        }
        app_.sceneRuntime_->configure(value,app_.videoConfig_);
        app_.sceneRuntime_->cursorEnabled(app_.engine_.cursorEnabled);
        app_.sceneRuntime_->borderPermission(app_.borderAccess_.result().permission);
    }
    Scenes::RuntimeStatus sceneStatus() override {auto status=app_.sceneRuntime_?app_.sceneRuntime_->status():Scenes::RuntimeStatus{};if(app_.sceneAudio_&&app_.recorder_.busy())for(auto& [id,text]:app_.sceneAudio_->status())status.sources[QString::fromStdWString(id)]=QString::fromStdWString(text);return status;}
    bool scenePreview(HWND window,const QRectF& viewport,const QVector<QPolygonF>& lines,const QVector<QPointF>& handles) override {
        return app_.sceneRuntime_&&app_.scenePresentation_.render(window,app_.engine_.gpuResources(),app_.sceneRuntime_->latest(),viewport,lines,handles);
    }
    QJsonObject legacySceneSettings() override {
        QJsonObject value;for(auto& [key,text]:app_.loadedSettings_)value[QString::fromStdString(key)]=QString::fromStdWString(text);return value;
    }
    FrontendController(HINSTANCE instance, HWND preview)
    {
        app_.externalPreview_ = preview;
        if (!app_.create(instance))
            throw std::runtime_error("Could not initialize ReDot controller");
        app_.engine_.setNotificationWindow(app_.hwnd_);
    }
    FrontendState state(bool diagnostics) override
    {
        FrontendState s;
        s.encoding = app_.recorder_.busy();
        if(auto input=app_.recorder_.ingress()){std::lock_guard lock(input->mutex);s.taskbarMode=static_cast<unsigned>(Branding::engineMode(app_.recorder_.recording(),input->stopping,input->encodedFrames.load(),app_.replayWorkerOnly_,app_.replayEnabled_));}
        s.encoderReady=app_.recorder_.recording();s.previewReady=app_.engine_.imageReady();
        s.recording = s.encoding && !app_.replayWorkerOnly_;
        s.replay = app_.replayEnabled_;
        s.capture = app_.engine_.active();
        s.closing = app_.closing_;
        s.recovery = app_.recoveryPending_;
        s.closeToTray = app_.closeToTray_;
        s.minimizeToTray = app_.minimizeToTray_;
        s.transparency = app_.transparency_;
        BOOL systemMotion=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&systemMotion,0);
        s.motion = app_.animations_&&systemMotion;
        s.previewEnabled = app_.previewVisible_;
        s.pendingStart=app_.borderAction_!=CaptureAction::None || app_.micAction_!=CaptureAction::None;
        s.borderStatus=app_.borderAccess_.result().text;
        s.gameEnabled=App::checked(app_.gameEnable_);s.micEnabled=App::checked(app_.microphoneEnable_);
        auto micIngress = app_.recorder_.busy() ? app_.recorder_.ingress() : app_.micMonitor_.ingress();
        if (micIngress)
        {
            s.micMeter=micIngress->rawMeters[1].snapshot(GetTickCount64());
            std::lock_guard lock(micIngress->mutex);
            s.micOpen=micIngress->audioSourceOpen[1];
            if (micIngress->audioSourceOpen[1])
                s.openedMic = micIngress->audioEndpointNames[1];
        }
        if(auto input=app_.recorder_.ingress();app_.recorder_.busy()&&input){
            s.gameMeter=input->rawMeters[0].snapshot(GetTickCount64());
            std::lock_guard lock(input->mutex);s.gameOpen=input->audioSourceOpen[0];s.openedGame=input->audioEndpointNames[0];
        }
        s.captureStatus = App::uiText(app_.status_);
        s.recordStatus = App::uiText(app_.recordStatus_);
        s.replayStatus = App::uiText(app_.replayStatus_);
        s.elapsed = App::uiText(app_.elapsed_);
        s.gameStatus = App::uiText(app_.gameSourceStatus_);
        s.micStatus = App::uiText(app_.micSourceStatus_);
        s.settingsStatus = App::uiText(app_.settingsStatus_);
        s.folder = app_.outputFolder_;
        s.selectedSource = App::uiText(app_.combo_);
        if(app_.sceneRuntime_){s.capture=app_.sceneRuntime_->active();s.previewReady=bool(app_.sceneRuntime_->latest().texture);for(auto& scene:app_.selectedScene_.scenes)if(scene.id==app_.selectedScene_.selected)s.selectedSource=scene.name.toStdWString();}
        if(app_.sceneRuntime_){s.gameEnabled=s.micEnabled=false;for(auto& scene:app_.selectedScene_.scenes)if(scene.id==app_.selectedScene_.selected)for(auto& a:scene.audio)for(auto& d:app_.selectedScene_.definitions)if(d.id==a.sourceId){if(d.kind==Scenes::Kind::Microphone)s.micEnabled=true;else s.gameEnabled=true;}}
        if(s.encoding&&!app_.videoConfig_.trackMix&&!app_.videoConfig_.trackGame&&!app_.videoConfig_.trackMicrophone)s.gameEnabled=s.micEnabled=false;
        s.videoDescription = app_.videoConfig_.description();
        s.gameLevel = static_cast<unsigned>(s.gameMeter.displayedRms*1000);
        s.micLevel = static_cast<unsigned>(s.micMeter.displayedRms*1000);
        s.pendingSaves = app_.replayStore_->pendingSaves();
        s.libraryRevision = app_.frontendLibraryRevision_;
        s.historySeconds = app_.replayStore_->availableSeconds();
        s.replayBytes = app_.replayStore_->memoryBytes();
        s.replayCap = app_.replayStore_->memoryLimitBytes();
        if (diagnostics)
            s.diagnostics = app_.diagnosticsReport();
        return s;
    }
    std::vector<FrontendControl> settings() override
    {
        std::vector<FrontendControl> rows;
        for (auto &r : app_.settingsRows_)
        {
            auto h = r.control ? r.control : r.label;
            const int id=GetDlgCtrlID(h);
            if(id==1001||id==1002||id==1003||id==1004||r.category==4)continue;
            rows.push_back(read(h, App::uiText(r.label), r.category));
        }
        return rows;
    }
    FrontendControl control(int id) override
    {
        if(app_.sceneRuntime_&&(id==1019||id==1020||id==1017||id==1018||id==1014||id==1016)){
            bool mic=id==1020||id==1018||id==1016;FrontendControl c;c.id=id;c.enabled=!app_.closing_;c.value=100;c.checked=false;c.kind=(id==1019||id==1020)?FrontendControlKind::Slider:(id==1017||id==1018)?FrontendControlKind::Toggle:FrontendControlKind::Text;
            c.label=mic?L"Microphone":L"Game / desktop audio";std::vector<std::wstring> names;
            for(auto& scene:app_.selectedScene_.scenes)if(scene.id==app_.selectedScene_.selected)for(auto& a:scene.audio)for(auto& d:app_.selectedScene_.definitions)if(d.id==a.sourceId&&(d.kind==Scenes::Kind::Microphone)==mic){if(names.empty()){c.value=int(a.gain*100);c.checked=a.muted;}names.push_back(d.name.toStdWString());}
            c.enabled=c.enabled&&!names.empty();c.text=names.empty()?L"No source assigned in this scene":names.size()==1?names.front():std::to_wstring(names.size())+L" sources assigned in Scenes";
            if(c.kind==FrontendControlKind::Toggle)c.label=L"Mute assigned sources";else if(c.kind==FrontendControlKind::Slider)c.label=L"Gain (%)";
            return c;
        }
        return read(find(id));
    }
    void edit(int id, const std::wstring &text, int value, bool checked) override
    {
        if(app_.sceneRuntime_&&(id==1017||id==1018||id==1019||id==1020)){if(sceneAudioEdit_)sceneAudioEdit_(id,value,checked);return;}
        auto h = find(id);
        if (!h || !IsWindowEnabled(h))
            return;
        auto c = read(h);
        switch (c.kind)
        {
        case FrontendControlKind::Choice:
            if (c.editable)
            {
                SetWindowTextW(h, text.c_str());
                SendMessageW(app_.hwnd_, WM_COMMAND, MAKEWPARAM(id, CBN_EDITCHANGE), reinterpret_cast<LPARAM>(h));
            }
            else
            {
                SendMessageW(h, CB_SETCURSEL, value, 0);
                SendMessageW(app_.hwnd_, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), reinterpret_cast<LPARAM>(h));
                if(id==1001&&app_.selectedTarget()&&IsWindow(app_.selectedTarget()))app_.setStatus(L"Selected source ready. Start Recording or Enable Replay.");
            }
            break;
        case FrontendControlKind::Toggle:
            App::check(h, checked);
            SendMessageW(app_.hwnd_, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(h));
            break;
        case FrontendControlKind::Slider:
            SendMessageW(h, TBM_SETPOS, TRUE, value);
            app_.markSettingsDirty();
            break;
        case FrontendControlKind::Shortcut:
            SendMessageW(h, HKM_SETHOTKEY, value, 0);
            app_.markSettingsDirty();
            break;
        default:
            SetWindowTextW(h, text.c_str());
            SendMessageW(app_.hwnd_, WM_COMMAND, MAKEWPARAM(id, EN_CHANGE), reinterpret_cast<LPARAM>(h));
            break;
        }
    }
    void action(int id) override
    {
        if(app_.sceneRuntime_&&(id==1004||id==1001))return;
        logLine(L"Qt frontend command=%d; source hwnd=%p; encoder busy=%d, replay=%d, capture=%d, pending border/mic=%d/%d.",id,app_.selectedTarget(),app_.recorder_.busy(),app_.replayEnabled_,app_.engine_.active(),int(app_.borderAction_),int(app_.micAction_));
        if (id == 1010)
        {
            app_.shortcut(2);
            return;
        }
        if (id == 1023)
        {
            app_.toggleReplay();
            return;
        }
        if (id == 1002 && app_.sceneRuntime_)
        {
            if(app_.sceneRuntime_->active())app_.stopCapture();else app_.startCapture();
            return;
        }
        SendMessageW(app_.hwnd_, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(find(id)));
    }
    void setFolder(const std::wstring &path) override
    {
        app_.outputFolder_ = path;
        SetWindowTextW(app_.folderLabel_, path.c_str());
        app_.markSettingsDirty();
        app_.galleryDirty_ = true;
    }
    void previewResize() override
    {
        app_.resize();
    }
    void validationError(const std::wstring &message) override
    {
        SetWindowTextW(app_.settingsStatus_, message.c_str());
    }
    void previewVisible(bool visible) override
    {
        app_.previewVisible_ = visible;
        App::check(app_.ui(App::PreviewCheck), visible);
        app_.markSettingsDirty();
        if (visible)
            app_.draw();
    }
    void requestLibrary() override
    {
        app_.library_.request(app_.outputFolder_);
        app_.galleryFolder_ = app_.outputFolder_;
        app_.galleryDirty_ = false;
    }
    const std::vector<ClipInfo> &clips() override
    {
        return app_.clips_;
    }
    void poll() override
    {
        if (alive())
            SendMessageW(app_.hwnd_, WM_TIMER, kHealthTimer, 0);
    }
    bool alive() const override
    {
        return IsWindow(app_.hwnd_);
    }
    void shutdown() override
    {
        app_.exitRequested_ = true;
        SendMessageW(app_.hwnd_, WM_CLOSE, 0, 0);
    }
    void onVisibilityShortcut(std::function<void()> fn) override
    {
        app_.frontendVisibility_ = std::move(fn);
    }
    void onShortcutNotice(std::function<void(const std::wstring &)> fn) override
    {
        app_.frontendNotice_ = std::move(fn);
        if (app_.frontendNotice_ && !app_.hotkeyError_.empty())
            app_.frontendNotice_(app_.hotkeyError_); // Registration ran before the tray existed.
    }
    bool previewReadback(std::vector<uint32_t> &pixels, unsigned &width, unsigned &height) override
    {
#ifdef REDOT_UI_VERIFICATION
        return app_.engine_.quietReadback(pixels, width, height);
#else
        (void)pixels;
        (void)width;
        (void)height;
        return false;
#endif
    }
    void generatedSource(unsigned mode) override
    {
#ifdef REDOT_UI_VERIFICATION
        app_.startQuietSource(mode);
#else
        (void)mode;
#endif
    }
#ifdef REDOT_UI_VERIFICATION
    bool verificationSettingsSettled() override {return !app_.settingsDirty_&&!app_.settingsStore_.busy();}
    uint64_t verificationWgcFrames() override {return app_.sceneRuntime_?app_.sceneRuntime_->status().sourceFrames:0;}
    uint64_t verificationEncodedFrames() override {auto input=app_.recorder_.ingress();return input?input->encodedFrames.load():0;}
    void verificationHotkey(unsigned action) override {if(action<6)SendMessageW(app_.hwnd_,WM_HOTKEY,100+action,0);}
    uint64_t verificationStartupSnapshots() override {auto input=app_.recorder_.ingress();return input?input->startupSnapshots.load():0;}
    void verificationInterrupt(bool fail) override{app_.forceRecoveryFailure_=fail;app_.interruptCapture(L"Controlled scene recovery failure.",true);}
    void verificationRecoveryFailure(bool fail) override{app_.forceRecoveryFailure_=fail;}
    unsigned verificationRecoveryAttempts() override{return app_.recoveryAttempts_;}
    unsigned verificationLiveMutes() override {unsigned result=0;for(auto& scene:app_.selectedScene_.scenes)if(scene.id==app_.selectedScene_.selected)for(auto& a:scene.audio)if(a.muted)for(auto& d:app_.selectedScene_.definitions)if(d.id==a.sourceId)result|=d.kind==Scenes::Kind::Microphone?2u:1u;return result;}
#endif
    std::wstring quietHotkeyProbe() override
    {
#ifdef REDOT_UI_VERIFICATION
        wchar_t ownName[128]{}, inputName[128]{}; DWORD bytes{};
        auto own=GetThreadDesktop(GetCurrentThreadId());
        auto input=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
        GetUserObjectInformationW(own,UOI_NAME,ownName,sizeof(ownName),&bytes);
        if(input){GetUserObjectInformationW(input,UOI_NAME,inputName,sizeof(inputName),&bytes);CloseDesktop(input);}
        if(wcsncmp(ownName,L"PandaClipQtQuiet_",17)!=0 || !inputName[0] || wcscmp(ownName,inputName)==0)
            return L"SKIPPED: private non-input desktop could not be positively verified";
        std::array<WORD,6> saved{};
        for(int i=0;i<6;++i){saved[i]=static_cast<WORD>(SendMessageW(app_.bindControls_[i],HKM_GETHOTKEY,0,0));SendMessageW(app_.bindControls_[i],HKM_SETHOTKEY,0,0);}
        const WORD binding=MAKEWORD(VK_F23,HOTKEYF_CONTROL|HOTKEYF_ALT|HOTKEYF_SHIFT);
        SendMessageW(app_.bindControls_[0],HKM_SETHOTKEY,binding,0);
        std::vector<std::wstring> notices; app_.frontendNotice_=[&notices](const std::wstring &t){notices.push_back(t);};
        app_.quietHotkeyRegistrationProbe_=true;app_.applyBindings();
        const bool registered=app_.bindingsActive_[0];
        // Regression: a registered shortcut reaches the window procedure as WM_HOTKEY; with replay off the user must get a notice, not silence.
        // The press path only reads replayEnabled_ and rewrites the replay status text; restore both so concurrent export checks still see "Saved clip:".
        const bool replayWas=app_.replayEnabled_;wchar_t statusWas[1024]{};if(app_.replayStatus_)GetWindowTextW(app_.replayStatus_,statusWas,1024);
        app_.replayEnabled_=false;
        SendMessageW(app_.hwnd_,WM_HOTKEY,100,0);
        app_.replayEnabled_=replayWas;if(app_.replayStatus_)SetWindowTextW(app_.replayStatus_,statusWas);
        const bool pressNoticed=notices.size()==1&&notices[0].find(L"replay buffer is off")!=std::wstring::npos;
        // Regression: changing the shortcut releases the old combination and registers the new one.
        SendMessageW(app_.bindControls_[0],HKM_SETHOTKEY,MAKEWORD(VK_F21,HOTKEYF_CONTROL|HOTKEYF_ALT|HOTKEYF_SHIFT),0);app_.applyBindings();
        const bool changed=app_.bindingsActive_[0]&&RegisterHotKey(app_.hwnd_,150,MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_NOREPEAT,VK_F23);
        if(changed)UnregisterHotKey(app_.hwnd_,150);
        SendMessageW(app_.bindControls_[0],HKM_SETHOTKEY,binding,0);app_.applyBindings();notices.clear();
        HWND peer=CreateWindowW(L"STATIC",L"Owned inactive shortcut conflict probe",WS_POPUP,0,0,1,1,nullptr,nullptr,app_.instance_,nullptr);
        const bool duplicate=peer&&RegisterHotKey(peer,101,MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_NOREPEAT,VK_F23);
        const DWORD conflictError=GetLastError();
        if(duplicate)UnregisterHotKey(peer,101);
        if(peer)DestroyWindow(peer);
        // Regression: a Save Clip shortcut already owned by another window must be reported, not silently inactive.
        const WORD heldBinding=MAKEWORD(VK_F22,HOTKEYF_CONTROL|HOTKEYF_ALT|HOTKEYF_SHIFT);
        HWND holder=CreateWindowW(L"STATIC",L"Owned inactive shortcut holder",WS_POPUP,0,0,1,1,nullptr,nullptr,app_.instance_,nullptr);
        const bool held=holder&&RegisterHotKey(holder,102,MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_NOREPEAT,VK_F22);
        SendMessageW(app_.bindControls_[0],HKM_SETHOTKEY,heldBinding,0);app_.applyBindings();
        const bool reported=held&&!app_.bindingsActive_[0]&&app_.hotkeyError_.find(L"failed to register")!=std::wstring::npos&&notices.size()==1;
        if(held)UnregisterHotKey(holder,102);
        if(holder)DestroyWindow(holder);
        for(int i=0;i<6;++i){UnregisterHotKey(app_.hwnd_,100+i);SendMessageW(app_.bindControls_[i],HKM_SETHOTKEY,saved[i],0);}
        app_.quietHotkeyRegistrationProbe_=false;app_.frontendNotice_=nullptr;app_.applyBindings();
        return registered&&pressNoticed&&changed&&peer&&!duplicate&&conflictError==ERROR_HOTKEY_ALREADY_REGISTERED&&reported ?
            L"PASS: production custom Ctrl+Alt+Shift+F23 registration, WM_HOTKEY press notice, shortcut change, owned-window conflict with visible registration-failure notice, then unregister/restore" :
            L"FAIL: isolated registration/conflict result registered="+std::to_wstring(registered)+L" pressNoticed="+std::to_wstring(pressNoticed)+L" changed="+std::to_wstring(changed)+L" duplicate="+std::to_wstring(duplicate)+L" error="+std::to_wstring(conflictError);
#else
        return L"Unavailable in shipping builds";
#endif
    }
};
