// Renderiza os paineis fora da tela, em escala 1x, e grava PNG.
//
// Existe porque posicionar por constante calculada de cabeca, sem conferir o
// resultado, produziu uma sequencia de desalinhamentos. Com isto da para medir
// coluna a coluna em vez de olhar e achar.
//
//   QT_QPA_PLATFORM=offscreen ./build/pang_render <saida-dir> [escala]

#include <QApplication>
#include <QImage>
#include <QPainter>

#include <cstdio>
#include <string>
#include <memory>
#include <QThread>

#include "core/state/controller.h"
#include "ui/panel/equalizer_panel.h"
#include "ui/panel/main_panel.h"
#include "ui/panel/playlist_panel.h"
#include "ui/skin/atlas.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(".");
    const int scale = argc > 2 ? QString::fromLocal8Bit(argv[2]).toInt() : 1;

    pang::ui::skin::Atlas atlas;
    QString error;
    if (!atlas.load(QStringLiteral(PLAYAMPNG_SKIN_DIR), error)) {
        std::printf("erro: %s\n", qPrintable(error));
        return 1;
    }
    atlas.set_scale(scale);

    pang::core::Engine engine(44100, 2);
    pang::core::Controller controller(engine);
    controller.playlist().add("/musica/Artista Um - Cancao De Nome Longo Para Rolar.mp3");
    controller.playlist().add("/musica/Outro - Segunda.mp3");

    std::vector<pang::core::dsp::EqPreset> presets;
    // Bandas espalhadas: com tudo em 0 dB os polegares cobrem os proprios
    // entalhes e nao ha como conferir o alinhamento.
    const float gains[] = {9, 6, -3, -7, -2, 4, 7, 10, 8, 5};
    for (int b = 0; b < pang::core::dsp::Equalizer::kBands; ++b)
        engine.equalizer().set_band_db(b, gains[b]);
    engine.equalizer().set_preamp_db(3.0f);
    auto save = [&](QWidget& widget, const char* name) {
        QImage image(widget.size(), QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        widget.render(&image);
        const QString path = QStringLiteral("%1/%2.png").arg(out, QLatin1String(name));
        image.save(path);
        std::printf("%-12s %dx%d -> %s\n", name, image.width(), image.height(), qPrintable(path));
    };

    pang::ui::MainPanel main_panel(controller, engine, atlas);

    // Alimenta o espectro com audio real: sem isto as barras nao aparecem e
    // nao ha o que medir.
    engine.load(std::string(argc > 3 ? argv[3] : PLAYAMPNG_SKIN_DIR "/../test/tone.wav"),
                pang::core::State::Playing);
    for (int i = 0; i < 200 && engine.snapshot().state == pang::core::State::Loading; ++i)
        QThread::msleep(5);
    main_panel.set_visualization(pang::ui::MainPanel::Visualization::Spectrum);
    std::vector<float> block(512 * 2);
    for (int i = 0; i < 40; ++i) {
        engine.render(block.data(), 512);
        main_panel.tick(0.016f);
        QThread::msleep(2);
    }
    pang::ui::EqualizerPanel equalizer(engine.equalizer(), presets, atlas);
    pang::ui::PlaylistPanel playlist(controller, atlas);
    save(main_panel, "main");
    save(equalizer, "eq");
    save(playlist, "playlist");
    return 0;
}
