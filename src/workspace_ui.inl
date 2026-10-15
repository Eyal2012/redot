// App UI integration. Existing capture/record/replay methods remain the owners of their lifecycle.
enum
{
    NavCapture = 2000,
    NavClips,
    NavSettings,
    NavDiagnostics,
    ApplySettings = 2010,
    RestoreSettings,
    OpenOutput,
    SettingsStatus,
    CaptureSummary,
    WidthEdit = 2020,
    HeightEdit,
    FpsEdit,
    RateMode,
    BitrateEdit,
    MaxBitrateEdit,
    QpEdit,
    CursorCheck,
    PreviewCheck,
    PreviewRate,
    ResolutionPreset,
    GameSync,
    MicSync,
    MixTrack,
    GameTrack,
    MicTrack,
    SystemOutput,
    RefreshAudio,
    CloseToTray,
    MinimizeToTray,
    RecordingFormat, // 2040: ordinary-recording container (MKV default, MP4 alternative)
    ClipConfirmSound, // 2041: play a short confirmation after a replay clip is saved (default on)
    GalleryRefresh = 2050,
    GallerySearch,
    GallerySort,
    GalleryList,
    PlayerToggle,
    PlayerSeek,
    PlayerVolume,
    PlayerMute,
    PlayerFullscreen,
    PlayerTracks,
    PlayerExternal,
    LogsOpen,
    GalleryStatus,
    PlaybackStatus,
    BindApply = 2080,
    TrayOpen = 2090,
    TraySave,
    TrayRecord,
    TrayReplay,
    TrayExit,
    SettingsCategory = 2070, GalleryFilter, TransparencyCheck, AnimationCheck, AppearanceStatus,
    CaptureAudioToggle, CaptureDetailsToggle, CapturePreviewToggle,
    MainGameEnable = 2400, MainGameMode, MainMicEnable, MainMicDevice, MainGameMute, MainMicMute,
    MainGameVolume, MainMicVolume, MainGameMeter, MainMicMeter, MainMicCheck
};
struct UiRow
{
    HWND label{}, control{};
    int height{36};
    int category{2};
};
SettingsStore settingsStore_;
SettingsStore::Values loadedSettings_;
VideoConfig videoConfig_;
HWND pages_[4]{}, nav_[4]{}, settingsStatus_{}, captureSummary_{}, galleryStatus_{}, settingsAudioStatus_{},
    settingsContent_{};
HWND uiControls_[100]{}; // recording settings and gallery buttons are also addressable by ID
std::vector<UiRow> settingsRows_;
std::array<HWND, 6> bindControls_{}, bindStatus_{};
std::array<WORD, 6> bindings_{};
std::array<bool, 6> bindingsActive_{};
std::vector<AudioDeviceInfo> outputDevices_;
AudioDeviceCatalog deviceCatalog_;
AudioDeviceCatalog::Result pendingDevices_;
bool devicesPending_{}, audioRefreshRequested_{};
std::vector<ClipInfo> clips_;
ClipLibrary library_;
// Unfinished-MKV recovery: one background pass over the output folder at startup (see mkv_writer.h).
std::thread mkvRecoveryThread_;
std::atomic_bool mkvRecoveryCancel_{false}, mkvRecoveryFinished_{false};
std::vector<MkvRecoveryItem> mkvRecoveryItems_;
void startMkvRecovery()
{
    if (outputFolder_.empty() || mkvRecoveryThread_.joinable())
        return;
    mkvRecoveryFinished_ = false;
    const std::wstring folder = outputFolder_;
    try
    {
        mkvRecoveryThread_ = std::thread([this, folder] {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            try
            {
                mkvRecoveryItems_ = mkvRecoverOrphans(folder, &mkvRecoveryCancel_);
            }
            catch (...)
            {
            }
            mkvRecoveryFinished_ = true;
        });
    }
    catch (const std::system_error &)
    {
        logLine(L"MKV recovery worker could not start; unfinished recordings stay untouched.");
    }
}
void pollMkvRecovery()
{
    if (!mkvRecoveryThread_.joinable() || !mkvRecoveryFinished_)
        return;
    mkvRecoveryThread_.join();
    unsigned recovered = 0;
    for (const auto &item : mkvRecoveryItems_)
    {
        logLine(L"MKV recovery: %s -> %s", item.source.c_str(), item.note.c_str());
        if (item.recoveredOk)
        {
            ++recovered;
            logLine(L"MKV recovery: validated clip %s (%.1f s).", item.recovered.c_str(), item.seconds);
            library_.upsert(outputFolder_, item.recovered);
        }
    }
    mkvRecoveryItems_.clear();
    if (recovered)
    {
        galleryDirty_ = true;
        setStatus(L"Recovered " + std::to_wstring(recovered) + (recovered == 1 ? L" unfinished MKV recording" : L" unfinished MKV recordings") +
                  L" from an earlier session. Originals were kept.");
    }
}
std::wstring galleryFolder_;
HWND applyButton_{}, brand_{}, category_{}, categoryLabel_{}, mainAudio_[11]{};
int settingsCategory_{2};
bool transparency_{true}, animations_{true}, audioExpanded_{true}, detailsExpanded_{};
int page_{}, previewRate_{30};
bool settingsDirty_{};
std::chrono::steady_clock::time_point settingsDirtyAt_{};
unsigned replaySeconds_{60}, replayMiB_{512};
bool settingsAutosave_{true};
bool uiReady_{}, exitRequested_{}, closeToTray_{}, minimizeToTray_{}, clipSound_{true}, galleryDirty_{true}, settingsPending_{};
std::chrono::steady_clock::time_point lastPreviewPaint_{}, shutdownStarted_{};
NOTIFYICONDATAW tray_{};
std::array<HICON, 4> stateIcons_{};
std::wstring lastTrayTip_;
unsigned lastTrayIcon_{99};

static std::wstring uiText(HWND control)
{
    int n = GetWindowTextLengthW(control);
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(control, s.data(), n + 1);
    s.resize(n);
    return s;
}
static void setMeterChanged(HWND h, uint64_t value)
{
    value = std::min<uint64_t>(value, 1000);
    if (SendMessageW(h, PBM_GETPOS, 0, 0) != static_cast<LRESULT>(value))
        SendMessageW(h, PBM_SETPOS, value, 0);
}
static void setTextChanged(HWND h, const std::wstring &text)
{
    if (h && uiText(h) != text)
        SetWindowTextW(h, text.c_str());
}
void markSettingsDirty()
{
    if (!uiReady_ || !settingsAutosave_ || closing_)
        return;
    settingsDirty_ = true;
    settingsDirtyAt_ = std::chrono::steady_clock::now();
}
HWND ui(int id) const
{
    return id==ApplySettings?applyButton_:id >= 2020 && id < 2120 ? uiControls_[id - 2020] : nullptr;
}
static bool checked(HWND h)
{
    return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
}
static void check(HWND h, bool on)
{
    SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}
static int selected(HWND h)
{
    return static_cast<int>(SendMessageW(h, CB_GETCURSEL, 0, 0));
}
static void select(HWND h, int n)
{
    SendMessageW(h, CB_SETCURSEL, n, 0);
}
static int integer(const std::wstring &value, int min, int max)
{
    size_t used{};
    int n = std::stoi(value, &used);
    if (used != value.size() || n < min || n > max)
        throw std::runtime_error("range");
    return n;
}
int dip(int value) const
{
    return MulDiv(value, GetDpiForWindow(hwnd_), 96);
}
HWND control(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, int id)
{
    auto h =
        CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | (wcscmp(cls, L"STATIC") == 0 ? 0 : WS_TABSTOP) | style, 0,
                        0, 100, 32, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    if(id==ApplySettings)applyButton_=h;
    if (id >= 2020 && id < 2120)
        uiControls_[id - 2020] = h;
    return h;
}
HWND field(const wchar_t *label, int id, const wchar_t *value = L"", const wchar_t *cls = L"EDIT",
           DWORD style = WS_BORDER | ES_AUTOHSCROLL)
{
    auto h = control(pages_[2], cls, value, style, id);
    settingsRows_.push_back({control(pages_[2], L"STATIC", label, 0, 0), h});
    return h;
}
void existingField(const wchar_t *label, HWND h, int height = 36)
{
    SetParent(h, pages_[2]);
    settingsRows_.push_back({control(pages_[2], L"STATIC", label, 0, 0), h, height});
}
void heading(const wchar_t *title)
{
    settingsRows_.push_back({control(pages_[2], L"STATIC", title, 0, 0), nullptr, 44});
}
void fill(HWND h, std::initializer_list<const wchar_t *> items, int choice)
{
    SendMessageW(h, CB_RESETCONTENT, 0, 0);
    for (auto item : items)
        SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    select(h, choice);
}
static LRESULT CALLBACK pageProcedure(HWND h, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data)
{
    auto* self=reinterpret_cast<App*>(data);
    if(message==WM_COMMAND||message==WM_NOTIFY||message==WM_HSCROLL)
        return SendMessageW(self->hwnd_,message,w,l);
    return DefSubclassProc(h,message,w,l);
}

void applyAppearance()
{
    // Preferences are controller state only. DesktopBackdrop owns every visible Qt HWND.
    setTextChanged(ui(AppearanceStatus), transparency_ ? L"Native compositor appearance managed by Qt; availability shown in Appearance." : L"Solid appearance selected.");
}
void syncMainAudio()
{
    HWND original[]={gameEnable_,gameAudio_,microphoneEnable_,microphone_,gameMute_,microphoneMute_,gameVolume_,microphoneVolume_,gameMeter_,microphoneMeter_,micCheck_};
    for(int i=0;i<11;++i){
        auto dest=mainAudio_[i],src=original[i];if(!dest)continue;
        if(i==1 || i==3){
            int count=static_cast<int>(SendMessageW(src,CB_GETCOUNT,0,0));bool changed=count!=SendMessageW(dest,CB_GETCOUNT,0,0);
            for(int j=0;!changed&&j<count;++j){wchar_t a[4096]{},b[4096]{};SendMessageW(src,CB_GETLBTEXT,j,reinterpret_cast<LPARAM>(a));SendMessageW(dest,CB_GETLBTEXT,j,reinterpret_cast<LPARAM>(b));changed=wcscmp(a,b)!=0;}
            if(changed&&!SendMessageW(dest,CB_GETDROPPEDSTATE,0,0)){SendMessageW(dest,CB_RESETCONTENT,0,0);for(int j=0;j<count;++j){wchar_t text[4096]{};SendMessageW(src,CB_GETLBTEXT,j,reinterpret_cast<LPARAM>(text));SendMessageW(dest,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));}}
            if(selected(dest)!=selected(src))select(dest,selected(src));
        }else if(i<6){if(checked(dest)!=checked(src))check(dest,checked(src));}
        else if(i<8){auto pos=SendMessageW(src,TBM_GETPOS,0,0);if(SendMessageW(dest,TBM_GETPOS,0,0)!=pos)SendMessageW(dest,TBM_SETPOS,TRUE,pos);}
        else if(i<10)setMeterChanged(dest,static_cast<uint64_t>(SendMessageW(src,PBM_GETPOS,0,0)));
        else setTextChanged(dest,uiText(src));
        if(IsWindowEnabled(dest)!=IsWindowEnabled(src))EnableWindow(dest,IsWindowEnabled(src));
    }
}
void createWorkspaceUi()
{
    WNDCLASSW viewport{};
    viewport.lpfnWndProc = DefWindowProcW;
    viewport.hInstance = instance_;
    viewport.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    viewport.lpszClassName = L"PandaClipSettingsViewport";
    RegisterClassW(&viewport);
    for (int i = 0; i < 4; i++)
    {
        pages_[i] = CreateWindowExW(WS_EX_CONTROLPARENT, i == 2 ? L"PandaClipSettingsViewport" : L"STATIC", L"",
                                    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | (i == 2 ? WS_VSCROLL | WS_TABSTOP : 0), 0,
                                    0, 1, 1, hwnd_, nullptr, instance_, nullptr);
        SetWindowSubclass(pages_[i], pageProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
    }
    brand_ = control(hwnd_,L"STATIC",L"ReDot",0,0);
    const wchar_t *labels[] = {L"◉  Capture", L"▦  Clips", L"⚙  Settings", L"◎  Diagnostics"};
    for (int i = 0; i < 4; i++)
        nav_[i] = control(hwnd_, L"BUTTON", labels[i], BS_PUSHBUTTON, NavCapture + i);
    for (HWND h : {combo_, start_, stop_, refresh_, record_, stopRecording_, elapsed_, status_, recordStatus_,
                   replayToggle_, replaySave_, replayStatus_, preview_, gameSourceStatus_, micSourceStatus_})
        SetParent(h, pages_[0]);
    captureSummary_ = control(pages_[0], L"STATIC", L"", 0, CaptureSummary);
    ShowWindow(diagnosticsButton_, SW_HIDE);
    settingsStatus_ =
        control(pages_[2], L"STATIC",
                L"Apply validates settings. Encoder changes take effect at the next session; replay then warms again.",
                0, SettingsStatus);
    settingsRows_.push_back({settingsStatus_, nullptr, 64});
    existingField(L"Save changes", control(pages_[2], L"BUTTON", L"Apply settings", BS_PUSHBUTTON, ApplySettings));
    heading(L"RECORDING  |  H.264 SDR, GPU fit with letterboxing");
    existingField(L"Output folder", folderLabel_, 56);
    existingField(L"Choose output", chooseFolder_);
    existingField(L"Open output", control(pages_[2], L"BUTTON", L"Open Folder", BS_PUSHBUTTON, OpenOutput));
    field(L"Recording format", RecordingFormat, L"", L"COMBOBOX", CBS_DROPDOWNLIST);
    fill(ui(RecordingFormat), {L"MKV (recommended, recoverable after a crash)", L"MP4 (finalized only when recording stops)"}, 0);
    field(L"Resolution preset", ResolutionPreset, L"", L"COMBOBOX", CBS_DROPDOWNLIST);
    fill(ui(ResolutionPreset), {L"1280 x 720", L"1920 x 1080", L"2560 x 1440", L"Custom (even dimensions)"}, 1);
    field(L"Output width (pixels)", WidthEdit, L"1920");
    field(L"Output height (pixels)", HeightEdit, L"1080");
    field(L"Output FPS (30 / 60 / 120)", FpsEdit, L"60");
    existingField(L"NVENC quality preset", quality_);
    field(L"NVENC rate control", RateMode, L"", L"COMBOBOX", CBS_DROPDOWNLIST);
    fill(ui(RateMode), {L"VBR bitrate", L"CBR bitrate", L"CQP constant quantizer"}, 0);
    field(L"Bitrate (Mbps, 1-200)", BitrateEdit, L"20");
    field(L"VBR maximum (Mbps)", MaxBitrateEdit, L"35");
    field(L"CQP (0-51; lower = higher quality)", QpEdit, L"23");
    field(L"Mouse pointer", CursorCheck, L"Capture cursor", L"BUTTON", BS_AUTOCHECKBOX);
    field(L"Preview", PreviewCheck, L"Render preview", L"BUTTON", BS_AUTOCHECKBOX);
    check(ui(PreviewCheck), true);
    field(L"Preview refresh limit (FPS, 1-60)", PreviewRate, L"30");
    heading(L"REPLAY  |  Compressed packets, two pending saves maximum");
    SetWindowLongPtrW(replayDuration_, GWL_STYLE,
                      (GetWindowLongPtrW(replayDuration_, GWL_STYLE) & ~3) |
                          CBS_DROPDOWN); // recreated below so the editable child exists
    DestroyWindow(replayDuration_);
    replayDuration_ = control(pages_[2], L"COMBOBOX", L"", CBS_DROPDOWN | WS_VSCROLL, kReplayDurationCombo);
    fill(replayDuration_, {L"15", L"30", L"60", L"120", L"300"}, 2);
    existingField(L"Requested duration (seconds, 5-600)", replayDuration_);
    DestroyWindow(replayMemory_);
    replayMemory_ = control(pages_[2], L"COMBOBOX", L"", CBS_DROPDOWN | WS_VSCROLL, kReplayMemoryCombo);
    fill(replayMemory_, {L"128", L"256", L"512", L"1024", L"2048", L"4096"}, 2);
    existingField(L"RAM limit (MiB, 128-4096; 1024 MiB = 1 GiB)", replayMemory_, 52);
    heading(L"AUDIO  |  Positive sync delays; negative sync advances (ms)");
    existingField(L"Game capture enabled", gameEnable_);
    existingField(L"Game audio mode", gameAudio_);
    field(L"Whole-system output device", SystemOutput, L"", L"COMBOBOX", CBS_DROPDOWNLIST | WS_VSCROLL);
    existingField(L"Game gain (0-200%)", gameVolume_);
    existingField(L"Game mute (also works during capture)", gameMute_);
    existingField(L"Game input level", gameMeter_);
    field(L"Game sync (-500 to +500 ms)", GameSync, L"0");
    existingField(L"Microphone enabled", microphoneEnable_);
    existingField(L"Recording input device", microphone_);
    existingField(L"Refresh input/output list",
                  control(pages_[2], L"BUTTON", L"Refresh audio devices", BS_PUSHBUTTON, RefreshAudio));
    existingField(L"Test selected input before recording", micCheck_);
    existingField(L"Mic gain (0-200%)", microphoneVolume_);
    existingField(L"Mic mute (also works during capture)", microphoneMute_);
    existingField(L"Raw mic input level", microphoneMeter_);
    field(L"Mic sync (-500 to +500 ms)", MicSync, L"0");
    settingsAudioStatus_ = control(pages_[2], L"STATIC", L"", 0, 0);
    settingsRows_.push_back({settingsAudioStatus_, nullptr, 112});
    field(L"Convenient playback track", MixTrack, L"Mix", L"BUTTON", BS_AUTOCHECKBOX);
    field(L"Editing stem", GameTrack, L"Separate Game", L"BUTTON", BS_AUTOCHECKBOX);
    field(L"Editing stem", MicTrack, L"Separate Microphone", L"BUTTON", BS_AUTOCHECKBOX);
    for (int id : {MixTrack, GameTrack, MicTrack})
        check(ui(id), true);
    heading(L"SHORTCUTS  |  Press a key combination; Backspace clears");
    const wchar_t *actions[] = {
        L"Save Clip",        L"Toggle replay",      L"Start/stop recording", L"Toggle microphone mute",
        L"Toggle game mute", L"Show/hide ReDot"};
    for (int i = 0; i < 6; i++)
    {
        bindControls_[i] = control(pages_[2], HOTKEY_CLASSW, L"", WS_BORDER, 2200 + i);
        existingField(actions[i], bindControls_[i]);
        bindStatus_[i] = control(pages_[2], L"STATIC", L"Unbound", 0, 2300 + i);
        settingsRows_.push_back({bindStatus_[i], nullptr, 26});
    }
    bindings_[0] = MAKEWORD(VK_F9, HOTKEYF_CONTROL | HOTKEYF_SHIFT);
    SendMessageW(bindControls_[0], HKM_SETHOTKEY, bindings_[0], 0);
    existingField(L"Register shortcuts", control(pages_[2], L"BUTTON", L"Apply shortcuts", BS_PUSHBUTTON, BindApply));
    heading(L"WINDOW AND DEFAULTS");
    field(L"Closing the window", CloseToTray, L"Keep capture running in the tray", L"BUTTON", BS_AUTOCHECKBOX);
    field(L"Minimizing the window", MinimizeToTray, L"Hide in the system tray (default: off)", L"BUTTON", BS_AUTOCHECKBOX);
    field(L"Clip confirmation", ClipConfirmSound, L"Play clip confirmation sound", L"BUTTON", BS_AUTOCHECKBOX);
    existingField(L"Reset saved configuration",
                  control(pages_[2], L"BUTTON", L"Restore Defaults...", BS_PUSHBUTTON, RestoreSettings));
    for (HWND h : {gameVolumeLabel_, microphoneVolumeLabel_, gameMeterLabel_, microphoneMeterLabel_})
        ShowWindow(h, SW_HIDE);
    existingField(L"Windows capture permission", borderRequest_);
    existingField(L"Outline permission status", borderStatus_, 64);
    heading(L"APPEARANCE  |  Native backdrop with solid fallback");
    field(L"Transparency (default: on)",TransparencyCheck,L"Use Windows backdrop",L"BUTTON",BS_AUTOCHECKBOX);
    field(L"Hover transitions (default: on)",AnimationCheck,L"Subtle control animation",L"BUTTON",BS_AUTOCHECKBOX);
    check(ui(TransparencyCheck),true); check(ui(AnimationCheck),true);
    auto appearanceStatus=control(pages_[2],L"STATIC",L"",0,AppearanceStatus);
    settingsRows_.push_back({appearanceStatus,nullptr,72});
    category_=control(hwnd_,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,SettingsCategory);
    fill(category_,{L"General",L"Capture",L"Recording",L"Replay",L"Audio",L"Keybinds",L"Appearance",L"All settings"},2);
    categoryLabel_=control(hwnd_,L"STATIC",L"SETTINGS CATEGORY",0,0);
    int group=2;
    for(auto &row:settingsRows_) {
        if(!row.control && row.height==44) {
            auto text=uiText(row.label);
            group=text.starts_with(L"REPLAY")?3:text.starts_with(L"AUDIO")?4:text.starts_with(L"SHORTCUTS")?5:text.starts_with(L"WINDOW")?0:text.starts_with(L"APPEARANCE")?6:2;
        }
        row.category=group;
        int id=GetDlgCtrlID(row.control);
        if(id==CursorCheck || id==PreviewCheck || id==PreviewRate || row.control==borderRequest_ || row.control==borderStatus_)row.category=1;
        if(row.control==folderLabel_ || row.control==chooseFolder_ || id==OpenOutput)row.category=0;
    }
    // Pinned apply/status stay visible independently of category and scroll position.
    std::erase_if(settingsRows_,[&](const UiRow &row){if(row.label==settingsStatus_ || row.control==ui(ApplySettings)){if(row.control)DestroyWindow(row.label);return true;}return false;});
    SetParent(settingsStatus_,hwnd_);SetParent(ui(ApplySettings),hwnd_);
    const wchar_t *audioText[]={L"Game",L"",L"Microphone",L"",L"Mute",L"Mute",L"",L"",L"",L"",L"Test mic"};
    for(int i=0;i<11;++i){
        const wchar_t *cls=(i==1||i==3)?L"COMBOBOX":(i==6||i==7)?TRACKBAR_CLASSW:(i==8||i==9)?PROGRESS_CLASSW:L"BUTTON";
        DWORD style=(i==1||i==3)?CBS_DROPDOWNLIST|WS_VSCROLL:(i==6||i==7)?TBS_NOTICKS:(i==8||i==9)?0:(i==10)?BS_PUSHBUTTON:BS_AUTOCHECKBOX;
        mainAudio_[i]=control(pages_[0],cls,audioText[i],style,MainGameEnable+i);
        if(i==6||i==7){SendMessageW(mainAudio_[i],TBM_SETRANGE,TRUE,MAKELONG(0,200));SendMessageW(mainAudio_[i],TBM_SETPOS,TRUE,100);}
        if(i==8||i==9){SendMessageW(mainAudio_[i],PBM_SETRANGE32,0,1000);SendMessageW(mainAudio_[i],PBM_SETBARCOLOR,0,RGB(45,195,208));SetWindowTheme(mainAudio_[i],L"",L"");}
    }
    fill(mainAudio_[1],{L"Off",L"Selected application",L"Whole system (explicit)"},1);
    control(pages_[0],L"BUTTON",L"Audio mixer",BS_PUSHBUTTON,CaptureAudioToggle);
    control(pages_[0],L"BUTTON",L"Source details",BS_PUSHBUTTON,CaptureDetailsToggle);
    control(pages_[0],L"BUTTON",L"Hide preview",BS_PUSHBUTTON,CapturePreviewToggle);
    showDiagnostics();
    ShowWindow(diagnostics_, SW_HIDE);
    SetParent(diagnosticsText_, pages_[3]);
    SetParent(diagnosticsStatus_, pages_[3]);
    SetParent(GetDlgItem(diagnostics_, kReportButton), pages_[3]);
    control(pages_[3], L"BUTTON", L"Open logs folder", BS_PUSHBUTTON, LogsOpen);
    settingsContent_ =
        CreateWindowExW(WS_EX_CONTROLPARENT, L"PandaClipSettingsViewport", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                        0, 0, 1, 1, pages_[2], nullptr, instance_, nullptr);
    SetWindowSubclass(settingsContent_, pageProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
    for (HWND h = GetWindow(pages_[2], GW_CHILD), next; h; h = next)
    {
        next = GetWindow(h, GW_HWNDNEXT);
        if (h != settingsContent_)
            SetParent(h, settingsContent_);
    }
    fill(ui(SystemOutput), {L"Default output (pinned at session start)"}, 0);
    refreshOutputDevices();
    std::wstring warning;
    loadedSettings_ = settingsStore_.load(warning);
    restoreUi(loadedSettings_);
    if (!warning.empty())
    {
        settingsAutosave_ = false;
        SetWindowTextW(settingsStatus_, warning.c_str());
    }
    uiReady_ = true;
    startMkvRecovery();
    applyBindings();
    switchPage(0);
    SetWindowTextW(hwnd_, L"ReDot");
}
void refreshOutputDevices()
{
    deviceCatalog_.request();
}
void applyOutputDevices(std::vector<AudioDeviceInfo> devices, const std::wstring &error)
{
    auto saved = selectedOutputId();
    outputDevices_ = std::move(devices);
    SendMessageW(ui(SystemOutput), WM_SETREDRAW, FALSE, 0);
    SendMessageW(ui(SystemOutput), CB_RESETCONTENT, 0, 0);
    SendMessageW(ui(SystemOutput), CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Default output (pinned at session start)"));
    int choice = 0;
    for (size_t i = 0; i < outputDevices_.size(); i++)
    {
        SendMessageW(ui(SystemOutput), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(outputDevices_[i].name.c_str()));
        if (outputDevices_[i].id == saved)
            choice = static_cast<int>(i + 1);
    }
    if (!saved.empty() && !choice)
    {
        outputDevices_.push_back({saved, L"Selected output device (unavailable)"});
        SendMessageW(ui(SystemOutput), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(outputDevices_.back().name.c_str()));
        choice = static_cast<int>(outputDevices_.size());
    }
    select(ui(SystemOutput), choice);
    SendMessageW(ui(SystemOutput), WM_SETREDRAW, TRUE, 0);
    InvalidateRect(ui(SystemOutput), nullptr, FALSE);
    if (!error.empty())
        setTextChanged(settingsStatus_, error);
}
void updateDeviceLists()
{
    if (deviceCatalog_.take(pendingDevices_))
        devicesPending_ = true;
    if (!devicesPending_ || recorder_.busy())
        return;
    if (SendMessageW(microphone_, CB_GETDROPPEDSTATE, 0, 0) ||
        SendMessageW(mainAudio_[3], CB_GETDROPPEDSTATE, 0, 0) ||
        SendMessageW(ui(SystemOutput), CB_GETDROPPEDSTATE, 0, 0))
        return;
    devicesPending_ = false;
    applyMicrophones(std::move(pendingDevices_.inputs), pendingDevices_.inputError);
    applyOutputDevices(std::move(pendingDevices_.outputs), pendingDevices_.outputError);
    if (audioRefreshRequested_)
    {
        audioRefreshRequested_ = false;
        setTextChanged(settingsStatus_, pendingDevices_.inputError.empty() && pendingDevices_.outputError.empty()
                           ? L"Audio device lists refreshed. Selected endpoint IDs preserved; unavailable devices remain selected."
                           : L"Audio device refresh completed with errors: " + pendingDevices_.inputError + L" " + pendingDevices_.outputError);
    }
}
std::wstring selectedOutputId()
{
    int i = selected(ui(SystemOutput));
    return i > 0 && static_cast<size_t>(i) <= outputDevices_.size() ? outputDevices_[i - 1].id : L"";
}
void restoreUi(const SettingsStore::Values &values)
{
    auto text = [&](const char *key, HWND h) {
        auto it = values.find(key);
        if (it != values.end())
            SetWindowTextW(h, it->second.c_str());
    };
    const std::pair<const char *, int> fields[] = {
        {"width", WidthEdit},         {"height", HeightEdit},         {"fps", FpsEdit},
        {"bitrate", BitrateEdit},     {"maxBitrate", MaxBitrateEdit}, {"qp", QpEdit},
        {"previewRate", PreviewRate}, {"gameSync", GameSync},         {"micSync", MicSync}};
    for (auto [key, id] : fields)
        text(key, ui(id));
    text("duration", replayDuration_);
    text("memoryMiB", replayMemory_);
    auto number = [&](const char *key, int fallback, int min, int max) {
        try
        {
            auto it = values.find(key);
            return it == values.end() ? fallback : integer(it->second, min, max);
        }
        catch (...)
        {
            return fallback;
        }
    };
    // The production palette is fixed by design-system.json. Historical
    // theme/materialMode/glassPreset/tint keys are intentionally never read.
    // Retain only these accessibility preferences; collectSettings writes a
    // whitelist of functional keys so legacy styles cannot return on Apply.
    transparency_=number("transparency",1,0,1);animations_=number("animations",1,0,1);
    audioExpanded_=number("audioExpanded",1,0,1);detailsExpanded_=number("detailsExpanded",0,0,1);
    check(ui(TransparencyCheck),transparency_);check(ui(AnimationCheck),animations_);
    select(quality_, number("quality", 0, 0, 1));
    select(gameAudio_, number("gameMode", 1, 0, 2));
    select(ui(RateMode), number("rate", 0, 0, 2));
    select(ui(RecordingFormat), number("recordFormat", 0, 0, 1)); // absent/invalid -> MKV
    check(gameEnable_, number("gameEnabled", 1, 0, 1));
    check(microphoneEnable_, number("micEnabled", 0, 0, 1));
    check(gameMute_, number("gameMute", 0, 0, 1));
    check(microphoneMute_, number("micMute", 0, 0, 1));
    SendMessageW(gameVolume_, TBM_SETPOS, TRUE, number("gameGain", 100, 0, 200));
    SendMessageW(microphoneVolume_, TBM_SETPOS, TRUE, number("micGain", 100, 0, 200));
    for (auto [key, id] : {std::pair{"cursor", CursorCheck},
                           {"preview", PreviewCheck},
                           {"mixTrack", MixTrack},
                           {"gameTrack", GameTrack},
                           {"micTrack", MicTrack},
                           {"closeTray", CloseToTray},
                           {"minimizeTray", MinimizeToTray},
                           {"clipSound", ClipConfirmSound}})
        check(ui(id), number(key, (id == CursorCheck || id == CloseToTray || id == MinimizeToTray) ? 0 : 1, 0, 1));
    if (auto it = values.find("folder"); it != values.end() && !it->second.empty())
        outputFolder_ = it->second;
    SetWindowTextW(folderLabel_, outputFolder_.c_str());
    auto restoreDevice = [&](const char *key, HWND h, std::vector<AudioDeviceInfo> &list) {
        auto it = values.find(key);
        if (it == values.end() || it->second.empty())
            return;
        int choice = 0;
        for (size_t i = 0; i < list.size(); i++)
            if (list[i].id == it->second)
                choice = static_cast<int>(i + 1);
        if (!choice)
        {
            list.push_back({it->second, L"Selected device (unavailable)"});
            choice = static_cast<int>(list.size());
            SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(list.back().name.c_str()));
        }
        SendMessageW(h, CB_SETITEMDATA, choice, choice);
        select(h, choice);
    };
    restoreDevice("micId", microphone_, microphones_);
    restoreDevice("outputId", ui(SystemOutput), outputDevices_);
    for (int i = 0; i < 6; i++)
    {
        auto key = "binding" + std::to_string(i);
        bindings_[i] =
            static_cast<WORD>(number(key.c_str(), i ? 0 : MAKEWORD(VK_F9, HOTKEYF_CONTROL | HOTKEYF_SHIFT), 0, 65535));
        SendMessageW(bindControls_[i], HKM_SETHOTKEY, bindings_[i], 0);
    }
    if (values.contains("sourceExe") && values.contains("sourceTitle"))
    {
        select(combo_, -1);
        for (size_t i = 0; i < windows_.size(); i++)
            if (windows_[i].title == values.at("sourceTitle") &&
                executableFor(windows_[i].handle) == values.at("sourceExe"))
                select(combo_, static_cast<int>(i));
        if (selected(combo_) < 0)
            setStatus(L"Saved source is unavailable. Select its reopened window; capture has not started.");
    }
    if (!applySettings(false))
    {
        videoConfig_ = {};
        SetWindowTextW(ui(WidthEdit), L"1920");
        SetWindowTextW(ui(HeightEdit), L"1080");
        SetWindowTextW(ui(FpsEdit), L"60");
        SetWindowTextW(ui(BitrateEdit), L"20");
        SetWindowTextW(ui(MaxBitrateEdit), L"35");
        SetWindowTextW(ui(QpEdit), L"23");
        SetWindowTextW(ui(PreviewRate), L"30");
        SetWindowTextW(ui(GameSync), L"0");
        SetWindowTextW(ui(MicSync), L"0");
        SetWindowTextW(replayDuration_, L"60");
        SetWindowTextW(replayMemory_, L"512");
        applySettings(false);
        SetWindowTextW(settingsStatus_, L"Invalid saved numeric settings replaced with safe defaults.");
    }
    if(uiReady_){applyAppearance();setTextChanged(ui(CapturePreviewToggle),previewVisible_?L"Hide preview":L"Show preview");shellLayout();}
    if (values.contains("windowWidth"))
    {
        int w = number("windowWidth", 1100, 640, 6000), h = number("windowHeight", 820, 480, 4000),
            x = number("windowX", 100, -20000, 20000), y = number("windowY", 100, -20000, 20000);
        RECT r{x, y, x + w, y + h};
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &monitor);
        w = std::min<int>(w, monitor.rcWork.right - monitor.rcWork.left);
        h = std::min<int>(h, monitor.rcWork.bottom - monitor.rcWork.top);
        SetWindowPos(hwnd_, nullptr, std::clamp<int>(x, monitor.rcWork.left, monitor.rcWork.right - w),
                     std::clamp<int>(y, monitor.rcWork.top, monitor.rcWork.bottom - h), w, h, SWP_NOZORDER);
    }
}
SettingsStore::Values collectSettings()
{
    SettingsStore::Values v;
    auto n = [&](const char *key, auto value) { v[key] = std::to_wstring(value); };
    n("width", videoConfig_.width);
    n("height", videoConfig_.height);
    n("fps", videoConfig_.fps);
    n("rate", videoConfig_.rateMode);
    n("recordFormat", static_cast<unsigned>(videoConfig_.container));
    n("bitrate", videoConfig_.bitrateMbps);
    n("maxBitrate", videoConfig_.maxBitrateMbps);
    n("qp", videoConfig_.qp);
    n("quality", selected(quality_));
    n("previewRate", previewRate_);
    n("duration", replaySeconds_);
    n("memoryMiB", replayMiB_);
    n("gameMode", selected(gameAudio_));
    n("gameEnabled", checked(gameEnable_));
    n("micEnabled", checked(microphoneEnable_));
    n("gameMute", checked(gameMute_));
    n("micMute", checked(microphoneMute_));
    n("gameGain", SendMessageW(gameVolume_, TBM_GETPOS, 0, 0));
    n("micGain", SendMessageW(microphoneVolume_, TBM_GETPOS, 0, 0));
    n("gameSync", gameSyncMilliseconds_);
    n("micSync", microphoneSyncMilliseconds_);
    n("cursor", checked(ui(CursorCheck)));
    n("preview", previewVisible_);
    n("mixTrack", videoConfig_.trackMix);
    n("gameTrack", videoConfig_.trackGame);
    n("micTrack", videoConfig_.trackMicrophone);
    n("closeTray", closeToTray_);
    n("minimizeTray", minimizeToTray_);
    n("clipSound", clipSound_);
    n("transparency",transparency_);n("animations",animations_);
    n("audioExpanded",audioExpanded_);n("detailsExpanded",detailsExpanded_);
    v["folder"] = outputFolder_;
    v["micId"] = selectedMicrophoneId();
    v["outputId"] = selectedOutputId();
    for (int i = 0; i < 6; i++)
        v["binding" + std::to_string(i)] = std::to_wstring(bindings_[i]);
    // Legacy identity is read only for migration. The selected scene owns new targets.
    for (auto key : {"sourceExe", "sourceTitle"})
        if (loadedSettings_.contains(key)) v[key] = loadedSettings_[key];
    WINDOWPLACEMENT placement{sizeof(placement)};
    GetWindowPlacement(hwnd_, &placement);
    auto r = placement.rcNormalPosition;
    n("windowX", r.left);
    n("windowY", r.top);
    n("windowWidth", r.right - r.left);
    n("windowHeight", r.bottom - r.top);
    return v;
}
bool applySettings(bool save = true)
{
    try
    {
        VideoConfig next;
        next.width = integer(uiText(ui(WidthEdit)), 320, 3840);
        next.height = integer(uiText(ui(HeightEdit)), 180, 2160);
        next.fps = integer(uiText(ui(FpsEdit)), 30, 120);
        next.rateMode = selected(ui(RateMode));
        next.container = containerFromSetting(static_cast<unsigned>(std::max(0, selected(ui(RecordingFormat)))));
        next.bitrateMbps = integer(uiText(ui(BitrateEdit)), 1, 200);
        next.maxBitrateMbps = integer(uiText(ui(MaxBitrateEdit)), 1, 200);
        next.qp = integer(uiText(ui(QpEdit)), 0, 51);
        next.trackMix = checked(ui(MixTrack));
        next.trackGame = checked(ui(GameTrack));
        next.trackMicrophone = checked(ui(MicTrack));
        std::wstring error;
        if (!next.validate(error))
        {
            SetWindowTextW(settingsStatus_, error.c_str());
            return false;
        }
        auto seconds = integer(uiText(replayDuration_), 5, 600), memory = integer(uiText(replayMemory_), 128, 4096),
             rate = integer(uiText(ui(PreviewRate)), 1, 60), gameSync = integer(uiText(ui(GameSync)), -500, 500),
             micSync = integer(uiText(ui(MicSync)), -500, 500);
        if (replayStore_->pendingSaves() && memory != static_cast<int>(replayMiB_))
        {
            SetWindowTextW(settingsStatus_, L"Wait for pending clip saves before changing the memory limit.");
            return false;
        }
        videoConfig_ = next;
        select(ui(ResolutionPreset), next.width == 1280 && next.height == 720    ? 0
                                     : next.width == 1920 && next.height == 1080 ? 1
                                     : next.width == 2560 && next.height == 1440 ? 2
                                                                                 : 3);
        replaySeconds_ = seconds;
        replayMiB_ = memory;
        previewRate_ = rate;
        gameSyncMilliseconds_ = gameSync;
        microphoneSyncMilliseconds_ = micSync;
        previewVisible_ = checked(ui(PreviewCheck));
        closeToTray_ = checked(ui(CloseToTray));
        minimizeToTray_ = checked(ui(MinimizeToTray));
        clipSound_ = checked(ui(ClipConfirmSound));
        engine_.cursorEnabled = checked(ui(CursorCheck));
        if(sceneRuntime_)sceneRuntime_->cursorEnabled(engine_.cursorEnabled);
        replayStore_->setHistorySeconds(replaySeconds_);
        replayStore_->setMemoryLimitBytes(uint64_t(replayMiB_) * 1048576);
        settingsPending_ = recorder_.busy();
        if (!recorder_.busy())
            replayStore_->clearHistory();
        SetWindowTextW(settingsStatus_,
                       settingsPending_ ? L"Saved for the next encoder session. Current recording/replay keeps its "
                                          L"format; stop and restart to apply. New replay history will warm again."
                                        : L"Settings applied. Encoder/audio changes apply next session; cursor applies "
                                          L"on next capture start. Replay history reset; new capture starts warming.");
        if (save)
        {
            settingsAutosave_ = true;
            settingsStore_.save(collectSettings());
        }
        return true;
    }
    catch (...)
    {
        SetWindowTextW(settingsStatus_,
                       L"Invalid number. Duration: 5-600 seconds; RAM: 128-4096 MiB; even video dimensions: 320-3840 "
                       L"by 180-2160; FPS: 30/60/120; preview: 1-60; sync: -500 to +500 ms.");
        return false;
    }
}
void applyBindings()
{
    bool skipQuietBindings=diagnosticSetting(L"PANDACLIP_TEST_QUIET_UI");
#ifdef REDOT_UI_VERIFICATION
    skipQuietBindings=skipQuietBindings&&!quietHotkeyRegistrationProbe_;
#endif
    if(skipQuietBindings){
        for(int i=0;i<6;++i){UnregisterHotKey(hwnd_,100+i);bindings_[i]=static_cast<WORD>(SendMessageW(bindControls_[i],HKM_GETHOTKEY,0,0));bindingsActive_[i]=false;setTextChanged(bindStatus_[i],L"Not registered in isolated quiet tests");}
        return;
    }
    for (int i = 0; i < 6; i++)
    {
        UnregisterHotKey(hwnd_, 100 + i);
        bindingsActive_[i] = false;
        bindings_[i] = static_cast<WORD>(SendMessageW(bindControls_[i], HKM_GETHOTKEY, 0, 0));
    }
    hotkeyError_.clear();
    for (int i = 0; i < 6; i++)
    {
        WORD binding = bindings_[i];
        UINT key = LOBYTE(binding), flags = HIBYTE(binding);
        std::wstring status = L"Unbound";
        bool duplicate = false;
        for (int j = 0; j < 6; j++)
            if (j != i && binding && bindings_[j] == binding)
                duplicate = true;
        if (duplicate)
            status = L"Conflict: another ReDot action uses this shortcut.";
        else if (key)
        {
            if (key == VK_F12 || key == VK_LWIN || key == VK_RWIN || (!flags && (key < VK_F1 || key > VK_F24)))
                status =
                    L"Unsupported/reserved. Use Ctrl, Alt or Shift; unmodified function keys except F12 are allowed.";
            else
            {
                UINT mods = MOD_NOREPEAT | ((flags & HOTKEYF_CONTROL) ? MOD_CONTROL : 0) |
                            ((flags & HOTKEYF_SHIFT) ? MOD_SHIFT : 0) | ((flags & HOTKEYF_ALT) ? MOD_ALT : 0);
                bindingsActive_[i] = !!RegisterHotKey(hwnd_, 100 + i, mods, key);
                const DWORD registerError = bindingsActive_[i] ? 0 : GetLastError();
                logLine(L"Shortcut %d %s: key=0x%02X modifiers=0x%02X error=%lu%s.", i, bindingsActive_[i] ? L"registered" : L"registration failed", key, flags, registerError, registerError == ERROR_HOTKEY_ALREADY_REGISTERED ? L" (already registered by another program or another ReDot window)" : L"");
                if (i == 0 && !bindingsActive_[i])
                    hotkeyError_ = std::wstring(L"Save Clip shortcut failed to register: ") + (registerError == ERROR_HOTKEY_ALREADY_REGISTERED ? L"already used by another program or another ReDot window. " : L"Windows rejected it. ") + L"Change it in Settings > Shortcuts.";
                status = bindingsActive_[i]
                             ? L"Active globally (held keys do not repeat)"
                             : L"Not active: Windows reserved this combination or another app registered it.";
            }
        }
        SetWindowTextW(bindStatus_[i], status.c_str());
    }
    if (!hotkeyError_.empty() && frontendNotice_)
        frontendNotice_(hotkeyError_);
    if (!replayEnabled_)
        setReplayStatus(bindingsActive_[0]
                            ? L"Replay disabled. Save Clip shortcut is active."
                            : L"Replay disabled. Save Clip shortcut is not active; check Settings > Shortcuts.");
    if (uiReady_ && settingsAutosave_)
        settingsStore_.save(collectSettings());
}
void shortcut(unsigned action)
{
    if (closing_)
        return;
    if (action == 0)
    {
        if (!replayEnabled_)
            shortcutNotice(L"Save Clip shortcut pressed, but the replay buffer is off. Enable Replay Buffer in ReDot first.");
        saveReplayClip();
    }
    else if (action == 1)
        toggleReplay();
    else if (action == 2)
    {
        if (recorder_.busy() && !replayWorkerOnly_)
            stopRecording();
        else
            startRecording();
    }
    else if (action == 3 || action == 4)
    {
        HWND h = action == 3 ? microphoneMute_ : gameMute_;
        check(h, !checked(h));
        liveMutes();
    }
    else if (action == 5)
    {
        if(frontendVisibility_)frontendVisibility_();
        return;
    }
}
void liveMutes()
{
    markSettingsDirty();
    if (auto input = recorder_.ingress())
    {
        input->liveGameMute = checked(gameMute_);
        input->liveMicMute = checked(microphoneMute_);
    }
    updateAudioSources();
}
static HICON stateIcon(COLORREF color)
{
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), 32, -32, 1, 32, BI_RGB};
    void *pixels{};
    HDC dc = CreateCompatibleDC(nullptr);
    auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap)
    {
        DeleteDC(dc);
        return nullptr;
    }
    auto data = static_cast<uint32_t *>(pixels);
    std::fill_n(data, 1024, 0u);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            if ((x - 16) * (x - 16) + (y - 16) * (y - 16) <= 14 * 14)
                data[y * 32 + x] =
                    0xff000000u | uint32_t(GetRValue(color)) << 16 | uint32_t(GetGValue(color)) << 8 | GetBValue(color);
    auto old = SelectObject(dc, bitmap);
    auto font = CreateFontW(-23, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto oldFont = SelectObject(dc, font);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    RECT r{0, 1, 32, 32};
    DrawTextW(dc, L"P", 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldFont);
    DeleteObject(font);
    SelectObject(dc, old);
    DeleteDC(dc);
    for (int i = 0; i < 1024; ++i)
        if (data[i] & 0xffffff)
            data[i] |= 0xff000000;
    BYTE maskBits[128]{};
    auto mask = CreateBitmap(32, 32, 1, 1, maskBits);
    ICONINFO iconInfo{TRUE, 0, 0, mask, bitmap};
    auto icon = CreateIconIndirect(&iconInfo);
    DeleteObject(mask);
    DeleteObject(bitmap);
    return icon;
}
void setupTray()
{
    if(diagnosticSetting(L"PANDACLIP_TEST_QUIET_UI"))return;
    tray_.cbSize = sizeof(tray_);
    tray_.hWnd = hwnd_;
    tray_.uID = 1;
    tray_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray_.uCallbackMessage = WM_APP + 40;
    stateIcons_ = {stateIcon(RGB(100, 112, 128)), stateIcon(RGB(216, 60, 65)), stateIcon(RGB(36, 170, 113)),
                   stateIcon(RGB(224, 153, 50))};
    tray_.hIcon = stateIcons_[0] ? stateIcons_[0] : LoadIconW(nullptr, IDI_APPLICATION);
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(tray_.hIcon));
    wcscpy_s(tray_.szTip, L"ReDot - idle");
    Shell_NotifyIconW(NIM_ADD, &tray_);
}
void trayMenu()
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, tray_.szTip);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, TrayOpen, L"Open ReDot");
    AppendMenuW(menu, MF_STRING, TraySave, L"Save Clip");
    AppendMenuW(menu, MF_STRING, TrayRecord,
                recorder_.busy() && !replayWorkerOnly_ ? L"Stop recording" : L"Start recording");
    AppendMenuW(menu, MF_STRING, TrayReplay, replayEnabled_ ? L"Disable replay" : L"Enable replay");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, TrayExit, L"Exit and finalize");
    POINT p{};
    GetCursorPos(&p);
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
}
void gallerySaved(const std::wstring &path)
{
    if (path.empty() || galleryFolder_ != outputFolder_)
    {
        galleryDirty_ = true;
        return;
    }
    library_.upsert(outputFolder_, path);
}
void switchPage(int page)
{
    page_ = page;
    for (int i = 0; i < 4; i++)
        ShowWindow(pages_[i], i == page ? SW_SHOW : SW_HIDE);
    if (page == 1 && (galleryDirty_ || galleryFolder_ != outputFolder_))
    {
        library_.request(outputFolder_);
        galleryFolder_ = outputFolder_;
        galleryDirty_ = false;
        SetWindowTextW(galleryStatus_, L"Reading metadata and thumbnails in the background...");
    }
    if (page == 3)
        setTextChanged(diagnosticsText_, diagnosticsReport().c_str());
    syncMainAudio();
    shellLayout();
    for(auto nav:nav_)InvalidateRect(nav,nullptr,FALSE);
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void layoutSettings() {} // Hidden state controls have no presentation/layout.
void workspaceTick()
{
    if (!uiReady_)
        return;
    pollMkvRecovery();
    updateDeviceLists();
    if(page_==0)syncMainAudio();
    if (page_ == 3)
        setTextChanged(diagnosticsText_, diagnosticsReport().c_str());
    auto active = recorder_.ingress();
    auto summary = (recorder_.busy() && active ? active->video : videoConfig_).description() + L" | clip " +
                   std::to_wstring(replaySeconds_) + L" s" +
                   (settingsPending_ && recorder_.busy() ? L" | Settings pending restart" : L"");
    setTextChanged(captureSummary_, summary.c_str());
    auto audioSummary = uiText(gameSourceStatus_) + L"\r\n" + uiText(micSourceStatus_) + L"\r\nSelected tracks: " +
                        (videoConfig_.trackMix ? std::wstring(L"Mix ") : L"") +
                        (videoConfig_.trackGame ? L"Game (if enabled) " : L"") +
                        (videoConfig_.trackMicrophone ? L"Mic (if enabled)" : L"");
    setTextChanged(settingsAudioStatus_, audioSummary.c_str());
    EnableWindow(gameMute_, TRUE);
    EnableWindow(microphoneMute_, TRUE);
    EnableWindow(ui(SystemOutput), !recorder_.busy());
    EnableWindow(ui(RefreshAudio), !recorder_.busy());
    int mode = selected(ui(RateMode));
    EnableWindow(ui(BitrateEdit), mode != 2);
    EnableWindow(ui(MaxBitrateEdit), mode == 0);
    EnableWindow(ui(QpEdit), mode == 2);
    EnableWindow(ui(SystemOutput), !recorder_.busy() && selected(gameAudio_) == 2);
    std::wstring status;
    std::vector<ClipInfo> result;
    if (library_.take(result, status))
    {
        clips_ = std::move(result);
        ++frontendLibraryRevision_;
        SetWindowTextW(galleryStatus_, status.c_str());
    }
    if (page_ == 1 && galleryDirty_ && !library_.busy())
    {
        library_.request(outputFolder_);
        galleryFolder_ = outputFolder_;
        galleryDirty_ = false;
    }
    auto tip = L"ReDot - " + std::wstring(closing_                                 ? L"finalizing"
                                              : recoveryPending_                       ? L"recovering"
                                              : recorder_.busy() && !replayWorkerOnly_ ? L"RECORDING"
                                              : replayEnabled_                         ? L"REPLAY ACTIVE"
                                                                                       : L"idle");
    wcsncpy_s(tray_.szTip, tip.c_str(), _TRUNCATE);
    unsigned iconState = closing_ || recoveryPending_             ? 3
                         : recorder_.busy() && !replayWorkerOnly_ ? 1
                         : replayEnabled_                         ? 2
                                                                  : 0;
    if (stateIcons_[iconState])
        tray_.hIcon = stateIcons_[iconState];
    tray_.uFlags = NIF_TIP | NIF_ICON;
    if (!closing_ && settingsAutosave_ && settingsDirty_ &&
        std::chrono::steady_clock::now() - settingsDirtyAt_ >= std::chrono::milliseconds(750))
    {
        settingsDirty_ = false;
        settingsStore_.save(collectSettings());
    }
    auto failure = settingsStore_.error();
    if (!failure.empty())
        SetWindowTextW(settingsStatus_, failure.c_str());
}
bool workspaceCommand(WPARAM w, LPARAM)
{
    int id = LOWORD(w);
    if ((id < NavCapture || (id >= ApplySettings && id < GalleryRefresh) || (id >= 2200 && id <= 2205)) &&
        (HIWORD(w) == BN_CLICKED || HIWORD(w) == CBN_SELCHANGE || HIWORD(w) == EN_CHANGE))
        markSettingsDirty();
    if (!uiReady_)
        return false;
    if (id >= NavCapture && id <= NavDiagnostics)
    {
        switchPage(id - NavCapture);
        return true;
    }
    if(id==SettingsCategory && HIWORD(w)==CBN_SELCHANGE){settingsCategory_=selected(category_);layoutSettings();return true;}
    if(id==ClipConfirmSound){clipSound_=checked(ui(ClipConfirmSound));markSettingsDirty();return true;}
    if(id==CloseToTray || id==MinimizeToTray){closeToTray_=checked(ui(CloseToTray));minimizeToTray_=checked(ui(MinimizeToTray));markSettingsDirty();return true;}
    if(id==TransparencyCheck || id==AnimationCheck){transparency_=checked(ui(TransparencyCheck));animations_=checked(ui(AnimationCheck));applyAppearance();markSettingsDirty();return true;}
    if(id==CaptureAudioToggle || id==CaptureDetailsToggle || id==CapturePreviewToggle){
        if(id==CaptureAudioToggle)audioExpanded_=!audioExpanded_;
        if(id==CaptureDetailsToggle)detailsExpanded_=!detailsExpanded_;
        if(id==CapturePreviewToggle){previewVisible_=!previewVisible_;check(ui(PreviewCheck),previewVisible_);setTextChanged(ui(CapturePreviewToggle),previewVisible_?L"Hide preview":L"Show preview");}
        markSettingsDirty();shellLayout();return true;
    }
    if(id>=MainGameEnable && id<=MainMicCheck){
        HWND originals[]={gameEnable_,gameAudio_,microphoneEnable_,microphone_,gameMute_,microphoneMute_,gameVolume_,microphoneVolume_,gameMeter_,microphoneMeter_,micCheck_};
        int i=id-MainGameEnable;HWND original=originals[i];
        if(i==1||i==3)select(original,selected(mainAudio_[i]));
        else if(i==0||i==2||i==4||i==5)check(original,checked(mainAudio_[i]));
        SendMessageW(hwnd_,WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(original),HIWORD(w)),reinterpret_cast<LPARAM>(original));
        markSettingsDirty();syncMainAudio();return true;
    }
    if (id == ApplySettings)
    {
        applySettings();
        return true;
    }
    if (id == BindApply)
    {
        applyBindings();
        return true;
    }
    if (id == RestoreSettings)
    {
        if (recorder_.busy() || micMonitor_.busy())
        {
            SetWindowTextW(settingsStatus_, L"Stop recording, replay and mic check before restoring defaults.");
            return true;
        }
        if (MessageBoxW(hwnd_, L"Restore ReDot settings and shortcuts to defaults? Saved clips are kept.",
                        L"Restore Defaults", MB_YESNO | MB_ICONQUESTION) == IDYES)
        {
            stopCapture();
            SetWindowTextW(ui(WidthEdit), L"1920");
            SetWindowTextW(ui(HeightEdit), L"1080");
            SetWindowTextW(ui(FpsEdit), L"60");
            SetWindowTextW(ui(BitrateEdit), L"20");
            SetWindowTextW(ui(MaxBitrateEdit), L"35");
            SetWindowTextW(ui(QpEdit), L"23");
            SetWindowTextW(ui(PreviewRate), L"30");
            SetWindowTextW(ui(GameSync), L"0");
            SetWindowTextW(ui(MicSync), L"0");
            SetWindowTextW(replayDuration_, L"60");
            SetWindowTextW(replayMemory_, L"512");
            select(microphone_, 0);
            select(ui(SystemOutput), 0);
            pinnedDefaultMicrophone_.clear();
            pinnedSystemAudio_.clear();
            loadedSettings_.clear();
            select(combo_, -1);
            wchar_t profile[32768]{};
            GetEnvironmentVariableW(L"USERPROFILE", profile, 32768);
            outputFolder_ = (std::filesystem::path(profile) / L"Videos" / L"PandaClip").wstring();
            select(ui(ResolutionPreset), 1);
            restoreUi({});
            settingsAutosave_ = true;
            applyBindings();
            settingsStore_.save(collectSettings());
        }
        return true;
    }
    if ((id == WidthEdit || id == HeightEdit) && HIWORD(w) == EN_CHANGE)
    {
        auto width = uiText(ui(WidthEdit)), height = uiText(ui(HeightEdit));
        select(ui(ResolutionPreset), width == L"1280" && height == L"720"    ? 0
                                     : width == L"1920" && height == L"1080" ? 1
                                     : width == L"2560" && height == L"1440" ? 2
                                                                             : 3);
        return true;
    }
    if (id == RateMode && HIWORD(w) == CBN_SELCHANGE)
    {
        shellLayout();
        return true;
    }
    if (id == ResolutionPreset && HIWORD(w) == CBN_SELCHANGE)
    {
        int i = selected(ui(ResolutionPreset));
        if (i < 3)
        {
            const wchar_t *widths[] = {L"1280", L"1920", L"2560"};
            const wchar_t *heights[] = {L"720", L"1080", L"1440"};
            SetWindowTextW(ui(WidthEdit), widths[i]);
            SetWindowTextW(ui(HeightEdit), heights[i]);
        }
        return true;
    }
    if (id == OpenOutput)
    {
        ShellExecuteW(hwnd_, L"open", outputFolder_.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return true;
    }
    if (id == RefreshAudio)
    {
        audioRefreshRequested_ = true;
        deviceCatalog_.request();
        setTextChanged(settingsStatus_,
                       L"Refreshing audio devices in the background; selected endpoints are preserved.");
        return true;
    }
    if (id == SystemOutput && HIWORD(w) == CBN_SELCHANGE)
    {
        pinnedSystemAudio_.clear();
        return true;
    }
    if (id == kMicrophoneMute || id == kGameMute)
    {
        liveMutes();
        return true;
    }
    if (id == kReportButton)
    {
        exportDiagnostics();
        return true;
    }
    if (id == LogsOpen)
    {
        wchar_t local[32768]{};
        GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
        auto path = (std::filesystem::path(local) / L"PandaClip" / L"logs").wstring();
        ShellExecuteW(hwnd_, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return true;
    }
    if (id == TrayOpen)
    {
        ShowWindow(hwnd_, SW_RESTORE);
        SetForegroundWindow(hwnd_);
        return true;
    }
    if (id == TraySave)
    {
        saveReplayClip();
        return true;
    }
    if (id == TrayRecord)
    {
        shortcut(2);
        return true;
    }
    if (id == TrayReplay)
    {
        toggleReplay();
        return true;
    }
    if (id == TrayExit)
    {
        exitRequested_ = true;
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        return true;
    }
    return false;
}


void shellLayout() {} // hidden controller has no presentation layout
