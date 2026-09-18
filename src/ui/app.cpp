// Interface provisoria do M1.
//
// Nao tenta parecer com o Winamp: a aparencia definitiva depende do atlas de
// sprites e chega no M5. O que esta janela entrega e reproducao real com
// controles de verdade, para que os requisitos do M1 possam ser exercitados a
// mao alem dos testes headless.

#include <QApplication>
#include <QCommandLineParser>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QWidget>

#include <cstring>
#include <memory>

#include "core/audio/engine.h"
#include "core/util/log.h"
#include "platform/audio_device.h"

namespace {

constexpr int kPreferredRate = 44100;
constexpr int kChannels = 2;

// Dono do dispositivo e do engine. O engine e criado depois do dispositivo,
// porque a taxa efetiva quem decide e o dispositivo — nao o nosso desejo.
struct Player {
    pang::platform::AudioOutput output;
    std::unique_ptr<pang::core::Engine> engine;

    static void render(void* user, float* out, std::uint32_t frames) {
        auto* self = static_cast<Player*>(user);
        if (self->engine)
            self->engine->render(out, frames);
        else
            std::memset(out, 0, static_cast<std::size_t>(frames) * kChannels * sizeof(float));
    }

    bool open_device(std::string& error) {
        if (!output.start(kPreferredRate, kChannels, &Player::render, this, error)) return false;
        engine = std::make_unique<pang::core::Engine>(output.sample_rate(), output.channels());
        return true;
    }

    // Envolve as operacoes cuja precondicao e "render() parado".
    template <typename Fn>
    void controlled(Fn&& fn) {
        output.suspend();
        fn();
        output.resume();
    }
};

QString format_time(std::int64_t frames, int rate) {
    if (frames < 0 || rate <= 0) return QStringLiteral("--:--");
    const std::int64_t total = frames / rate;
    return QStringLiteral("%1:%2")
        .arg(total / 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("PlayAmpNG"));
    app.setApplicationVersion(QStringLiteral(PLAYAMPNG_VERSION));

    // IN-07 — abertura por argumentos de linha de comando.
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Player de audio"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("arquivo"),
                                 QStringLiteral("arquivo ou URL de audio"));
    parser.process(app);

    Player player;
    std::string device_error;
    const bool device_ok = player.open_device(device_error);

    QWidget window;
    window.setWindowTitle(QStringLiteral("PlayAmpNG (provisorio — M1)"));
    window.resize(480, 190);
    window.setStyleSheet(QStringLiteral("background:#2b2b2b; color:#00ff7f;"));

    auto* title = new QLabel(QStringLiteral("nenhuma faixa carregada"));
    auto* status = new QLabel;
    auto* elapsed = new QLabel(QStringLiteral("--:--"));
    auto* duration = new QLabel(QStringLiteral("--:--"));
    auto* position = new QSlider(Qt::Horizontal);
    auto* volume = new QSlider(Qt::Horizontal);
    volume->setRange(0, 100);
    volume->setValue(100);
    position->setRange(0, 1000);
    position->setEnabled(false);

    auto* open = new QPushButton(QStringLiteral("Abrir"));
    auto* play = new QPushButton(QStringLiteral("Tocar"));
    auto* pause = new QPushButton(QStringLiteral("Pausar"));
    auto* stop = new QPushButton(QStringLiteral("Parar"));

    auto* grid = new QGridLayout(&window);
    grid->addWidget(title, 0, 0, 1, 4);
    grid->addWidget(status, 1, 0, 1, 4);
    grid->addWidget(elapsed, 2, 0);
    grid->addWidget(position, 2, 1, 1, 2);
    grid->addWidget(duration, 2, 3);
    grid->addWidget(new QLabel(QStringLiteral("Vol")), 3, 0);
    grid->addWidget(volume, 3, 1, 1, 3);
    grid->addWidget(open, 4, 0);
    grid->addWidget(play, 4, 1);
    grid->addWidget(pause, 4, 2);
    grid->addWidget(stop, 4, 3);

    if (!device_ok) {
        status->setText(QStringLiteral("sem dispositivo de audio: %1")
                            .arg(QString::fromStdString(device_error)));
        for (auto* b : {open, play, pause, stop}) b->setEnabled(false);
        volume->setEnabled(false);
        window.show();
        return app.exec();
    }

    auto* engine = player.engine.get();

    const auto load = [&, engine](const QString& path) {
        title->setText(path);
        player.controlled([&] { engine->load(path.toStdString(), true); });
    };

    QObject::connect(open, &QPushButton::clicked, [&] {
        const QString path = QFileDialog::getOpenFileName(&window, QStringLiteral("Abrir audio"));
        if (!path.isEmpty()) load(path);
    });
    QObject::connect(play, &QPushButton::clicked, [engine] { engine->play(); });
    QObject::connect(pause, &QPushButton::clicked, [engine] { engine->pause(); });
    QObject::connect(stop, &QPushButton::clicked,
                     [&, engine] { player.controlled([engine] { engine->stop(); }); });
    QObject::connect(volume, &QSlider::valueChanged,
                     [engine](int v) { engine->set_volume(static_cast<float>(v) / 100.0f); });

    // Busca so acontece quando o usuario solta o controle: arrastar nao deve
    // disparar uma sequencia de seeks.
    QObject::connect(position, &QSlider::sliderReleased, [&, engine] {
        const auto snap = engine->snapshot();
        if (snap.duration_frames <= 0) return;
        const double seconds = static_cast<double>(position->value()) / 1000.0 *
                               static_cast<double>(snap.duration_frames) / engine->sample_rate();
        player.controlled([engine, seconds] { engine->seek(seconds); });
    });

    auto* timer = new QTimer(&window);
    QObject::connect(timer, &QTimer::timeout, [&, engine] {
        const auto snap = engine->snapshot();

        QString line = QString::fromLatin1(pang::core::to_string(snap.state));
        if (snap.sample_rate > 0)
            line += QStringLiteral("  ·  %1 Hz  ·  %2")
                        .arg(snap.sample_rate)
                        .arg(snap.channels == 1 ? QStringLiteral("mono") : QStringLiteral("estereo"));
        // PL-26 — bitrate ausente nao vira numero plausivel.
        line += QStringLiteral("  ·  %1").arg(
            snap.bitrate_bps > 0 ? QStringLiteral("%1 kbps").arg(snap.bitrate_bps / 1000)
                                 : QStringLiteral("bitrate -"));
        if (snap.underruns > 0) line += QStringLiteral("  ·  underruns: %1").arg(snap.underruns);
        if (snap.state == pang::core::State::Error)
            line = QString::fromStdString(engine->last_error());
        status->setText(line);

        elapsed->setText(format_time(snap.position_frames, engine->sample_rate()));
        duration->setText(snap.duration_frames >= 0
                              ? format_time(snap.duration_frames, engine->sample_rate())
                              : QStringLiteral("--:--"));

        position->setEnabled(snap.seekable && snap.duration_frames > 0);
        if (!position->isSliderDown() && snap.duration_frames > 0)
            position->setValue(static_cast<int>(1000.0 * snap.position_frames /
                                                static_cast<double>(snap.duration_frames)));
    });
    timer->start(100);

    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) load(args.front());

    window.show();
    return app.exec();
}
