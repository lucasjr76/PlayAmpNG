// Janela provisoria do M0.
//
// Nao tenta parecer com o Winamp: a aparencia definitiva depende do atlas de
// sprites e chega no M5. O que esta janela entrega e a verificacao de que as
// tres camadas ligam no mesmo binario e que o app abre.

#include <QApplication>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

#include "core/audio/probe.h"
#include "platform/audio_device.h"

namespace {

QString status_text() {
    std::string error;
    const auto devices = pang::platform::list_playback_devices(error);
    const QString audio = error.empty()
                              ? QStringLiteral("%1 · %2 dispositivo(s)")
                                    .arg(QString::fromStdString(pang::platform::backend_name()))
                                    .arg(devices.size())
                              : QStringLiteral("audio: %1").arg(QString::fromStdString(error));

    return QStringLiteral("PlayAmpNG %1\nFFmpeg %2 (%3)\n%4")
        .arg(QStringLiteral(PLAYAMPNG_VERSION))
        .arg(QString::fromStdString(pang::core::ffmpeg_version()))
        .arg(QString::fromStdString(pang::core::ffmpeg_license()))
        .arg(audio);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("PlayAmpNG"));

    QWidget window;
    window.setWindowTitle(QStringLiteral("PlayAmpNG"));
    window.setFixedSize(275, 116);  // dimensao classica da janela principal (AP-01)
    window.setStyleSheet(QStringLiteral("background:#2b2b2b; color:#00ff7f;"));

    auto* label = new QLabel(status_text(), &window);
    label->setAlignment(Qt::AlignCenter);
    auto* layout = new QVBoxLayout(&window);
    layout->addWidget(label);

    window.show();
    return app.exec();
}
