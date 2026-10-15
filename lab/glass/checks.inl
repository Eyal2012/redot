// Compiled only into the verification target, same shipping UI and material source.
#include <QtTest/QTest>
#include <QJsonArray>
static void runLabChecks(QQuickView& view,LabController& controller,QQuickItem* root,const QString& folder){
    QTimer::singleShot(1700,&view,[&view,&controller,root,folder]{
        QJsonArray results;bool all=true;
        auto check=[&](const char* name,bool success){results.append(QJsonObject{{"check",name},{"passed",success}});all=all&&success;};
        std::function<QQuickItem*(QQuickItem*,const QString&)> findItem=[&](QQuickItem* parent,const QString& name)->QQuickItem*{
            if(parent->objectName()==name)return parent;
            for(auto child:parent->childItems())if(auto found=findItem(child,name))return found;
            return nullptr;
        };
        auto item=[&](const char* name){return findItem(root,QString::fromLatin1(name));};
        auto reveal=[&](QQuickItem* control){
            auto scroll=item("materialScroll");if(!scroll)return;
            auto flick=qvariant_cast<QQuickItem*>(scroll->property("contentItem"));
            for(auto parent=control->parentItem();parent;parent=parent->parentItem())if(parent==flick){
                const double top=control->mapToItem(flick,{0,0}).y(),bottom=top+control->height();const double current=flick->property("contentY").toDouble();
                if(top<0)flick->setProperty("contentY",std::max(0.0,current+top-6));
                else if(bottom>flick->height())flick->setProperty("contentY",std::min(flick->property("contentHeight").toDouble()-flick->height(),current+bottom-flick->height()+6));
                QTest::qWait(50);break;
            }
        };
        auto click=[&](const char* name){auto control=item(name);if(!control){check(name,false);return;}reveal(control);auto p=control->mapToScene({control->width()/2,control->height()/2}).toPoint();QTest::mouseClick(static_cast<QWindow*>(&view),Qt::LeftButton,Qt::NoModifier,p);QTest::qWait(80);};
        const auto hwnd=reinterpret_cast<HWND>(view.winId());
        check("shipping QML launched",view.status()==QQuickView::Ready);
        check("alpha root without whole-window opacity",view.format().alphaBufferSize()==8&&view.color().alpha()==0&&view.opacity()==1.0);
        unsigned children{};EnumChildWindows(hwnd,[](HWND,LPARAM p)->BOOL{++*reinterpret_cast<unsigned*>(p);return TRUE;},reinterpret_cast<LPARAM>(&children));
        check("no unintended native child HWND",children==0);
        const COLORREF border=0xfffffffe;check("native border suppression request accepted",SUCCEEDED(DwmSetWindowAttribute(hwnd,34,&border,sizeof(border))));
        check("policy states activation and request type",controller.policyDetails().contains("Window: inactive")&&controller.policyDetails().contains("None/solid request:"));
        check("prototype preset loaded into UI",root->property("radius").toDouble()==controller.preset()["radius"].toDouble());
        check("prototype grain and recipe reload",root->property("grain")==controller.preset()["grain"]&&root->property("materialPreset")==controller.preset()["materialPreset"]);
        click("preset1");check("Soft Frost preset targeted click",root->property("materialPreset").toString()=="Soft Frost"&&std::abs(root->property("surfaceOpacity").toDouble()-.13)<.001&&root->property("grain").toDouble()>0&&root->property("localBlur").toDouble()>1);
        click("preset2");check("Solid preset forces honest fallback",root->property("solidPreview").toBool()&&!controller.acrylic()&&controller.policy().contains("forced solid"));
        click("preset0");check("Clear preset restores low haze and zero grain",root->property("materialPreset").toString()=="Clear"&&!root->property("solidPreview").toBool()&&std::abs(root->property("surfaceOpacity").toDouble()-.06)<.001&&root->property("grain").toDouble()==0);
        check("desktop shade unavailable in fallback is disabled",!item("shadeSlider")->isEnabled());
        auto viewport=qvariant_cast<QQuickItem*>(item("materialScroll")->property("contentItem"));
        reveal(item("opacitySlider"));
        const auto scrollStart=viewport->property("contentY").toDouble();const auto gainBefore=root->property("surfaceOpacity").toDouble();
        const auto wheelPoint=item("opacitySlider")->mapToScene({20,item("opacitySlider")->height()/2});
        QWheelEvent wheel(wheelPoint,view.mapToGlobal(wheelPoint.toPoint()),{},QPoint(0,-240),Qt::NoButton,Qt::NoModifier,Qt::ScrollUpdate,false,Qt::MouseEventSynthesizedByApplication);
        QCoreApplication::sendEvent(&view,&wheel);QTest::qWait(150);
        check("wheel over slider scrolls material viewport",viewport->property("contentY").toDouble()>scrollStart);
        check("wheel does not accidentally change slider",root->property("surfaceOpacity").toDouble()==gainBefore);
        viewport->setProperty("contentY",0.0);
        auto bar=item("materialBar");const auto barStart=bar->mapToScene({bar->width()/2,bar->height()*bar->property("size").toDouble()/2}).toPoint();
        QTest::mousePress(static_cast<QWindow*>(&view),Qt::LeftButton,Qt::NoModifier,barStart);QTest::mouseMove(static_cast<QWindow*>(&view),barStart+QPoint(0,60));QTest::mouseRelease(static_cast<QWindow*>(&view),Qt::LeftButton,Qt::NoModifier,barStart+QPoint(0,60));QTest::qWait(100);
        check("scrollbar thumb drag scrolls viewport",viewport->property("contentY").toDouble()>0);
        viewport->setProperty("contentY",0.0);
        root->setProperty("sampleMoving",true);click("motion");check("sample motion pauses",!root->property("sampleMoving").toBool());click("motion");check("sample motion resumes",root->property("sampleMoving").toBool());
        click("record");check("demo start targeted click",root->property("demoRunning").toBool());
        click("record");check("demo stop targeted click",!root->property("demoRunning").toBool());
        click("nav1");check("navigation targeted click",root->property("navigation").toInt()==1);
        click("gear");auto popup=root->findChild<QObject*>("demoPopup");check("gear opens anchored popup",popup&&popup->property("visible").toBool());
        if(popup&&popup->property("visible").toBool()){
            auto guides=popup->findChild<QQuickItem*>("guides");if(guides){const auto p=guides->mapToScene({guides->width()/2,guides->height()/2}).toPoint();QTest::mouseClick(static_cast<QWindow*>(&view),Qt::LeftButton,Qt::NoModifier,p);QTest::qWait(80);}
            check("popover source switch",!root->property("gridVisible").toBool());
            QTest::keyClick(static_cast<QWindow*>(&view),Qt::Key_Escape);QTest::qWait(250);
            check("Escape closes popover",!popup->property("visible").toBool());check("gear focus restored",item("gear")->hasActiveFocus());
        }
        click("gear");
        results.append(QJsonObject{{"popup x",popup->property("x").toDouble()},{"popup y",popup->property("y").toDouble()},{"popup visible before outside",popup->property("visible").toBool()}});
        QTest::mouseClick(static_cast<QWindow*>(&view),Qt::LeftButton,Qt::NoModifier,{55,125});QTest::qWait(250);
        check("outside click dismisses popover",popup&&!popup->property("visible").toBool());
        if(popup&&popup->property("visible").toBool()){QTest::keyClick(static_cast<QWindow*>(&view),Qt::Key_Escape);QTest::qWait(200);}
        reveal(item("tintSlider"));root->setProperty("tint",.32);QTest::qWait(50);
        auto slider=item("tintSlider");const double original=root->property("tint").toDouble();
        if(slider){auto start=slider->mapToScene({slider->width()*.32,slider->height()/2}).toPoint();auto end=slider->mapToScene({slider->width()*.80,slider->height()/2}).toPoint();
            QTest::mousePress(static_cast<QWindow*>(&view),Qt::LeftButton,Qt::NoModifier,start);QTest::mouseMove(static_cast<QWindow*>(&view),end);QTest::mouseRelease(static_cast<QWindow*>(&view),Qt::LeftButton,Qt::NoModifier,end);QTest::qWait(100);
            check("live tint drag",root->property("tint").toDouble()>original+.25);
            QTest::keyClick(static_cast<QWindow*>(&view),Qt::Key_Left);check("slider keyboard input",root->property("tint").toDouble()<.9);
        }else check("tint slider found",false);
        root->setProperty("surfaceOpacity",.28);root->setProperty("highlight",.52);root->setProperty("radius",28.0);root->setProperty("refraction",7.0);root->setProperty("localBlur",2.0);root->setProperty("grain",.20);root->setProperty("backdropShade",.10);
        check("lens uniform changes",item("lens")->property("refraction").toDouble()==7.0);
        click("save");for(int i=0;controller.saving()&&i<100;++i)QTest::qWait(20);
        QFile saved(folder+"/approved-material-v1.json");check("prototype-only preset saved",saved.open(QIODevice::ReadOnly));auto json=QJsonDocument::fromJson(saved.readAll()).object();
        check("preset values retained",json["radius"].toDouble()==28&&json["refraction"].toDouble()==7&&json["grain"].toDouble()==.20&&json["backdropShade"].toDouble()==.10&&json["materialPreset"].toString()=="Clear");saved.close();
        QMetaObject::invokeMethod(root,"applyMaterialPreset",Q_ARG(QVariant,QVariant("Solid")));click("save");for(int i=0;controller.saving()&&i<100;++i)QTest::qWait(20);
        QFile solidFile(folder+"/approved-material-v1.json");solidFile.open(QIODevice::ReadOnly);const auto solidSaved=QJsonDocument::fromJson(solidFile.readAll()).object();
        check("Solid recipe and forced fallback persist",solidSaved["materialPreset"].toString()=="Solid"&&solidSaved["solidPreview"].toBool()&&solidSaved["grain"].toDouble()==0);solidFile.close();
        click("reset");check("reset defaults propagated",root->property("radius").toDouble()==18&&std::abs(root->property("tint").toDouble()-.025)<.001&&root->property("grain").toDouble()==0);
        root->setProperty("sampleMoving",false);QTest::qWait(100);
        root->setProperty("refraction",0.0);root->setProperty("localBlur",0.0);QTest::qWait(200);
        auto lens=item("lens");auto position=lens->mapToScene({0,0});const auto scale=view.devicePixelRatio();
        const QRect sampleRect(int(position.x()*scale),int(position.y()*scale),int(lens->width()*scale),int(lens->height()*scale));
        auto neutral=view.grabWindow().copy(sampleRect);
        root->setProperty("refraction",8.0);QTest::qWait(200);auto refracted=view.grabWindow().copy(sampleRect);
        int changedPixels=0;if(!neutral.isNull()&&!refracted.isNull()&&neutral.size()==refracted.size())for(int y=0;y<neutral.height();++y)for(int x=0;x<neutral.width();++x)if(neutral.pixel(x,y)!=refracted.pixel(x,y))++changedPixels;
        check("GPU lens displacement changes bounded source pixels",changedPixels>50);
        QDir().mkpath(folder);neutral.save(folder+"/lens-neutral.png");refracted.save(folder+"/lens-refracted.png");
        results.append(QJsonObject{{"lens changed pixels",changedPixels},{"note","internal GPU fixture readback, not desktop transmission evidence"}});
        root->setProperty("refraction",0.0);root->setProperty("grain",0.0);QTest::qWait(120);auto clean=view.grabWindow().copy(sampleRect);
        root->setProperty("grain",1.0);QTest::qWait(120);auto grainy=view.grabWindow().copy(sampleRect);
        int grainPixels=0;for(int y=0;y<clean.height();++y)for(int x=0;x<clean.width();++x)if(clean.pixel(x,y)!=grainy.pixel(x,y))++grainPixels;
        check("grain shader changes actual bounded pixels",grainPixels>50);
        root->setProperty("grain",0.0);root->setProperty("localBlur",0.0);QTest::qWait(120);auto sharp=view.grabWindow().copy(sampleRect);
        root->setProperty("localBlur",2.0);QTest::qWait(120);auto diffused=view.grabWindow().copy(sampleRect);
        int blurPixels=0;for(int y=0;y<sharp.height();++y)for(int x=0;x<sharp.width();++x)if(sharp.pixel(x,y)!=diffused.pixel(x,y))++blurPixels;
        check("sample diffusion shader changes actual bounded pixels",blurPixels>50);
        results.append(QJsonObject{{"grain changed pixels",grainPixels},{"sample diffusion changed pixels",blurPixels}});
        QMetaObject::invokeMethod(root,"reset");
        click("solid");check("solid preview switch",!controller.acrylic()&&root->property("solidPreview").toBool());
        click("solid");check("solid preview restores policy",!root->property("solidPreview").toBool());
        view.resize(810,660);QTest::qWait(300);
        item("save")->forceActiveFocus(Qt::TabFocusReason);QTest::qWait(100);
        const auto savePoint=item("save")->mapToItem(viewport,{0,0});
        check("keyboard focus reveals lower controls at small size",savePoint.y()>=-1&&savePoint.y()+item("save")->height()<=viewport->height()+1);
        RECT client{};GetClientRect(hwnd,&client);check("resize Qt/native client agrees",std::abs(client.right-view.width()*view.devicePixelRatio())<2&&std::abs(client.bottom-view.height()*view.devicePixelRatio())<2);
        RECT bounds{};GetWindowRect(hwnd,&bounds);LRESULT hit{};SendMessageTimeoutW(hwnd,WM_NCHITTEST,0,MAKELPARAM(bounds.left+2,bounds.top+100),SMTO_ABORTIFHUNG,500,reinterpret_cast<PDWORD_PTR>(&hit));check("native resize hit test",hit==HTLEFT);
        SendMessageTimeoutW(hwnd,WM_NCHITTEST,0,MAKELPARAM(bounds.left+170,bounds.top+25),SMTO_ABORTIFHUNG,500,reinterpret_cast<PDWORD_PTR>(&hit));check("native caption hit test",hit==HTCAPTION);
        click("maximize");check("maximize control",view.visibility()==QWindow::Maximized);
        click("maximize");check("restore control",view.visibility()==QWindow::Windowed);
        click("minimize");check("minimize control",view.visibility()==QWindow::Minimized);view.showNormal();QTest::qWait(150);
        QDir().mkpath(folder);QSaveFile report(folder+"/checks.json");if(report.open(QIODevice::WriteOnly)){report.write(QJsonDocument(QJsonObject{{"passed",all},{"results",results},{"policy",controller.policy()},{"dpr",view.devicePixelRatio()}}).toJson());report.commit();}
        click("close");QCoreApplication::exit(all?0:1);
    });
}
