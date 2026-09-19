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
#include <QMenu>
#include <QActionGroup>
#include <QMessageBox>
#include <QTextStream>
#include <QCryptographicHash>
#include <QDir>
#include <QTimer>
#include <QGuiApplication>
#include <QScreen>

#include <QWidget>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

#ifdef _WIN32
// NOMINMAX vem do CMake, e nao daqui: windows.h define min e max como macro e
// quebraria todo numeric_limits<T>::max() da unidade de traducao.
#include <cstdio>
#include <windows.h>
#endif

#include "core/dsp/presets.h"
#include "core/util/log.h"
#include "core/meta/scanner.h"
#include "core/playlist/m3u.h"
#include "core/state/controller.h"
#include "platform/audio_device.h"
#include "core/audio/probe.h"
#include "platform/integration.h"
#include "ui/panel/equalizer_panel.h"
#include "ui/panel/main_panel.h"
#include "ui/panel/playlist_panel.h"
#include "ui/shell/integrated.h"
#include "ui/settings.h"
#include "ui/single_instance.h"
#include "ui/shell/recovery.h"
#include "ui/skin/winamp_skin.h"

using pang::core::Repeat;
using pang::core::State;

namespace {

constexpr int kPreferredRate = 44100;
constexpr int kChannels = 2;



}  // namespace

namespace {

// IN-09 — encerramento pelo SISTEMA tambem precisa gravar.
//
// QApplication::aboutToQuit nao dispara com SIGTERM, que e como um logout ou
// um desligamento pedem ao programa que termine. Sem isto, a configuracao e a
// playlist da sessao se perdiam sempre que o player nao era fechado pelo botao.
//
// O tratador so levanta uma bandeira: chamar Qt de dentro dele nao e seguro.
// Quem realmente encerra e o tique da interface, que ja existe.
std::atomic<bool> g_termination_requested{false};

extern "C" void request_termination(int) {
    g_termination_requested.store(true, std::memory_order_relaxed);
}

}  // namespace

#ifdef _WIN32
// O binario do Windows e de subsistema GUI, e por isso nasce SEM console: o
// log em stderr some, inclusive a linha que diz se a integracao com o painel
// de midia subiu. Quem roda pelo PowerShell recebe o log no proprio terminal;
// quem abre pelo atalho continua sem console, como deve ser.
void attach_parent_console() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    FILE* dummy = nullptr;
    freopen_s(&dummy, "CONOUT$", "w", stderr);
    freopen_s(&dummy, "CONOUT$", "w", stdout);
}
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    attach_parent_console();
#endif
    std::signal(SIGTERM, request_termination);
    std::signal(SIGINT, request_termination);

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

    // IN-08 — instancia unica, decidida ANTES de abrir dispositivo de audio ou
    // janela: um segundo lancamento que fosse ate la tomaria o dispositivo e
    // piscaria uma janela antes de desistir.
    //
    // A chave deriva do ARQUIVO DE CONFIGURACAO, e nao do nome do usuario.
    // Duas execucoes com configuracoes diferentes sao dois players: e o que
    // permite um segundo perfil, e foi o que faltou quando um teste de
    // execucao prolongada, com configuracao propria, entregou seus arquivos ao
    // player que o usuario tinha aberto e saiu sem tocar nada.
    //
    // O caminho ja inclui o diretorio do usuario, entao duas contas na mesma
    // maquina continuam separadas.
    pang::ui::SingleInstance instance(
        QStringLiteral("playampng-%1")
            .arg(QString::fromLatin1(
                QCryptographicHash::hash(pang::ui::settings::config_file_path().toUtf8(),
                                         QCryptographicHash::Sha1)
                    .toHex()
                    .left(16))));
    if (!instance.primary()) {
        const QStringList paths = parser.positionalArguments();
        if (!paths.isEmpty() && instance.send(paths)) {
            pang::core::log::info("arquivos entregues a instancia ja aberta");
            return 0;
        }
        if (paths.isEmpty()) {
            // Sem arquivos a entregar, o util e trazer a janela existente para
            // a frente — que e o que o usuario quis ao lancar de novo.
            instance.send(QStringList{});
            return 0;
        }
        pang::core::log::warn("a instancia existente nao respondeu; abrindo outra janela");
    }

    // A configuracao e lida antes do audio porque o dispositivo de saida
    // escolhido pelo usuario (AU-11) e um dado dela: abrir no padrao e trocar
    // depois faria o player soar um instante no lugar errado.
    pang::ui::settings::AppState saved = pang::ui::settings::load();

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
    if (!output.start(kPreferredRate, kChannels, render, &bridge, device_error,
                      saved.audio_device)) {
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

    // EN-04 — o skin e procurado ao lado do BINARIO, e so depois no diretorio
    // de fontes. Fixar o caminho de compilacao funcionava enquanto o player
    // rodava da arvore de build; instalado num pacote, aquele caminho nao
    // existe e a janela abriria sem desenho nenhum.
    //
    // A ordem cobre os tres casos: instalado em prefixo (incluindo AppImage e
    // Flatpak, onde o prefixo e relativo ao executavel), skin ao lado do
    // binario, e arvore de desenvolvimento.
    const QString exe_dir = QCoreApplication::applicationDirPath();
    const QStringList skin_candidates{
        exe_dir + QStringLiteral("/../share/playampng/skin/default"),
        exe_dir + QStringLiteral("/skin/default"),
        QStringLiteral(PLAYAMPNG_SKIN_DIR "/default"),
    };
    QString skin_path;
    for (const QString& candidate : skin_candidates) {
        if (!QFileInfo::exists(candidate)) continue;
        if (skin.load(candidate, skin_error)) {
            skin_path = candidate;
            break;
        }
    }
    if (skin_path.isEmpty()) {
        pang::core::log::error("skin nao encontrado em nenhum de: " +
                               skin_candidates.join(QStringLiteral(", ")).toStdString() +
                               (skin_error.isEmpty() ? "" : " (" + skin_error.toStdString() + ")"));
        return 1;
    }
    pang::core::log::info("skin: " + QDir::cleanPath(skin_path).toStdString());
    if (!skin.missing().isEmpty())
        pang::core::log::warn(("skin sem os bitmaps: " +
                               skin.missing().join(QStringLiteral(", ")).toStdString()));

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
    if (saved.always_on_top) shell.set_always_on_top(true);  // IN-06

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

    // MD-03 — janela de propriedades. Campo que a fonte nao informa aparece
    // como "nao informado", e nunca como zero ou estimativa: inventar numero
    // aqui e pior que admitir a ausencia.
    auto show_properties = [&] {
        const int index = controller->current_index();
        if (index < 0 || index >= controller->playlist().size()) return;
        const auto& track = controller->playlist().at(index);

        std::string probe_error;
        const auto info = pang::core::probe(track.path, probe_error);
        const auto snap = engine->snapshot();

        const auto ou = [](const QString& v) { return v.isEmpty() ? QStringLiteral("nao informado") : v; };
        QString text;
        QTextStream out(&text);
        out << "Localizacao\n" << QString::fromStdString(track.path) << "\n\n";
        if (info) {
            out << "Formato       " << QString::fromStdString(info->format) << "\n";
            out << "Codec         " << QString::fromStdString(info->codec) << "\n";
            out << "Taxa          " << (info->sample_rate ? QString::number(info->sample_rate) + " Hz"
                                                          : QStringLiteral("nao informada")) << "\n";
            out << "Canais        " << (info->channels ? QString::number(info->channels)
                                                       : QStringLiteral("nao informado")) << "\n";
            out << "Bitrate       " << (info->bitrate_bps
                                            ? QString::number(*info->bitrate_bps / 1000) + " kbps"
                                            : QStringLiteral("nao informado")) << "\n";
            out << "Duracao       " << (info->duration_us
                                            ? QStringLiteral("%1 s").arg(*info->duration_us / 1e6, 0, 'f', 2)
                                            : QStringLiteral("nao informada")) << "\n";
            out << "Pesquisavel   " << (info->seekable ? QStringLiteral("sim") : QStringLiteral("nao")) << "\n";
            out << "Fonte ao vivo " << (info->live ? QStringLiteral("sim") : QStringLiteral("nao")) << "\n";
            if (info->station)
                out << "Estacao       " << QString::fromStdString(*info->station) << "\n";
            out << "ReplayGain    "
                << (info->replaygain_track_db
                        ? QStringLiteral("%1 dB (faixa)").arg(*info->replaygain_track_db, 0, 'f', 2)
                        : info->replaygain_album_db
                              ? QStringLiteral("%1 dB (album)").arg(*info->replaygain_album_db, 0, 'f', 2)
                              : QStringLiteral("ausente na fonte"))
                << "\n";
        } else {
            out << "Nao foi possivel ler a fonte:\n"
                << QString::fromStdString(probe_error) << "\n";
        }
        out << "\nTags\n";
        out << "Titulo        " << ou(QString::fromStdString(track.title)) << "\n";
        out << "Artista       " << ou(QString::fromStdString(track.artist)) << "\n";
        out << "Album         " << ou(QString::fromStdString(track.album)) << "\n";
        out << "Genero        " << ou(QString::fromStdString(track.genre)) << "\n";
        out << "Ano           " << (track.year ? QString::number(track.year)
                                               : QStringLiteral("nao informado")) << "\n";
        out << "\nSaida\n";
        out << "Dispositivo   " << QString::fromStdString(output.device_name()) << "\n";
        out << "Backend       " << QString::fromStdString(pang::platform::backend_name()) << "\n";
        out << "Taxa da saida " << snap.sample_rate << " Hz, " << snap.channels << " canais\n";
        out << "FFmpeg        " << QString::fromStdString(pang::core::ffmpeg_version())
            << " (" << QString::fromStdString(pang::core::ffmpeg_license()) << ")\n";

        QMessageBox box(&shell);
        box.setWindowTitle(QStringLiteral("Propriedades da faixa"));
        box.setTextInteractionFlags(Qt::TextSelectableByMouse);  // o caminho precisa ser copiavel
        box.setText(text);
        box.exec();
    };

    // IN-01 — atalhos documentados, e documentados ONDE o usuario esta. Uma
    // lista so no README nao ajuda quem ja abriu o programa.
    auto show_shortcuts = [&] {
        QMessageBox box(&shell);
        box.setWindowTitle(QStringLiteral("Atalhos de teclado"));
        box.setText(QStringLiteral(
            "Reproducao\n"
            "  Espaco        tocar / pausar\n"
            "  Z X C V B     anterior, tocar, pausar, parar, proxima\n"
            "  Setas < >     retroceder e avancar 5 s\n"
            "  Setas ^ v     volume\n"
            "\nJanela\n"
            "  Ctrl+1/2/3    escala 1x, 2x, 3x\n"
            "  Ctrl+W        modo barra\n"
            "  Ctrl+D        separar ou juntar os paineis\n"
            "  Ctrl+E        equalizador\n"
            "  Ctrl+P        playlist\n"
            "  Ctrl+T        manter acima das demais janelas\n"
            "\nPlaylist\n"
            "  Delete        remover a selecao\n"
            "  Enter         tocar a selecao\n"
            "  digitar       busca por prefixo\n"
            "\nTeclas de midia do teclado e do ambiente de trabalho tambem\n"
            "controlam o player."));
        box.exec();
    };

    // AU-11 — menu de contexto com a escolha do dispositivo de saida.
    //
    // A lista e enumerada na hora de abrir o menu, nao guardada: dispositivo
    // de audio aparece e some enquanto o programa roda, e uma lista montada no
    // inicio mostraria fone que ja foi desconectado.
    main_panel->on_context_menu = [&, main_panel](const QPoint& at) {
        QMenu menu;
        QMenu* devices = menu.addMenu(QStringLiteral("Dispositivo de saida"));

        auto* group = new QActionGroup(devices);
        group->setExclusive(true);

        QAction* system_default = devices->addAction(QStringLiteral("Padrao do sistema"));
        system_default->setCheckable(true);
        system_default->setChecked(saved.audio_device.empty());
        group->addAction(system_default);
        devices->addSeparator();

        std::string list_error;
        for (const auto& device : pang::platform::list_playback_devices(list_error)) {
            QAction* action = devices->addAction(QString::fromStdString(device.name));
            action->setCheckable(true);
            action->setChecked(device.name == saved.audio_device);
            action->setData(QString::fromStdString(device.name));
            group->addAction(action);
        }
        if (!list_error.empty())
            devices->addAction(QString::fromStdString(list_error))->setEnabled(false);

        // Em falha o menu precisa dizer que ha falha, e nao so oferecer a lista:
        // o usuario chegou aqui justamente porque parou de sair som.
        if (output.health() == pang::platform::AudioOutput::Health::Failed) {
            menu.addSeparator();
            QAction* failed = menu.addAction(
                QStringLiteral("Saida indisponivel: %1")
                    .arg(QString::fromStdString(output.last_error())));
            failed->setEnabled(false);
        }

        menu.addSeparator();

        // MD-03 — propriedades tecnicas e localizacao do arquivo. Os dados vem
        // do probe, e nao da playlist: o que interessa aqui e o que a FONTE
        // informou, incluindo o que ela nao informou.
        QAction* properties = menu.addAction(QStringLiteral("Propriedades da faixa"));
        properties->setEnabled(controller->current_index() >= 0);

        QAction* shortcuts = menu.addAction(QStringLiteral("Atalhos de teclado"));

        QAction* chosen = menu.exec(at);
        if (chosen == properties) {
            show_properties();
            return;
        }
        if (chosen == shortcuts) {
            show_shortcuts();
            return;
        }
        if (!chosen || !chosen->isCheckable()) return;

        const std::string wanted =
            chosen == system_default ? std::string{} : chosen->data().toString().toStdString();
        std::string error;
        if (output.switch_to(wanted, error)) {
            saved.audio_device = wanted;
            pang::ui::settings::save(saved);
        } else {
            pang::core::log::error("nao foi possivel usar o dispositivo: " + error);
        }
        main_panel->update();
    };

    // ------------------------------------------- integracao com o ambiente
    //
    // IN-05 — MPRIS no Linux. IN-04 sai junto: as teclas de midia sao
    // encaminhadas pelo ambiente ao player registrado, em vez de cada programa
    // capturar a tecla por conta propria — que so funcionaria com a janela em
    // foco e brigaria com os outros players.
    pang::platform::Commands commands;
    commands.play = [&] { controller->play(); };
    commands.pause = [&] { controller->pause(); };
    commands.play_pause = [&] {
        const auto s = engine->snapshot().state;
        if (s == pang::core::State::Playing) controller->pause(); else controller->play();
    };
    commands.stop = [&] { controller->stop(); };
    commands.next = [&] { controller->next(); };
    commands.previous = [&] { controller->previous(); };
    commands.set_volume = [&](float v) { engine->set_volume(std::clamp(v, 0.0f, 1.0f)); };
    commands.seek = [&](std::int64_t offset_us) {
        const auto snap = engine->snapshot();
        if (!snap.seekable || snap.sample_rate <= 0) return;
        const double now = double(snap.position_frames) / snap.sample_rate;
        controller->seek(std::max(0.0, now + double(offset_us) / 1e6));
    };
    commands.set_position = [&](std::int64_t position_us) {
        if (engine->snapshot().seekable) controller->seek(double(position_us) / 1e6);
    };
    commands.raise = [&] {
        shell.show();
        shell.raise();
        shell.activateWindow();
    };
    commands.quit = [&] { QCoreApplication::quit(); };

    // winId() cria a janela nativa mesmo antes de show(), que e o que o SMTC
    // do Windows precisa. Nos outros sistemas o valor e ignorado.
    auto integration = pang::platform::make_integration(
        "PlayAmpNG", std::move(commands), reinterpret_cast<void*>(shell.winId()));
    pang::core::log::info("integracao com o ambiente: " + integration->backend());

    // Onde a configuracao e a sessao sao gravadas. Vai para o log porque o
    // caminho depende do SISTEMA — o Qt usa ~/.config no Linux, Application
    // Support no macOS e AppData no Windows — e quem investiga "minhas
    // preferencias nao ficam salvas" precisa saber onde procurar. Os testes
    // tambem leem daqui, em vez de presumir o caminho: presumir foi o que
    // fez a suite reprovar no macOS por motivo nenhum.
    pang::core::log::info("configuracao: " +
                          pang::ui::settings::config_file_path().toStdString());

    auto publish_now_playing = [&] {
        if (!integration->available()) return;
        const auto snap = engine->snapshot();
        pang::platform::NowPlaying now;
        const int index = controller->current_index();
        if (index >= 0 && index < controller->playlist().size()) {
            const auto& track = controller->playlist().at(index);
            now.title = track.display_title();
            now.artist = track.artist;
            now.album = track.album;
            now.url = track.path;
        }
        // MD-05 — em stream, o que o servidor diz estar tocando vale mais que o
        // nome do arquivo, que nem existe.
        if (const std::string icy = engine->icy_title(); !icy.empty()) now.title = icy;
        if (const std::string station = engine->station(); !station.empty()) now.album = station;

        // MD-09 — duracao so vai ao barramento quando a fonte informa.
        now.duration_us = snap.duration_frames > 0 && snap.sample_rate > 0
                              ? snap.duration_frames * 1'000'000LL / snap.sample_rate
                              : -1;
        now.position_us = snap.sample_rate > 0
                              ? snap.position_frames * 1'000'000LL / snap.sample_rate
                              : 0;
        now.playing = snap.state == pang::core::State::Playing;
        now.stopped = snap.state == pang::core::State::Stopped ||
                      snap.state == pang::core::State::Error;
        now.can_seek = snap.seekable;
        now.can_go_next = controller->playlist().size() > 1;
        now.can_go_previous = controller->playlist().size() > 1;
        now.volume = engine->volume();
        integration->publish(now);
    };

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
            playlist_panel->refresh(/*force=*/true);
        }
        // A faixa em reproducao pode mudar sem metadado novo — pelo botao,
        // pelo fim da faixa ou pelo barramento. O painel precisa saber.
        playlist_panel->refresh();
        if (equalizer_panel->isVisible()) equalizer_panel->refresh();
        publish_now_playing();

        if (g_termination_requested.load(std::memory_order_relaxed)) {
            pang::core::log::info("encerramento pedido pelo sistema");
            QCoreApplication::quit();  // dispara aboutToQuit, que grava tudo
            return;
        }

        // RB-07 — interrupcao de audio vai para o log quando acontece.
        //
        // Nao e instrumentacao so de teste: e o que permite a alguem que relata
        // "o som picota" mandar o log e a conversa comecar com um numero. So
        // registra quando o contador ANDA, senao o log vira ruido.
        {
            static std::uint32_t reported_underruns = 0;
            const auto snap = engine->snapshot();
            if (snap.underruns > reported_underruns) {
                pang::core::log::warn("interrupcoes de audio: " +
                                      std::to_string(snap.underruns) + " (+" +
                                      std::to_string(snap.underruns - reported_underruns) + ")");
                reported_underruns = snap.underruns;
            }
        }

        // AU-12, RB-05 — vigia o dispositivo no tique que ja existe, em vez de
        // criar um thread so para isso. A reabertura acontece aqui, no thread
        // da interface, e nao dentro do callback do proprio dispositivo.
        static bool device_failure_reported = false;
        switch (output.poll(100)) {
            case pang::platform::AudioOutput::Health::Failed:
                if (!device_failure_reported) {
                    device_failure_reported = true;
                    pang::core::log::error("saida de audio: " + output.last_error());
                    engine->pause();  // RB-05 — para explicitamente, nao em silencio
                }
                break;
            case pang::platform::AudioOutput::Health::Ok:
                device_failure_reported = false;
                break;
            default:
                break;
        }
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
        saved.always_on_top = shell.always_on_top();
        saved.main_geometry = {shell.x(), shell.y(), shell.width(), shell.height()};
        saved.playlist_geometry = {0, 0, pang::ui::PlaylistPanel::kWidth,
                                   playlist_panel->logical_height()};
        pang::ui::settings::save(saved);  // IN-09

        // IN-09 — a playlist da sessao, no mesmo formato que o usuario importa
        // e exporta. Assim a sessao restaurada abre em qualquer outro player, e
        // o arquivo continua legivel se algo der errado.
        std::string playlist_error;
        if (!pang::core::playlist_io::save(
                pang::ui::settings::session_playlist_path().toStdString(),
                controller->playlist().tracks(), playlist_error))
            pang::core::log::warn("playlist da sessao nao foi gravada: " + playlist_error);
    });

    // IN-08 — entrega vinda de um segundo lancamento. Enfileira sem
    // interromper o que toca: quem deu duplo clique num arquivo com o player
    // tocando nao pediu para cortar a faixa atual.
    instance.on_files = [&](const QStringList& paths) {
        if (!paths.isEmpty()) {
            const bool was_empty = controller->playlist().size() == 0;
            add_paths(paths);
            if (was_empty) controller->play_index(0);
        }
        shell.show();
        shell.raise();
        shell.activateWindow();
    };

    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        add_paths(args);
        controller->play_index(0);
    } else {
        // IN-11 — a sessao anterior volta PARADA. Retomar o som sozinho ao
        // abrir e comportamento que surpreende: o usuario abriu o player, nao
        // pediu para tocar. So a preferencia explicita muda isso.
        std::string playlist_error;
        const auto restored = pang::core::playlist_io::load(
            pang::ui::settings::session_playlist_path().toStdString(), playlist_error);
        if (!restored.empty()) {
            // Pelo MESMO caminho de abrir arquivos, e nao por uma insercao
            // propria. A versao anterior inseria direto na playlist e pulava a
            // varredura de metadados, entao a sessao restaurada aparecia
            // inteira com duracao "--:--" e total zerado — defeito que so se
            // via depois de fechar e abrir o player.
            QStringList paths;
            paths.reserve(static_cast<int>(restored.size()));
            for (const auto& track : restored)
                paths << QString::fromStdString(track.path);
            add_paths(paths);
            pang::core::log::info("sessao restaurada: " + std::to_string(restored.size()) +
                                  " faixa(s)");
            if (saved.autoplay_on_restore) controller->play_index(0);
        }
    }

    shell.show();
    main_panel->setFocus();
    return app.exec();
}
