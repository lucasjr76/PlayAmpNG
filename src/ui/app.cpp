// Interface provisoria do M2.
//
// Nao tenta parecer com o Winamp: a aparencia definitiva depende do atlas de
// sprites e chega no M5. O que esta janela entrega e reproducao real com
// playlist de verdade, para exercitar a mao o que os testes ja cobrem headless.

#include <QAbstractListModel>
#include <QApplication>
#include <QCommandLineParser>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMimeData>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QWidget>

#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#include "core/meta/scanner.h"
#include "core/playlist/m3u.h"
#include "core/state/controller.h"
#include "platform/audio_device.h"

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

// LI-17 — QListView sobre um modelo, e nao QListWidget: so as linhas visiveis
// sao consultadas, entao dez mil itens custam o mesmo que dez.
class PlaylistModel : public QAbstractListModel {
public:
    explicit PlaylistModel(pang::core::Playlist& playlist) : playlist_(playlist) {}

    int rowCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : playlist_.size();
    }

    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() >= playlist_.size()) return {};
        const pang::core::Track& t = playlist_.at(index.row());

        switch (role) {
            case Qt::DisplayRole:
                return QStringLiteral("%1. %2   %3")
                    .arg(index.row() + 1)
                    .arg(QString::fromStdString(t.display_title()))
                    .arg(format_ms(t.duration_ms));
            case Qt::ForegroundRole:
                // LI-10 — a faixa em reproducao se distingue da selecionada.
                return index.row() == current_ ? QColor(0, 255, 127) : QColor(160, 160, 160);
            case Qt::FontRole: {
                QFont f;
                f.setBold(index.row() == current_);
                return f;
            }
            default:
                return {};
        }
    }

    void refresh() {
        beginResetModel();
        endResetModel();
    }

    void set_current(int row) {
        if (row == current_) return;
        const int previous = current_;
        current_ = row;
        for (int r : {previous, current_})
            if (r >= 0 && r < playlist_.size()) emit dataChanged(index(r), index(r));
    }

    void row_changed(int row) {
        if (row >= 0 && row < playlist_.size()) emit dataChanged(index(row), index(row));
    }

private:
    pang::core::Playlist& playlist_;
    int current_ = -1;
};

// Janela com drag-and-drop (LI-02).
class Window : public QWidget {
public:
    std::function<void(const QStringList&)> on_drop;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasUrls()) event->acceptProposedAction();
    }
    void dropEvent(QDropEvent* event) override {
        QStringList paths;
        for (const QUrl& url : event->mimeData()->urls())
            if (url.isLocalFile()) paths << url.toLocalFile();
        if (!paths.isEmpty() && on_drop) on_drop(paths);
        event->acceptProposedAction();
    }
};

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
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("PlayAmpNG"));
    app.setApplicationVersion(QStringLiteral(PLAYAMPNG_VERSION));

    // IN-07 — abertura por argumentos de linha de comando.
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
    std::unique_ptr<pang::core::Engine> engine;
    std::unique_ptr<pang::core::Controller> controller;

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
    const bool device_ok =
        output.start(kPreferredRate, kChannels, render, &bridge, device_error);
    if (device_ok) {
        engine = std::make_unique<pang::core::Engine>(output.sample_rate(), output.channels());
        bridge.engine = engine.get();
        bridge.channels = output.channels();
        controller = std::make_unique<pang::core::Controller>(
            *engine, [&output] { output.suspend(); }, [&output] { output.resume(); });
    }

    // ------------------------------------------------------------ janela

    Window window;
    window.setAcceptDrops(true);
    window.setWindowTitle(QStringLiteral("PlayAmpNG (provisorio — M2)"));
    window.resize(720, 520);
    window.setStyleSheet(QStringLiteral(
        "background:#2b2b2b; color:#00ff7f;"
        "QListView{background:#1e1e1e;} QLineEdit{background:#1e1e1e;color:#00ff7f;}"));

    auto* status = new QLabel;
    auto* totals = new QLabel;
    auto* elapsed = new QLabel(QStringLiteral("--:--"));
    auto* duration = new QLabel(QStringLiteral("--:--"));
    auto* position = new QSlider(Qt::Horizontal);
    auto* volume = new QSlider(Qt::Horizontal);
    auto* search = new QLineEdit;
    auto* list = new QListView;

    search->setPlaceholderText(QStringLiteral("buscar..."));
    volume->setRange(0, 100);
    volume->setValue(100);
    position->setRange(0, 1000);
    position->setEnabled(false);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);  // LI-05
    list->setUniformItemSizes(true);

    auto* previous = new QPushButton(QStringLiteral("|<"));
    auto* play = new QPushButton(QStringLiteral("Tocar"));
    auto* pause = new QPushButton(QStringLiteral("Pausar"));
    auto* stop = new QPushButton(QStringLiteral("Parar"));
    auto* next = new QPushButton(QStringLiteral(">|"));
    auto* shuffle = new QPushButton(QStringLiteral("Shuffle"));
    auto* repeat = new QPushButton(QStringLiteral("Repetir: nao"));
    auto* add_files = new QPushButton(QStringLiteral("+ Arquivos"));
    auto* add_dir = new QPushButton(QStringLiteral("+ Pasta"));
    auto* remove_sel = new QPushButton(QStringLiteral("Remover"));
    auto* clear_all = new QPushButton(QStringLiteral("Limpar"));
    auto* import_list = new QPushButton(QStringLiteral("Importar"));
    auto* export_list = new QPushButton(QStringLiteral("Exportar"));
    shuffle->setCheckable(true);

    auto* grid = new QGridLayout(&window);
    int row = 0;
    grid->addWidget(status, row++, 0, 1, 6);
    grid->addWidget(elapsed, row, 0);
    grid->addWidget(position, row, 1, 1, 4);
    grid->addWidget(duration, row++, 5);
    grid->addWidget(new QLabel(QStringLiteral("Vol")), row, 0);
    grid->addWidget(volume, row++, 1, 1, 5);
    grid->addWidget(previous, row, 0);
    grid->addWidget(play, row, 1);
    grid->addWidget(pause, row, 2);
    grid->addWidget(stop, row, 3);
    grid->addWidget(next, row, 4);
    grid->addWidget(shuffle, row++, 5);
    grid->addWidget(repeat, row, 0);
    grid->addWidget(add_files, row, 1);
    grid->addWidget(add_dir, row, 2);
    grid->addWidget(remove_sel, row, 3);
    grid->addWidget(clear_all, row, 4);
    grid->addWidget(import_list, row++, 5);
    grid->addWidget(export_list, row, 0);
    grid->addWidget(search, row++, 1, 1, 5);
    grid->addWidget(list, row++, 0, 1, 6);
    grid->addWidget(totals, row, 0, 1, 6);
    grid->setRowStretch(row - 1, 1);

    if (!device_ok) {
        status->setText(QStringLiteral("sem dispositivo de audio: %1")
                            .arg(QString::fromStdString(device_error)));
        window.show();
        return app.exec();
    }

    auto* model = new PlaylistModel(controller->playlist());
    list->setModel(model);

    // ------------------------------------------------- metadados assincronos
    //
    // O callback roda no thread do scanner. Os resultados sao acumulados aqui e
    // aplicados pelo temporizador, no thread da interface — a playlist tem um
    // dono so.
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
        model->refresh();
        scan_missing();
    };

    window.on_drop = add_paths;  // LI-02

    // ------------------------------------------------------------- acoes

    QObject::connect(add_files, &QPushButton::clicked, [&] {
        const QStringList paths =
            QFileDialog::getOpenFileNames(&window, QStringLiteral("Adicionar arquivos"));
        if (!paths.isEmpty()) add_paths(paths);
    });
    QObject::connect(add_dir, &QPushButton::clicked, [&] {
        const QString dir =
            QFileDialog::getExistingDirectory(&window, QStringLiteral("Adicionar pasta"));
        if (!dir.isEmpty()) add_paths({dir});
    });
    QObject::connect(remove_sel, &QPushButton::clicked, [&] {
        std::vector<int> rows;
        for (const QModelIndex& index : list->selectionModel()->selectedRows())
            rows.push_back(index.row());
        if (rows.empty()) return;
        controller->playlist().remove(rows);  // LI-06 — nao apaga arquivo
        controller->playlist_changed();
        model->refresh();
    });
    QObject::connect(clear_all, &QPushButton::clicked, [&] {
        controller->playlist().clear();
        controller->playlist_changed();
        scanner.drop_pending();
        model->refresh();
    });

    QObject::connect(import_list, &QPushButton::clicked, [&] {
        const QString file = QFileDialog::getOpenFileName(
            &window, QStringLiteral("Importar playlist"), {},
            QStringLiteral("Playlists (*.m3u *.m3u8 *.pls)"));
        if (file.isEmpty()) return;
        std::string error;
        const auto tracks = pang::core::playlist_io::load(file.toStdString(), error);
        for (const pang::core::Track& t : tracks) {
            const int index = controller->playlist().add(t.path);
            // Aproveita titulo e duracao vindos do arquivo; o scanner refina.
            pang::core::Track seed = t;
            seed.metadata_loaded = false;
            controller->playlist().apply_metadata(controller->playlist().at(index).id, seed);
        }
        controller->playlist_changed();
        model->refresh();
        scan_missing();
        if (!error.empty()) status->setText(QString::fromStdString(error));
    });
    QObject::connect(export_list, &QPushButton::clicked, [&] {
        const QString file = QFileDialog::getSaveFileName(
            &window, QStringLiteral("Exportar playlist"), QStringLiteral("playlist.m3u8"),
            QStringLiteral("Playlists (*.m3u8 *.m3u *.pls)"));
        if (file.isEmpty()) return;
        std::string error;
        if (!pang::core::playlist_io::save(file.toStdString(), controller->playlist().tracks(),
                                           error))
            status->setText(QString::fromStdString(error));
    });

    QObject::connect(list, &QListView::doubleClicked,
                     [&](const QModelIndex& index) { controller->play_index(index.row()); });

    QObject::connect(play, &QPushButton::clicked, [&] { controller->play(); });
    QObject::connect(pause, &QPushButton::clicked, [&] { controller->pause(); });
    QObject::connect(stop, &QPushButton::clicked, [&] { controller->stop(); });
    QObject::connect(next, &QPushButton::clicked, [&] { controller->next(); });
    QObject::connect(previous, &QPushButton::clicked, [&] { controller->previous(); });
    QObject::connect(shuffle, &QPushButton::toggled,
                     [&](bool on) { controller->set_shuffle(on); });
    QObject::connect(repeat, &QPushButton::clicked, [&] {
        const Repeat current = controller->repeat();
        const Repeat cycled = current == Repeat::Off     ? Repeat::All
                              : current == Repeat::All   ? Repeat::Track
                                                         : Repeat::Off;
        controller->set_repeat(cycled);
        repeat->setText(QString::fromLatin1(repeat_label(cycled)));
    });

    QObject::connect(volume, &QSlider::valueChanged,
                     [&](int v) { engine->set_volume(static_cast<float>(v) / 100.0f); });

    QObject::connect(position, &QSlider::sliderReleased, [&] {
        const auto snap = engine->snapshot();
        if (snap.duration_frames <= 0) return;
        const double seconds = static_cast<double>(position->value()) / 1000.0 *
                               static_cast<double>(snap.duration_frames) / engine->sample_rate();
        controller->seek(seconds);
    });

    // LI-09 — busca textual: seleciona e rola ate o primeiro resultado.
    QObject::connect(search, &QLineEdit::textChanged, [&](const QString& text) {
        if (text.isEmpty()) return;
        const auto hits = controller->playlist().find(text.toStdString());
        if (hits.empty()) return;
        const QModelIndex index = model->index(hits.front());
        list->setCurrentIndex(index);
        list->scrollTo(index);
    });

    // ------------------------------------------------------------ atualizacao

    auto* timer = new QTimer(&window);
    QObject::connect(timer, &QTimer::timeout, [&] {
        controller->poll();  // PL-24 — avanco no fim da faixa

        // Aplica os metadados lidos em segundo plano.
        std::vector<pang::core::meta::Scanner::Result> ready;
        {
            std::lock_guard<std::mutex> lock(pending_mutex);
            ready.swap(pending);
        }
        for (const auto& result : ready) {
            const int index = controller->playlist().index_of(result.id);
            controller->playlist().apply_metadata(result.id, result.track);
            model->row_changed(index);
        }

        const auto snap = engine->snapshot();
        const int current = controller->current_index();
        model->set_current(current);

        QString line = QString::fromLatin1(pang::core::to_string(snap.state));
        if (current >= 0)
            line = QStringLiteral("[%1/%2] %3  ·  %4")
                       .arg(current + 1)
                       .arg(controller->playlist().size())
                       .arg(QString::fromStdString(
                           controller->playlist().at(current).display_title()))
                       .arg(line);
        if (snap.sample_rate > 0)
            line += QStringLiteral("  ·  %1 Hz  ·  %2")
                        .arg(snap.sample_rate)
                        .arg(snap.channels == 1 ? QStringLiteral("mono")
                                                : QStringLiteral("estereo"));
        // PL-26 — bitrate ausente nao vira numero plausivel.
        line += QStringLiteral("  ·  %1").arg(
            snap.bitrate_bps > 0 ? QStringLiteral("%1 kbps").arg(snap.bitrate_bps / 1000)
                                 : QStringLiteral("bitrate -"));
        if (snap.underruns > 0) line += QStringLiteral("  ·  underruns: %1").arg(snap.underruns);
        if (snap.state == State::Error) line = QString::fromStdString(engine->last_error());
        status->setText(line);

        // LI-11 — soma so as duracoes conhecidas, e diz quantas faltam.
        int unknown = 0;
        const std::int64_t known = controller->playlist().known_duration_ms(&unknown);
        totals->setText(
            QStringLiteral("%1 itens  ·  %2%3")
                .arg(controller->playlist().size())
                .arg(format_ms(known))
                .arg(unknown > 0 ? QStringLiteral(" (+%1 de duracao desconhecida)").arg(unknown)
                                 : QString()));

        elapsed->setText(format_frames(snap.position_frames, engine->sample_rate()));
        duration->setText(format_frames(snap.duration_frames, engine->sample_rate()));

        position->setEnabled(snap.seekable && snap.duration_frames > 0);
        if (!position->isSliderDown() && snap.duration_frames > 0)
            position->setValue(static_cast<int>(1000.0 * snap.position_frames /
                                                static_cast<double>(snap.duration_frames)));
    });
    timer->start(100);

    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        add_paths(args);
        controller->play_index(0);
    }

    window.show();
    return app.exec();
}
