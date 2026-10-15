#pragma once
#include <QIcon>
#include <QPainter>
#include "design_system.h"
#include <QPainterPath>
#include <QPixmap>
#include <cmath>
#include <array>

// Small vector foregrounds: no desktop/preview textures or generated assets.
inline QIcon drawDashboardIcon(int kind, bool muted=false) {
    QPixmap image(96,96);image.fill(Qt::transparent);
    QPainter p(&image);p.scale(3,3);p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(DesignSystem::color("text"),1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);
    if(kind==0){p.drawRoundedRect(QRectF(3,5,26,18),2,2);p.drawLine(16,23,16,28);p.drawLine(10,28,22,28);}
    if(kind==1){p.drawRoundedRect(QRectF(3,5,26,22),4,4);QPainterPath a;a.moveTo(13,10);a.lineTo(22,16);a.lineTo(13,22);a.closeSubpath();p.drawPath(a);}
    if(kind==2){QPainterPath a;for(int i=0;i<32;++i){double angle=i*3.141592653589793/16;double r=(i%4==1||i%4==2)?13:10;QPointF v(16+std::cos(angle)*r,16+std::sin(angle)*r);if(!i)a.moveTo(v);else a.lineTo(v);}a.closeSubpath();p.drawPath(a);p.drawEllipse(QRectF(12,12,8,8));}
    if(kind==3){QPainterPath a;a.moveTo(2,17);a.lineTo(8,17);a.lineTo(12,5);a.lineTo(17,27);a.lineTo(22,12);a.lineTo(25,17);a.lineTo(30,17);p.drawPath(a);}
    if(kind==4){p.setPen(Qt::NoPen);p.setBrush(DesignSystem::color("record"));p.drawEllipse(QRectF(5,5,22,22));}
    if(kind==5){p.drawArc(QRectF(4,4,24,24),35*16,300*16);p.drawLine(5,5,5,12);p.drawLine(5,12,12,12);}
    if(kind==6){p.drawRoundedRect(QRectF(5,4,22,25),2,2);p.drawRect(QRectF(10,4,12,9));p.drawRect(QRectF(10,20,12,9));}
    if(kind==7){QPainterPath a;a.moveTo(13,10);a.lineTo(7,14);a.lineTo(3,14);a.lineTo(3,21);a.lineTo(7,21);a.lineTo(13,25);a.closeSubpath();p.drawPath(a);if(!muted){p.drawArc(QRectF(10,8,15,19),-65*16,130*16);p.drawArc(QRectF(9,3,23,29),-55*16,110*16);}else{p.drawLine(20,12,28,23);p.drawLine(28,12,20,23);}}
    if(kind==8){p.drawRoundedRect(QRectF(12,3,8,17),4,4);p.drawArc(QRectF(7,6,18,20),180*16,180*16);p.drawLine(7,16,7,14);p.drawLine(25,16,25,14);p.drawLine(16,26,16,30);p.drawLine(11,30,21,30);}
    if(kind==9){QPainterPath a;a.moveTo(8,9);a.cubicTo(2,11,1,23,4,26);a.cubicTo(7,28,10,23,12,22);a.lineTo(20,22);a.cubicTo(22,23,25,28,28,26);a.cubicTo(31,23,30,11,24,9);a.closeSubpath();p.drawPath(a);p.drawLine(7,16,13,16);p.drawLine(10,13,10,19);p.drawEllipse(QPointF(23,14),.9,.9);p.drawEllipse(QPointF(26,18),.9,.9);}
    if(kind==10){QPainterPath a;a.moveTo(3,7);a.lineTo(12,7);a.lineTo(15,10);a.lineTo(29,10);a.lineTo(29,26);a.lineTo(3,26);a.closeSubpath();p.drawPath(a);}
    if(kind==12){QPainterPath a;a.moveTo(3,11);a.lineTo(16,4);a.lineTo(29,11);a.lineTo(16,18);a.closeSubpath();p.drawPath(a);for(int y:{17,23}){p.drawLine(3,y,16,y+7);p.drawLine(16,y+7,29,y);}}
    return QIcon(image);
}

inline QIcon dashboardIcon(int kind,bool muted=false){
    static const auto icons=[] {std::array<QIcon,13> result;for(int i=0;i<11;++i)result[i]=drawDashboardIcon(i);result[11]=drawDashboardIcon(7,true);result[12]=drawDashboardIcon(12);return result;}();
    return icons[muted&&kind==7?11:kind];
}
