#pragma once
#include <QPainter>
#include <QPainterPath>
#include <QWidget>

// Original vector illustration, rendered locally without game assets.
class FarmBanner : public QWidget {
public:
    explicit FarmBanner(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(180);
        setMaximumHeight(220);
        setAccessibleName("NatureLoader, kembali ke kebun");
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(width() / 1000.0, height() / 190.0);
        QPainterPath frame;
        frame.addRoundedRect(QRectF(1, 1, 998, 188), 10, 10);
        p.setClipPath(frame);
        QLinearGradient sky(0, 0, 0, 190);
        sky.setColorAt(0, QColor("#8fd7e9")); sky.setColorAt(1, QColor("#e6f3c6"));
        p.fillRect(QRectF(0, 0, 1000, 190), sky);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#ffe8a0")); p.drawEllipse(QRectF(835, 15, 52, 52));
        p.setBrush(QColor("#f7fcdf"));
        for (const auto &point : {QPointF(55, 27), QPointF(380, 17), QPointF(720, 36)}) {
            p.drawEllipse(QRectF(point.x(), point.y(), 80, 22));
            p.drawEllipse(QRectF(point.x() + 18, point.y() - 12, 36, 32));
        }
        p.setBrush(QColor("#78a794"));
        p.drawPolygon(QPolygonF({{0,125},{130,40},{215,112},{335,35},{450,125},{600,55},{760,125},{900,45},{1000,120}}));
        p.setBrush(QColor("#c6ddb8"));
        p.drawPolygon(QPolygonF({{275,75},{335,35},{390,77},{351,64},{334,71},{317,59}}));
        p.setBrush(QColor("#81ad59")); p.drawEllipse(QRectF(-70, 91, 660, 210));
        p.setBrush(QColor("#91b85b")); p.drawEllipse(QRectF(460, 99, 640, 150));
        p.setBrush(QColor("#6e963f")); p.drawRect(QRectF(0, 153, 1000, 37));
        // A winding lane joins the barn to the foreground.
        QPainterPath lane;
        lane.moveTo(811, 157); lane.cubicTo(792, 169, 815, 177, 845, 190);
        lane.lineTo(887, 190); lane.cubicTo(850, 169, 821, 170, 825, 157);
        p.setBrush(QColor("#d6bc75")); p.drawPath(lane);
        // Orchard trees use the same simple, rounded silhouettes as the UI.
        for (const auto &tree : {QPointF(620, 116), QPointF(945, 102)}) {
            p.setBrush(QColor("#805933")); p.drawRoundedRect(QRectF(tree.x()-4, tree.y()+12, 8, 36), 2, 2);
            p.setBrush(QColor("#477743")); p.drawEllipse(tree, 28, 25);
            p.setBrush(QColor("#60944a")); p.drawEllipse(tree + QPointF(-8,-8), 22, 21);
            p.setBrush(QColor("#8bb454")); p.drawEllipse(tree + QPointF(-12,-13), 13, 12);
            p.setBrush(QColor("#d16d42"));
            p.drawEllipse(tree + QPointF(10,3), 3, 3); p.drawEllipse(tree + QPointF(-10,10), 3, 3);
        }
        // Barn and vegetable rows.
        p.setBrush(QColor("#d9ba7c")); p.drawRect(QRectF(760, 100, 116, 65));
        p.setBrush(QColor("#a44735"));
        p.drawPolygon(QPolygonF({{745,105},{816,61},{890,105}}));
        p.setPen(QPen(QColor("#663e2c"), 3));
        p.setBrush(QColor("#754a30")); p.drawRect(QRectF(800, 121, 30, 44));
        p.setBrush(QColor("#cee5d0")); p.drawRect(QRectF(843, 116, 18, 19));
        p.setPen(QPen(QColor("#8a6938"), 8));
        for (int y : {170, 184}) p.drawLine(658, y, 743, y);
        for (int x = 665; x < 745; x += 17) {
            p.setPen(QPen(QColor("#c5d36e"), 3));
            p.drawLine(x, 170, x - 4, 164); p.drawLine(x, 170, x + 4, 163);
            p.drawLine(x, 184, x - 4, 178); p.drawLine(x, 184, x + 4, 177);
            p.setBrush(QColor("#efecd1")); p.setPen(Qt::NoPen);
            p.drawEllipse(QRectF(x-3, 181, 6, 5));
        }
        // Fence, flowers and a small chicken.
        p.setPen(QPen(QColor("#efdb9c"), 5));
        p.drawLine(884, 149, 1000, 149); p.drawLine(884, 164, 1000, 164);
        for (int x = 890; x < 1000; x += 26) p.drawLine(x, 138, x, 177);
        p.setPen(Qt::NoPen); p.setBrush(QColor("#ffefbd"));
        p.drawEllipse(QRectF(905, 174, 21, 12)); p.drawEllipse(QRectF(920, 168, 9, 10));
        p.setBrush(QColor("#bb4533")); p.drawEllipse(QRectF(922, 165, 4, 5));
        p.setBrush(QColor("#493e2c")); p.drawEllipse(QRectF(925, 171, 2, 2));
        for (int x : {25, 59, 610, 950, 977}) {
            p.setBrush(QColor("#ffdf85")); p.drawEllipse(QRectF(x, 175, 5, 5));
        }
        // Hand-painted sign, independent of the game's original logo.
        p.setPen(QPen(QColor("#664b2b"), 3)); p.setBrush(QColor("#875a32"));
        p.drawRoundedRect(QRectF(49, 43, 520, 111), 8, 8);
        QLinearGradient timber(0, 37, 0, 148);
        timber.setColorAt(0, QColor("#c89d5c")); timber.setColorAt(1, QColor("#a6753e"));
        p.setBrush(timber); p.drawRoundedRect(QRectF(43, 37, 520, 111), 8, 8);
        p.setPen(QPen(QColor("#d8aa63"), 1));
        for (int y : {63, 93, 125}) p.drawLine(50, y, 555, y);
        p.setPen(QColor("#4c311f"));
        QFont title("serif", 37, QFont::Bold); p.setFont(title);
        p.drawText(QRectF(66, 46, 470, 56), Qt::AlignCenter, "NatureLoader");
        p.setPen(QColor("#fff4ce"));
        p.drawText(QRectF(64, 43, 470, 56), Qt::AlignCenter, "NatureLoader");
        p.setFont(QFont("sans-serif", 12, QFont::Bold));
        p.drawText(QRectF(65, 105, 470, 25), Qt::AlignCenter, "KEMBALI KE KEBUN · BACK TO NATURE");
        p.setBrush(QColor("#60432b")); p.setPen(Qt::NoPen);
        for (const auto &point : {QPointF(55,49), QPointF(549,49), QPointF(55,136), QPointF(549,136)})
            p.drawEllipse(point, 3, 3);
        p.setClipping(false);
        p.setBrush(Qt::NoBrush); p.setPen(QPen(QColor("#9a8051"), 2));
        p.drawRoundedRect(QRectF(1, 1, 998, 188), 10, 10);
    }
};
