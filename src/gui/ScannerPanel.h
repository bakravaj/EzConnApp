#pragma once
#include "core/ScannerService.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <exception>
class ScannerPanel final : public QDialog {
public:
    ScannerPanel(ezconn::ScannerService& service,QWidget* parent=nullptr):QDialog(parent) {
        setWindowTitle("Scanner Emulator"); resize(720,400);
        auto* layout=new QVBoxLayout(this);
        layout->addWidget(new QLabel("Rotor108 Scanner RX Inject — max 251 bytes including CR/LF"));
        auto* mode=new QComboBox; mode->addItems({"ASCII","RAW HEX"}); layout->addWidget(mode);
        auto* editor=new QPlainTextEdit; editor->setPlaceholderText("Paste BVBS or raw hexadecimal bytes"); layout->addWidget(editor);
        auto* presets=new QHBoxLayout;
        for(int i=0;i<3;++i) {
            auto* button=new QPushButton(QString("Preset %1").arg(i+1)); presets->addWidget(button);
            connect(button,&QPushButton::clicked,this,[=]{mode->setCurrentIndex(0); editor->setPlainText(ezconn::ScannerService::preset(i));});
        }
        layout->addLayout(presets);
        auto* append=new QCheckBox("Append CR/LF"); append->setChecked(true); layout->addWidget(append);
        auto* actions=new QHBoxLayout; send_=new QPushButton("Send"); send_->setEnabled(false);
        auto* clear=new QPushButton("Clear"); actions->addWidget(send_); actions->addWidget(clear); layout->addLayout(actions);
        status_=new QLabel("Read versions first. Stop video before sending."); status_->setWordWrap(true); layout->addWidget(status_);
        connect(clear,&QPushButton::clicked,editor,&QPlainTextEdit::clear);
        connect(send_,&QPushButton::clicked,this,[=,&service,this]{
            try {
                const auto data=ezconn::ScannerService::preparePayload(editor->toPlainText(),mode->currentIndex()==1,append->isChecked());
                status_->setText("Sending command 9..."); service.sendScannerPayload(data);
            } catch(const std::exception& e) { const auto error=QString::fromUtf8(e.what()); status_->setText(error); emit service.logLine("Scanner: "+error); }
        });
        connect(&service,&ezconn::ScannerService::failed,status_,&QLabel::setText);
        connect(&service,&ezconn::ScannerService::completed,this,[this]{status_->setText("Rotor108 acknowledged Scanner RX Inject.");});
    }
    void setAvailable(bool available) { send_->setEnabled(available); }
private:
    QPushButton* send_;
    QLabel* status_;
};
