#include "application_picker.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QFileInfo>
#include <QPushButton>
#include <QVBoxLayout>
#include <QStyle>
#include <QElapsedTimer>
#include <QScrollBar>
#include <windows.h>
#include <dwmapi.h>
#include <algorithm>

namespace Scenes {
QJsonObject ApplicationWindow::identity() const {
    // HWND/PID are ephemeral and never saved. Exact executable/title/class
    // identify a reconnect candidate; ambiguous matches require explicit reselection.
    return {{"executable",executable},{"executableName",QFileInfo(executable).fileName()},
            {"target",title},{"windowClass",windowClass},{"targetIdentityVersion",1}};
}
namespace {
bool readWindow(HWND hwnd, ApplicationWindow& value) {
    DWORD pid{}; GetWindowThreadProcessId(hwnd,&pid);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process)return false;
    wchar_t path[32768]{};DWORD size=32768;FILETIME start{},exit{},kernel{},user{};
    bool ok=QueryFullProcessImageNameW(process,0,path,&size)&&GetProcessTimes(process,&start,&exit,&kernel,&user);
    CloseHandle(process);if(!ok)return false;
    wchar_t title[2048]{},type[256]{};
    // GetWindowText on external windows reads the system caption without
    // waiting for another application's message loop.
    GetWindowTextW(hwnd,title,2048);GetClassNameW(hwnd,type,256);
    value.handle=reinterpret_cast<quintptr>(hwnd);value.processId=pid;
    value.processStart=(quint64(start.dwHighDateTime)<<32)|start.dwLowDateTime;
    value.title=QString::fromWCharArray(title);value.executable=QString::fromWCharArray(path,int(size));value.windowClass=QString::fromWCharArray(type);
    RECT client{};if(GetClientRect(hwnd,&client)&&client.right>0&&client.bottom>0)value.clientSize=QSizeF(std::min(client.right,16384L),std::min(client.bottom,16384L));
    return !value.title.trimmed().isEmpty();
}
bool sameIdentity(const QJsonObject& saved,const ApplicationWindow& w) {
    return !saved["executable"].toString().isEmpty()
        && saved["executable"].toString().compare(w.executable,Qt::CaseInsensitive)==0
        && saved["target"].toString()==w.title
        && (saved["windowClass"].toString().isEmpty()||saved["windowClass"].toString()==w.windowClass);
}
}
ApplicationDiscovery discoverApplications(quintptr desktop,quint32 excludedProcess,bool withIcons) {
    struct Context { ApplicationDiscovery result;quint32 excluded;QElapsedTimer elapsed;bool icons; } context{{},excludedProcess,{},withIcons};
    context.elapsed.start();
    SetLastError(ERROR_SUCCESS);
    BOOL ok=EnumDesktopWindows(reinterpret_cast<HDESK>(desktop),[](HWND hwnd,LPARAM data)->BOOL {
        auto& c=*reinterpret_cast<Context*>(data);
        if(c.result.windows.size()>=512||c.elapsed.elapsed()>1500)return FALSE;
        DWORD pid{};GetWindowThreadProcessId(hwnd,&pid);
        if(pid==c.excluded||!IsWindowVisible(hwnd)||GetWindow(hwnd,GW_OWNER)
            ||(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&WS_EX_TOOLWINDOW))return TRUE;
        DWORD cloaked{};if(SUCCEEDED(DwmGetWindowAttribute(hwnd,DWMWA_CLOAKED,&cloaked,sizeof(cloaked)))&&cloaked)return TRUE;
        ApplicationWindow w;if(!readWindow(hwnd,w)){++c.result.inaccessible;return TRUE;}
        const auto exe=QFileInfo(w.executable).fileName();
        if(exe.compare("ReDot.exe",Qt::CaseInsensitive)==0||exe.compare("PandaClip.exe",Qt::CaseInsensitive)==0||exe.compare("PandaClipQuietApp.exe",Qt::CaseInsensitive)==0||exe.compare("ReDotQuietApp.exe",Qt::CaseInsensitive)==0
            ||w.windowClass=="Progman"||w.windowClass=="WorkerW"||w.windowClass=="Shell_TrayWnd"||w.windowClass=="Shell_SecondaryTrayWnd")return TRUE;
        if(c.icons){
        DWORD_PTR icon{};
        // Bounded icon requests: a hung application cannot block the UI or
        // turn automatic refresh into an unbounded series of pending jobs.
        SendMessageTimeoutW(hwnd,WM_GETICON,ICON_SMALL2,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,15,&icon);
        if(!icon)icon=GetClassLongPtrW(hwnd,GCLP_HICONSM);
        if(!icon)icon=GetClassLongPtrW(hwnd,GCLP_HICON);
        if(icon){auto owned=CopyIcon(reinterpret_cast<HICON>(icon));if(owned){w.icon=QImage::fromHICON(owned).scaled(32,32,Qt::KeepAspectRatio,Qt::SmoothTransformation);DestroyIcon(owned);}}
        }
        c.result.windows.append(std::move(w));return TRUE;
    },reinterpret_cast<LPARAM>(&context));
    if(!ok){auto error=GetLastError();context.result.error=error?QString("Window discovery failed (Windows %1). Refresh to retry.").arg(error):"Window discovery limit reached; refresh after closing unused windows.";}
    std::sort(context.result.windows.begin(),context.result.windows.end(),[](const auto& a,const auto& b){return a.title.localeAwareCompare(b.title)<0;});
    return context.result;
}
bool applicationStillAvailable(const ApplicationWindow& w) {
    ApplicationWindow now;return IsWindow(reinterpret_cast<HWND>(w.handle))&&readWindow(reinterpret_cast<HWND>(w.handle),now)
        &&now.processId==w.processId&&now.processStart==w.processStart&&now.executable.compare(w.executable,Qt::CaseInsensitive)==0&&now.title==w.title&&now.windowClass==w.windowClass;
}
ApplicationPicker::ApplicationPicker(const QJsonObject& saved,QWidget* parent):QWidget(parent),saved_(saved) {
    setObjectName("sceneApplicationPicker");desktop_=reinterpret_cast<quintptr>(GetThreadDesktop(GetCurrentThreadId()));
    auto box=new QVBoxLayout(this);box->setContentsMargins(0,0,0,0);auto row=new QHBoxLayout;
    search_=new QLineEdit;search_->setObjectName("sceneApplicationSearch");search_->setPlaceholderText("Search application or window title…");search_->setAccessibleName("Search running applications");row->addWidget(search_,1);
    auto refreshButton=new QPushButton("Refresh");refreshButton->setObjectName("sceneApplicationRefresh");row->addWidget(refreshButton);box->addLayout(row);
    list_=new QListWidget;list_->setObjectName("sceneApplicationList");list_->setIconSize({32,32});list_->setMinimumHeight(140);list_->setAccessibleName("Running application windows");box->addWidget(list_,1);
    status_=new QLabel;status_->setObjectName("sceneApplicationStatus");status_->setWordWrap(true);box->addWidget(status_);
    timer_.setInterval(2000);connect(&timer_,&QTimer::timeout,this,&ApplicationPicker::refresh);
    connect(refreshButton,&QPushButton::clicked,this,&ApplicationPicker::refresh);
    connect(search_,&QLineEdit::textChanged,this,[this]{filter();});
    connect(list_,&QListWidget::currentItemChanged,this,[this](QListWidgetItem* item){if(filtering_)return;selectedHandle_=item?item->data(Qt::UserRole).value<quintptr>():0;if(auto w=selected())saved_=w->identity();Q_EMIT selectionChanged();});
    connect(&worker_,&QFutureWatcher<ApplicationDiscovery>::finished,this,[this]{
        result_=worker_.result();bool present=false;for(const auto& w:result_.windows)if(w.handle==selectedHandle_&&sameIdentity(saved_,w))present=true;
        if(!present){selectedHandle_=0;QVector<quintptr> matches;for(const auto& w:result_.windows)if(sameIdentity(saved_,w))matches.append(w.handle);if(matches.size()==1)selectedHandle_=matches[0];}
        filter();Q_EMIT selectionChanged();
    });
}
void ApplicationPicker::showEvent(QShowEvent* e){QWidget::showEvent(e);timer_.start();refresh();}
void ApplicationPicker::hideEvent(QHideEvent* e){timer_.stop();QWidget::hideEvent(e);}
void ApplicationPicker::refresh(){if(worker_.isRunning())return;status_->setText("Discovering running windows…");auto desktop=desktop_;auto excluded=GetCurrentProcessId();worker_.setFuture(QtConcurrent::run([desktop,excluded]{return discoverApplications(desktop,excluded);}));}
std::optional<ApplicationWindow> ApplicationPicker::selected() const {for(const auto& w:result_.windows)if(w.handle==selectedHandle_)return w;return {};}
void ApplicationPicker::filter(){
    filtering_=true;const auto scroll=list_->verticalScrollBar()->value();list_->clear();const auto query=search_->text().trimmed();
    for(const auto& w:result_.windows){const auto exe=QFileInfo(w.executable).fileName();if(!query.isEmpty()&&!w.title.contains(query,Qt::CaseInsensitive)&&!exe.contains(query,Qt::CaseInsensitive))continue;
        auto item=new QListWidgetItem(w.icon.isNull()?style()->standardIcon(QStyle::SP_ComputerIcon):QIcon(QPixmap::fromImage(w.icon)),w.title+"\n"+exe,list_);
        item->setData(Qt::UserRole,QVariant::fromValue(w.handle));item->setToolTip(w.executable+"\n"+w.windowClass);if(w.handle==selectedHandle_)list_->setCurrentItem(item);
    }
    list_->verticalScrollBar()->setValue(scroll);filtering_=false;
    auto message=result_.error;
    if(message.isEmpty())message=selected()?"Selected application · Add Source or Save connects this window to the scene source.":saved_["executable"].toString().isEmpty()?"Select an application. Windows opening/closing refresh every 2 seconds.":"Saved target unavailable or ambiguous. Selection is preserved; choose a listed window to replace it.";
    if(result_.inaccessible)message+=QString(" %1 untitled/inaccessible windows skipped.").arg(result_.inaccessible);
    status_->setText(message);
}
}
