#pragma once
#include "core/RemoteKey.h"
#include <QApplication>
#include <QKeyEvent>
#include <QWidget>
#include <functional>

class RemoteKeyFilter final : public QObject {
public:
    RemoteKeyFilter(QWidget* window,std::function<void(quint8)> send):QObject(window),window_(window),send_(std::move(send)) {
        qApp->installEventFilter(this);
    }
    void setActive(bool active) { active_=active; }
protected:
    bool eventFilter(QObject* object,QEvent* event) override {
        auto* widget=qobject_cast<QWidget*>(object);
        if(!active_ || !widget || widget->window()!=window_ || !window_->isActiveWindow()) return false;
        if(event->type()!=QEvent::KeyPress && event->type()!=QEvent::KeyRelease && event->type()!=QEvent::ShortcutOverride) return false;
        auto* key=static_cast<QKeyEvent*>(event);
        if(key->modifiers() & (Qt::ShiftModifier|Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier)) return false;
        const auto code=ezconn::RemoteKey::fromQt(key->key());
        if(!code) return false;
        if(event->type()==QEvent::KeyPress && !key->isAutoRepeat()) send_(*code);
        key->accept(); return true;
    }
private:
    QWidget* window_;
    std::function<void(quint8)> send_;
    bool active_=false;
};
