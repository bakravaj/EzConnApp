#pragma once
#include <QWidget>
#include <QImage>
#include <QPainter>
#include <QMouseEvent>
#include <functional>
#include <optional>
#include <cmath>

class ScreenView final : public QWidget {
public:
    explicit ScreenView(QWidget* parent=nullptr):QWidget(parent) {
        setMinimumSize(1,1);
        setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    }
    void setFrame(const QImage& frame) { image_=frame; update(); }
    std::function<void(QPoint)> touch;
    std::optional<QPoint> framebufferPoint(QPointF position) const {
        const auto target=imageRect();
        if(target.isEmpty() || position.x()<target.x() || position.y()<target.y()
            || position.x()>=target.x()+target.width() || position.y()>=target.y()+target.height()) return std::nullopt;
        const int x=int(std::floor((position.x()-target.x())*image_.width()/target.width()));
        const int y=int(std::floor((position.y()-target.y())*image_.height()/target.height()));
        return QPoint(qBound(0,x,image_.width()-1),qBound(0,y,image_.height()-1));
    }
    QRect imageRect() const {
        if(image_.isNull()) return {};
        const auto size=image_.size().scaled(contentsRect().size(),Qt::KeepAspectRatio);
        return QRect(QPoint(contentsRect().x()+(contentsRect().width()-size.width())/2,
                            contentsRect().y()+(contentsRect().height()-size.height())/2),size);
    }
protected:
    void mousePressEvent(QMouseEvent* event) override {
        if(event->button()==Qt::LeftButton && event->modifiers()==Qt::NoModifier) {
            setFocus(Qt::MouseFocusReason);
            if(const auto point=framebufferPoint(event->position()); point && touch) touch(*point);
            event->accept(); return;
        }
        QWidget::mousePressEvent(event);
    }
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
