#pragma once
#include <QWidget>
#include <QImage>
#include <QPainter>

class ScreenView final : public QWidget {
public:
    explicit ScreenView(QWidget* parent=nullptr):QWidget(parent) {
        setMinimumSize(1,1);
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    }
    void setFrame(const QImage& frame) { image_=frame; update(); }
    QRect imageRect() const {
        if(image_.isNull()) return {};
        const auto size=image_.size().scaled(contentsRect().size(),Qt::KeepAspectRatio);
        return QRect(QPoint(contentsRect().x()+(contentsRect().width()-size.width())/2,
                            contentsRect().y()+(contentsRect().height()-size.height())/2),size);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(),Qt::black);
        if(image_.isNull()) { painter.setPen(Qt::white); painter.drawText(rect(),Qt::AlignCenter,"No frame yet"); return; }
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(imageRect(),image_);
    }
private:
    QImage image_;
};
