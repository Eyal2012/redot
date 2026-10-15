// Build-only renderer for our deliberately small, maintainable SVG vocabulary.
// Qt Core/Gui already ship with the project; no new SVG runtime dependency.
#include <QGuiApplication>
#include <QXmlStreamReader>
#include <QPainter>
#include <QPainterPath>
#include <QFile>
#include <QDir>
#include <QBuffer>
#include <QDataStream>
#include <QRegularExpression>
#include <iostream>
QPainterPath path(const QString& data){
    auto tokens=data.split(QRegularExpression("[\\s,]+"),Qt::SkipEmptyParts);int n=0;QPainterPath p;
    auto number=[&](){if(n>=tokens.size())throw std::runtime_error("Incomplete SVG path");bool ok{};auto x=tokens[n++].toDouble(&ok);if(!ok)throw std::runtime_error("Invalid SVG coordinate");return x;};
    while(n<tokens.size()){auto command=tokens[n++];if(command=="M"||command=="L"){auto x=number(),y=number();command=="M"?p.moveTo(x,y):p.lineTo(x,y);}else if(command=="C"){auto a=number(),b=number(),c=number(),d=number(),e=number(),f=number();p.cubicTo(a,b,c,d,e,f);}else throw std::runtime_error("SVG renderer supports absolute M/L/C paths only");}return p;
}
QImage render(const QByteArray& svg,int size,const QColor& dot){
    QImage image(size*4,size*4,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.scale(size/16.,size/16.);QXmlStreamReader xml(svg);
    while(!xml.atEnd()){xml.readNext();if(!xml.isStartElement())continue;auto a=xml.attributes();if(xml.name()==u"path"){p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor(a.value("stroke").toString()),a.value("stroke-width").toDouble(),Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.drawPath(path(a.value("d").toString()));}else if(xml.name()==u"circle"){p.setPen(Qt::NoPen);p.setBrush(dot);auto radius=a.value("r").toDouble();p.drawEllipse(QPointF(a.value("cx").toDouble(),a.value("cy").toDouble()),radius,radius);}}
    if(xml.hasError())throw std::runtime_error("Invalid brand SVG");p.end();return image.scaled(size,size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
}
int main(int argc,char** argv){qputenv("QT_QPA_PLATFORM","offscreen");QGuiApplication app(argc,argv);try{auto args=app.arguments();if(args.size()!=3)return 2;QFile source(args[1]);if(!source.open(QIODevice::ReadOnly))return 3;auto svg=source.readAll();QDir().mkpath(args[2]);const int sizes[]={16,20,24,32,40,44,48,50,64,96,128,150,256};
    for(auto state:{QString("idle"),QString("replay"),QString("recording")}){QColor dot(state=="replay"?"#43DA79":state=="recording"?"#F44C66":"#BEA3E8");QList<QByteArray> pngs;QList<int> iconSizes;QFile vector(args[2]+"/"+state+".svg");if(!vector.open(QIODevice::WriteOnly))return 6;auto variant=svg;variant.replace("#BEA3E8",dot.name().toUpper().toUtf8());if(vector.write(variant)!=variant.size())return 7;vector.close();
        for(int size:sizes){auto image=render(svg,size,dot);if(!image.save(args[2]+"/"+state+"-"+QString::number(size)+".png"))return 4;if(size==16||size==24||size==32||size==48||size==64||size==256){QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);image.save(&buffer,"PNG");pngs.append(bytes);iconSizes.append(size);}}
        QFile ico(args[2]+"/"+state+".ico");if(!ico.open(QIODevice::WriteOnly))return 5;QDataStream out(&ico);out.setByteOrder(QDataStream::LittleEndian);out<<quint16(0)<<quint16(1)<<quint16(pngs.size());quint32 offset=6+16*quint32(pngs.size());for(int n=0;n<pngs.size();++n){int size=iconSizes[n];out<<quint8(size==256?0:size)<<quint8(size==256?0:size)<<quint8(0)<<quint8(0)<<quint16(1)<<quint16(32)<<quint32(pngs[n].size())<<offset;offset+=quint32(pngs[n].size());}for(auto& png:pngs)out.writeRawData(png.constData(),int(png.size()));}
    std::cout<<"ReDot vector assets generated; only status-dot differs.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
