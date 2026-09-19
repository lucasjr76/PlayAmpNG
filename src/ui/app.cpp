// Montagem da aplicacao.
//
// O painel principal (275x116) e desenhado com sprites do atlas e e a janela
// primaria. Os paineis de playlist e de equalizador ainda usam widgets Qt
// estilizados: o tratamento em sprite deles vem na segunda parte do M5.

#include <QAbstractListModel>
#include <QApplication>
#include <QComboBox>
#include <QCloseEvent>
#include <QCommandLineParser>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QInputDialog>
#include <QLineEdit>
#include <QListView>
#include <QMimeData>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>

#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "core/dsp/presets.h"
#include "core/util/log.h"
#include "core/meta/scanner.h"
#include "core/playlist/m3u.h"
#include "core/state/controller.h"
#include "platform/audio_device.h"
#include "ui/panel/equalizer_panel.h"
#include "ui/panel/main_panel.h"
#include "ui/panel/playlist_panel.h"
#include "ui/shell/integrated.h"
#include "ui/settings.h"
#include "ui/shell/recovery.h"
#include "ui/skin/winamp_skin.h"

using pang::core::Repeat;
using pang::core::State;

namespace {

constexpr int kPreferredRate = 44100;
constexpr int kChannels = 2;

QString format_ms(std::int64_t ms) {
    if (ms < 0) return QStringLiteral("--:--");
    const std::int64_t total = ms / 1000;
    return QStringLiteral("%1:%2")
        .arg(total / 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

QString format_frames(std::int64_t frames, int rate) {
    if (frames < 0 || rate <= 0) return QStringLiteral("--:--");
    return format_ms(frames * 1000 / rate);
}

const char* repeat_label(Repeat r) {
    switch (r) {
        case Repeat::Off: return "Repetir: nao";
        case Repeat::Track: return "Repetir: faixa";
        case Repeat::All: return "Repetir: lista";
    }
    return "?";
}

}  // namespace

int main(int argc, char** argv) {
    // O aplicativo faz a PROPRIA escala, em multiplos inteiros e com vizinho
    // mais proximo. Precisa, portanto, receber pixels fisicos 1:1 do Qt.
    //
    // Sem isto, num monitor com escala fracionaria — 1,6 na maquina de
    // desenvolvimento — o Qt multiplica cada coordenada por 1,6 e reamostra:
    // uma barra de 3 px vira 4,8 fisicos e alterna entre 4 e 5, uma linha de
    // 1 px vira 1,6, e todo o trabalho de alinhamento em pixel se perde antes
    // de chegar a tela. Era essa a causa das barras irregulares e do aspecto
    // borrado geral.
    //
    // Floor leva qualquer fator fracionario a 1. Quem escolhe o tamanho e o
    // usuario, pelo controle de escala do proprio player.
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::Floor);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("PlayAmpNG"));
    app.setApplicationVersion(QStringLiteral(PLAYAMPNG_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Player de audio"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("arquivos"),
                                 QStringLiteral("arquivos, diretorios ou URLs"),
                                 QStringLiteral("[arquivos...]"));
    parser.process(app);

    // ------------------------------------------------------------- audio

    pang::platform::AudioOutput output;
    struct Bridge {
        pang::core::Engine* engine = nullptr;
        int channels = kChannels;
    } bridge;

    auto render = [](void* user, float* out, std::uint32_t frames) {
        auto* b = static_cast<Bridge*>(user);
        if (b->engine)
            b->engine->render(out, frames);
        else
            std::memset(out, 0, static_cast<std::size_t>(frames) * b->channels * sizeof(float));
    };

    std::string device_error;
    if (!output.start(kPreferredRate, kChannels, render, &bridge, device_error)) {
        QWidget failure;
        failure.setWindowTitle(QStringLiteral("PlayAmpNG"));
        failure.setStyleSheet(QStringLiteral("background:#2a2a2a; color:#00ff7f;"));
        auto* layout = new QVBoxLayout(&failure);
        layout->addWidget(new QLabel(QStringLiteral("Sem dispositivo de audio:\n%1")
                                         .arg(QString::fromStdString(device_error))));
        failure.show();
        return app.exec();
    }

    auto engine = std::make_unique<pang::core::Engine>(output.sample_rate(), output.channels());
    bridge.engine = engine.get();
    bridge.channels = output.channels();
    auto controller = std::make_unique<pang::core::Controller>(
        *engine, [&output] { output.suspend(); }, [&output] { output.resume(); });

    // -------------------------------------------------------------- skin

    // A aparencia e um skin no formato do Winamp 2.x. Nao ha selecao na
    // interface — a secao 2 da especificacao proibe; o que se adota e o
    // FORMATO, e o arquivo carregado e a aparencia fixa. Aceita .wsz ou pasta.
    pang::ui::skin::WinampSkin skin;
    QString skin_error;
    const QString skin_path = QStringLiteral(PLAYAMPNG_SKIN_DIR "/default");
    if (!skin.load(skin_path, skin_error)) {
        pang::core::log::error(skin_error.toStdString());
        return 1;
    }
    if (!skin.missing().isEmpty())
        pang::core::log::warn(("skin sem os bitmaps: " +
                               skin.missing().join(QStringLiteral(", ")).toStdString()));

    pang::ui::settings::AppState saved = pang::ui::settings::load();

    // Confere que a premissa acima se sustenta: se algum ambiente ainda
    // entregar ratio diferente de 1, o desenho sai reamostrado e e melhor
    // dizer do que deixar o usuario achar que o player e borrado.
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        if (screen->devicePixelRatio() != 1.0)
            pang::core::log::warn(
                "o sistema entregou devicePixelRatio != 1; o desenho sera reamostrado");

        // AP-12 — aviso de saida com escala fracionaria.
        //
        // O player desenha pixel a pixel e escala por multiplos inteiros. Se a
        // SAIDA tiver escala fracionaria, o resultado e reamostrado depois do
        // nosso buffer, e nada no aplicativo alcanca isso: com saida em 1,6 e
        // arte em 2x, cada pixel de arte vira 3,2 pixels de tela, entao uma
        // barra de 3 px alterna entre 9 e 10 px e toda aresta borra.
        //
        // Medido nesta maquina: janela de 550x232 logicos -> framebuffer de
        // 880x371. Escalas de arte 1, 2, 3 e 4 dao 1,6 / 3,2 / 4,8 / 6,4.
        //
        // Dizer isso em voz alta e melhor que deixar o usuario concluir que o
        // player e borrado.
        const QSize logical = screen->geometry().size();
        const QSize physical = screen->size() * screen->devicePixelRatio();
        const qreal ratio = logical.width() > 0
                                ? static_cast<qreal>(physical.width()) / logical.width()
                                : 1.0;
        if (std::fabs(ratio - std::round(ratio)) > 0.01)
            pang::core::log::warn(
                "a saida usa escala fracionaria; o desenho sera reamostrado e as "
                "arestas perdem nitidez. Para o player ficar exato, use uma escala "
                "inteira de monitor (1 ou 2).");
    }
    skin.set_scale(saved.scale);

    engine->set_volume(saved.volume);
    engine->set_balance(saved.balance);
    engine->set_replaygain_mode(static_cast<pang::core::ReplayGainMode>(saved.replaygain_mode));
    pang::core::dsp::apply(engine->equalizer(), saved.eq);  // EQ-09
    controller->set_shuffle(saved.shuffle);
    controller->set_repeat(static_cast<Repeat>(saved.repeat));

    // ------------------------------------------------------------ paineis

    auto* main_panel = new pang::ui::MainPanel(*controller, *engine, skin);
    auto* equalizer_panel = new pang::ui::EqualizerPanel(engine->equalizer(), saved.user_presets, skin);
    auto* playlist_panel = new pang::ui::PlaylistPanel(*controller, skin);
    playlist_panel->set_logical_height(saved.playlist_geometry[3] > 0
                                           ? saved.playlist_geometry[3]
                                           : 150);

    main_panel->set_visualization(
        saved.visualization == 1   ? pang::ui::MainPanel::Visualization::Scope
        : saved.visualization == 2 ? pang::ui::MainPanel::Visualization::Off
                                   : pang::ui::MainPanel::Visualization::Spectrum);

    // DEFEITOS.md A-1, A-2, A-3 — uma janela so. Os paineis abrem e fecham
    // juntos, tem a mesma largura, e nao ha janela primaria para se perder.
    pang::ui::shell::IntegratedShell shell(main_panel, equalizer_panel, playlist_panel, skin);
    shell.setWindowTitle(QStringLiteral("PlayAmpNG"));
    shell.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    shell.set_equalizer_visible(saved.equalizer_visible);
    shell.set_playlist_visible(saved.playlist_visible);
    if (saved.detached) shell.set_detached(true);
    if (saved.compact) shell.set_compact(true);

    // ------------------------------------------------- metadados assincronos

    std::mutex pending_mutex;
    std::vector<pang::core::meta::Scanner::Result> pending;
    pang::core::meta::Scanner scanner([&](const pang::core::meta::Scanner::Result& result) {
        std::lock_guard<std::mutex> lock(pending_mutex);
        pending.push_back(result);
    });

    const auto scan_missing = [&] {
        std::vector<std::pair<std::uint64_t, std::string>> work;
        const pang::core::Playlist& pl = controller->playlist();
        for (int i = 0; i < pl.size(); ++i)
            if (!pl.at(i).metadata_loaded) work.emplace_back(pl.at(i).id, pl.at(i).path);
        scanner.enqueue_many(std::move(work));
    };

    const auto add_paths = [&](const QStringList& paths) {
        for (const QString& p : paths) {
            const QFileInfo info(p);
            if (info.isDir())
                controller->playlist().add_directory(p.toStdString(), true);  // LI-03
            else
                controller->playlist().add(p.toStdString());
        }
        controller->playlist_changed();
        playlist_panel->refresh();
        scan_missing();
    };

    const auto open_files = [&] {
        const QStringList paths = QFileDialog::getOpenFileNames(&shell, QStringLiteral("Abrir"));
        if (paths.isEmpty()) return;
        const bool was_empty = controller->playlist().empty();
        add_paths(paths);
        if (was_empty) controller->play_index(0);
    };

    // ------------------------------------------------------------- ligacoes

    main_panel->on_open = open_files;
    main_panel->on_toggle_equalizer = [&shell] {
        shell.set_equalizer_visible(!shell.equalizer_visible());
    };
    main_panel->on_toggle_playlist = [&shell] {
        shell.set_playlist_visible(!shell.playlist_visible());
    };
    main_panel->equalizer_visible = [&shell] { return shell.equalizer_visible(); };
    main_panel->playlist_visible = [&shell] { return shell.playlist_visible(); };
    main_panel->on_scale_changed = [&shell](int scale) { shell.set_scale(scale); };
    main_panel->on_toggle_compact = [&shell] { shell.set_compact(!shell.compact()); };

    playlist_panel->on_add_files = open_files;
    playlist_panel->on_add_directory = [&] {
        const QString dir =
            QFileDialog::getExistingDirectory(&shell, QStringLiteral("Adicionar pasta"));
        if (!dir.isEmpty()) add_paths({dir});
    };
    playlist_panel->on_drop = add_paths;  // LI-02
    playlist_panel->on_height_changed = [&shell] { shell.relayout(); };
    playlist_panel->on_import = [&] {
        const QString file =
            QFileDialog::getOpenFileName(&shell, QStringLiteral("Importar playlist"), {},
                                         QStringLiteral("Playlists (*.m3u *.m3u8 *.pls)"));
        if (file.isEmpty()) return;
        std::string error;
        const auto tracks = pang::core::playlist_io::load(file.toStdString(), error);
        for (const pang::core::Track& t : tracks) {
            const int index = controller->playlist().add(t.path);
            pang::core::Track seed = t;
            seed.metadata_loaded = false;
            controller->playlist().apply_metadata(controller->playlist().at(index).id, seed);
        }
        controller->playlist_changed();
        playlist_panel->refresh();
        scan_missing();
    };
    playlist_panel->on_export = [&] {
        const QString file = QFileDialog::getSaveFileName(
            &shell, QStringLiteral("Exportar playlist"), QStringLiteral("playlist.m3u8"),
            QStringLiteral("Playlists (*.m3u8 *.m3u *.pls)"));
        if (file.isEmpty()) return;
        std::string error;
        pang::core::playlist_io::save(file.toStdString(), controller->playlist().tracks(), error);
    };

    // ------------------------------------------------------------ temporizadores

    auto* frame_timer = new QTimer(&shell);
    QObject::connect(frame_timer, &QTimer::timeout, [main_panel] { main_panel->tick(0.016f); });
    frame_timer->start(16);  // VI-18

    auto* slow_timer = new QTimer(&shell);
    QObject::connect(slow_timer, &QTimer::timeout, [&] {
        controller->poll();  // PL-24

        std::vector<pang::core::meta::Scanner::Result> ready;
        {
            std::lock_guard<std::mutex> lock(pending_mutex);
            ready.swap(pending);
        }
        if (!ready.empty()) {
            for (const auto& result : ready)
                controller->playlist().apply_metadata(result.id, result.track);
            playlist_panel->refresh();
        }
        if (equalizer_panel->isVisible()) equalizer_panel->refresh();
    });
    slow_timer->start(100);

    // ------------------------------------------------------ posicao da janela

    const auto screens = [] {
        QVector<QRect> rects;
        for (const QScreen* screen : QGuiApplication::screens())
            rects.append(screen->availableGeometry());
        return rects;
    };
    const QRect primary = QGuiApplication::primaryScreen()->availableGeometry();

    if (saved.main_geometry[2] > 0) {
        const QRect wanted(saved.main_geometry[0], saved.main_geometry[1], shell.width(),
                           shell.height());
        // AP-14 — a geometria salva passa pela regra de recuperacao.
        const QRect safe = pang::ui::shell::recover(wanted, screens(), primary);
        if (safe != wanted) pang::core::log::warn("janela fora da area visivel; reposicionada");
        shell.move(safe.topLeft());
    }

    QObject::connect(&app, &QApplication::aboutToQuit, [&] {
        saved.volume = engine->volume();
        saved.balance = engine->balance();
        saved.shuffle = controller->shuffle();
        saved.repeat = static_cast<int>(controller->repeat());
        saved.replaygain_mode = static_cast<int>(engine->replaygain_mode());
        saved.eq = pang::core::dsp::capture(engine->equalizer());
        saved.scale = skin.scale();
        saved.visualization =
            main_panel->visualization() == pang::ui::MainPanel::Visualization::Scope   ? 1
            : main_panel->visualization() == pang::ui::MainPanel::Visualization::Off   ? 2
                                                                                       : 0;
        saved.detached = shell.detached();
        saved.compact = shell.compact();
        saved.playlist_visible = shell.playlist_visible();
        saved.equalizer_visible = shell.equalizer_visible();
        saved.main_geometry = {shell.x(), shell.y(), shell.width(), shell.height()};
        saved.playlist_geometry = {0, 0, pang::ui::PlaylistPanel::kWidth,
                                   playlist_panel->logical_height()};
        pang::ui::settings::save(saved);  // IN-09
    });

    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        add_paths(args);
        controller->play_index(0);
    }

    shell.show();
    main_panel->setFocus();
    return app.exec();
}
