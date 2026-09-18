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
#include "ui/panel/main_panel.h"
#include "ui/settings.h"
#include "ui/shell/recovery.h"
#include "ui/skin/atlas.h"

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

// Painel do equalizador. Provisorio como o resto: no M5 vira sprite.
class EqualizerPanel : public QWidget {
public:
    EqualizerPanel(pang::core::dsp::Equalizer& equalizer,
                   std::vector<pang::core::dsp::EqPreset>& user_presets)
        : equalizer_(equalizer), user_presets_(user_presets) {
        setWindowTitle(QStringLiteral("Equalizador"));
        setStyleSheet(QStringLiteral("background:#2b2b2b; color:#00ff7f;"));

        auto* columns = new QHBoxLayout;

        preamp_ = add_slider(columns, QStringLiteral("Preamp"));
        QObject::connect(preamp_, &QSlider::valueChanged, [this](int value) {
            equalizer_.set_preamp_db(static_cast<float>(value) / 10.0f);
            mark_custom();
        });

        const auto& frequencies = pang::core::dsp::Equalizer::frequencies();
        for (int band = 0; band < pang::core::dsp::Equalizer::kBands; ++band) {
            const float hz = frequencies[static_cast<std::size_t>(band)];
            const QString label = hz >= 1000 ? QStringLiteral("%1k").arg(hz / 1000)
                                             : QStringLiteral("%1").arg(int(hz));
            QSlider* slider = add_slider(columns, label);
            // EQ-12 — banda inativa por Nyquist aparece desabilitada, em vez de
            // aceitar o arraste e nao fazer nada.
            slider->setEnabled(equalizer_.band_active(band));
            QObject::connect(slider, &QSlider::valueChanged, [this, band](int value) {
                equalizer_.set_band_db(band, static_cast<float>(value) / 10.0f);
                mark_custom();
            });
            bands_[static_cast<std::size_t>(band)] = slider;
        }

        presets_ = new QComboBox;
        auto* bypass = new QCheckBox(QStringLiteral("Bypass"));
        auto* reset = new QPushButton(QStringLiteral("Reset"));
        auto* save_preset = new QPushButton(QStringLiteral("Salvar preset"));
        auto* delete_preset = new QPushButton(QStringLiteral("Excluir preset"));

        bypass->setChecked(equalizer_.bypass());
        QObject::connect(bypass, &QCheckBox::toggled,
                         [this](bool on) { equalizer_.set_bypass(on); });
        QObject::connect(reset, &QPushButton::clicked, [this] {
            equalizer_.reset();
            refresh_from_equalizer();
        });
        QObject::connect(presets_, &QComboBox::currentIndexChanged, [this](int index) {
            if (suppress_ || index <= 0) return;
            const auto& builtin = pang::core::dsp::builtin_presets();
            const int offset = index - 1;
            const pang::core::dsp::EqPreset& preset =
                offset < static_cast<int>(builtin.size())
                    ? builtin[static_cast<std::size_t>(offset)]
                    : user_presets_[static_cast<std::size_t>(offset - builtin.size())];
            pang::core::dsp::apply(equalizer_,
                                   pang::core::dsp::from_preset(preset));
            refresh_from_equalizer();
            suppress_ = true;
            presets_->setCurrentIndex(index);
            suppress_ = false;
        });
        QObject::connect(save_preset, &QPushButton::clicked, [this] {
            bool ok = false;
            const QString name = QInputDialog::getText(this, QStringLiteral("Salvar preset"),
                                                       QStringLiteral("Nome:"), QLineEdit::Normal,
                                                       {}, &ok);
            if (!ok || name.isEmpty()) return;
            pang::core::dsp::EqPreset preset;
            preset.name = name.toStdString();
            preset.preamp_db = equalizer_.preamp_db();
            for (int b = 0; b < pang::core::dsp::Equalizer::kBands; ++b)
                preset.bands[static_cast<std::size_t>(b)] = equalizer_.band_db(b);
            user_presets_.push_back(preset);
            reload_presets();
        });
        QObject::connect(delete_preset, &QPushButton::clicked, [this] {
            const int index = presets_->currentIndex() - 1 -
                              static_cast<int>(pang::core::dsp::builtin_presets().size());
            if (index < 0 || index >= static_cast<int>(user_presets_.size())) return;
            user_presets_.erase(user_presets_.begin() + index);
            reload_presets();
        });

        auto* controls = new QHBoxLayout;
        controls->addWidget(presets_);
        controls->addWidget(bypass);
        controls->addWidget(reset);
        controls->addWidget(save_preset);
        controls->addWidget(delete_preset);

        auto* root = new QVBoxLayout(this);
        root->addLayout(columns);
        root->addLayout(controls);

        reload_presets();
        refresh_from_equalizer();
    }

    void refresh_from_equalizer() {
        suppress_ = true;
        preamp_->setValue(static_cast<int>(equalizer_.preamp_db() * 10.0f));
        for (int b = 0; b < pang::core::dsp::Equalizer::kBands; ++b)
            bands_[static_cast<std::size_t>(b)]->setValue(
                static_cast<int>(equalizer_.band_db(b) * 10.0f));
        suppress_ = false;
    }

private:
    QSlider* add_slider(QHBoxLayout* into, const QString& label) {
        auto* column = new QVBoxLayout;
        auto* slider = new QSlider(Qt::Vertical);
        const int range = static_cast<int>(pang::core::dsp::Equalizer::kRangeDb * 10.0f);
        slider->setRange(-range, range);
        slider->setValue(0);
        column->addWidget(slider, 1, Qt::AlignHCenter);
        auto* caption = new QLabel(label);
        caption->setAlignment(Qt::AlignHCenter);
        column->addWidget(caption);
        into->addLayout(column);
        return slider;
    }

    void reload_presets() {
        suppress_ = true;
        presets_->clear();
        presets_->addItem(QStringLiteral("(personalizado)"));
        for (const auto& preset : pang::core::dsp::builtin_presets())
            presets_->addItem(QString::fromStdString(preset.name));
        for (const auto& preset : user_presets_)
            presets_->addItem(QStringLiteral("* %1").arg(QString::fromStdString(preset.name)));
        suppress_ = false;
    }

    void mark_custom() {
        if (suppress_) return;
        suppress_ = true;
        presets_->setCurrentIndex(0);
        suppress_ = false;
    }

    pang::core::dsp::Equalizer& equalizer_;
    std::vector<pang::core::dsp::EqPreset>& user_presets_;
    QSlider* preamp_ = nullptr;
    std::array<QSlider*, pang::core::dsp::Equalizer::kBands> bands_{};
    QComboBox* presets_ = nullptr;
    bool suppress_ = false;
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
    const bool device_ok = output.start(kPreferredRate, kChannels, render, &bridge, device_error);
    if (!device_ok) {
        QWidget failure;
        failure.setWindowTitle(QStringLiteral("PlayAmpNG"));
        failure.setStyleSheet(QStringLiteral("background:#2a2a2a; color:#00ff7f;"));
        auto* layout = new QVBoxLayout(&failure);
        layout->addWidget(new QLabel(QStringLiteral("Sem dispositivo de audio:\n%1")
                                         .arg(QString::fromStdString(device_error))));
        failure.show();
        return app.exec();
    }

    engine = std::make_unique<pang::core::Engine>(output.sample_rate(), output.channels());
    bridge.engine = engine.get();
    bridge.channels = output.channels();
    controller = std::make_unique<pang::core::Controller>(
        *engine, [&output] { output.suspend(); }, [&output] { output.resume(); });

    // -------------------------------------------------------------- skin

    pang::ui::skin::Atlas atlas;
    QString atlas_error;
    if (!atlas.load(QStringLiteral(PLAYAMPNG_SKIN_DIR), atlas_error)) {
        pang::core::log::error(atlas_error.toStdString());
        return 1;
    }

    pang::ui::settings::AppState saved = pang::ui::settings::load();
    atlas.set_scale(saved.scale);

    engine->set_volume(saved.volume);
    engine->set_balance(saved.balance);
    engine->set_replaygain_mode(static_cast<pang::core::ReplayGainMode>(saved.replaygain_mode));
    pang::core::dsp::apply(engine->equalizer(), saved.eq);  // EQ-09
    controller->set_shuffle(saved.shuffle);
    controller->set_repeat(static_cast<Repeat>(saved.repeat));

    // ------------------------------------------------------------ paineis

    pang::ui::MainPanel main_panel(*controller, *engine, atlas);
    main_panel.setWindowTitle(QStringLiteral("PlayAmpNG"));
    // A barra de titulo e desenhada pelo proprio painel, como no classico —
    // por isso a moldura do sistema sai. O sinalizador de dialogo e a dica que
    // faz compositores em modo tiling tratarem a janela como flutuante.
    main_panel.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    main_panel.set_visualization(
        saved.visualization == 1   ? pang::ui::MainPanel::Visualization::Scope
        : saved.visualization == 2 ? pang::ui::MainPanel::Visualization::Off
                                   : pang::ui::MainPanel::Visualization::Spectrum);

    auto* eq_panel = new EqualizerPanel(engine->equalizer(), saved.user_presets);

    // --- painel de playlist
    Window playlist_window;
    playlist_window.setAcceptDrops(true);
    playlist_window.setWindowTitle(QStringLiteral("PlayAmpNG — Playlist"));
    playlist_window.resize(520, 380);
    playlist_window.setStyleSheet(QStringLiteral(
        "background:#2a2a2a; color:#00ff7f;"
        "QListView{background:#181818;} QLineEdit{background:#181818;color:#00ff7f;}"));

    auto* list = new QListView;
    auto* search = new QLineEdit;
    auto* totals = new QLabel;
    auto* status = new QLabel;
    search->setPlaceholderText(QStringLiteral("buscar..."));
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);  // LI-05
    list->setUniformItemSizes(true);                               // LI-17

    auto* add_files = new QPushButton(QStringLiteral("+ Arquivos"));
    auto* add_dir = new QPushButton(QStringLiteral("+ Pasta"));
    auto* remove_sel = new QPushButton(QStringLiteral("Remover"));
    auto* clear_all = new QPushButton(QStringLiteral("Limpar"));
    auto* import_list = new QPushButton(QStringLiteral("Importar"));
    auto* export_list = new QPushButton(QStringLiteral("Exportar"));
    auto* replaygain = new QComboBox;
    replaygain->addItems({QStringLiteral("ReplayGain: nao"), QStringLiteral("ReplayGain: faixa"),
                          QStringLiteral("ReplayGain: album")});
    replaygain->setCurrentIndex(saved.replaygain_mode);

    auto* buttons = new QHBoxLayout;
    for (QPushButton* button : {add_files, add_dir, remove_sel, clear_all, import_list, export_list})
        buttons->addWidget(button);

    auto* playlist_layout = new QVBoxLayout(&playlist_window);
    playlist_layout->addWidget(status);
    playlist_layout->addLayout(buttons);
    playlist_layout->addWidget(search);
    playlist_layout->addWidget(list, 1);
    playlist_layout->addWidget(replaygain);
    playlist_layout->addWidget(totals);

    auto* model = new PlaylistModel(controller->playlist());
    list->setModel(model);

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
        model->refresh();
        scan_missing();
    };
    playlist_window.on_drop = add_paths;  // LI-02

    const auto open_files = [&] {
        const QStringList paths =
            QFileDialog::getOpenFileNames(&main_panel, QStringLiteral("Abrir"));
        if (!paths.isEmpty()) {
            const bool was_empty = controller->playlist().empty();
            add_paths(paths);
            if (was_empty) controller->play_index(0);
        }
    };

    // ------------------------------------------------------------- ligacoes

    main_panel.on_open = open_files;
    main_panel.on_toggle_equalizer = [eq_panel] { eq_panel->setVisible(!eq_panel->isVisible()); };
    main_panel.on_toggle_playlist = [&playlist_window] {
        playlist_window.setVisible(!playlist_window.isVisible());
    };
    main_panel.equalizer_visible = [eq_panel] { return eq_panel->isVisible(); };
    main_panel.playlist_visible = [&playlist_window] { return playlist_window.isVisible(); };

    QObject::connect(add_files, &QPushButton::clicked, open_files);
    QObject::connect(add_dir, &QPushButton::clicked, [&] {
        const QString dir =
            QFileDialog::getExistingDirectory(&playlist_window, QStringLiteral("Adicionar pasta"));
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
            &playlist_window, QStringLiteral("Importar playlist"), {},
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
        model->refresh();
        scan_missing();
        if (!error.empty()) status->setText(QString::fromStdString(error));
    });
    QObject::connect(export_list, &QPushButton::clicked, [&] {
        const QString file = QFileDialog::getSaveFileName(
            &playlist_window, QStringLiteral("Exportar playlist"), QStringLiteral("playlist.m3u8"),
            QStringLiteral("Playlists (*.m3u8 *.m3u *.pls)"));
        if (file.isEmpty()) return;
        std::string error;
        if (!pang::core::playlist_io::save(file.toStdString(), controller->playlist().tracks(),
                                           error))
            status->setText(QString::fromStdString(error));
    });
    QObject::connect(list, &QListView::doubleClicked,
                     [&](const QModelIndex& index) { controller->play_index(index.row()); });
    QObject::connect(replaygain, &QComboBox::currentIndexChanged, [&](int index) {
        engine->set_replaygain_mode(static_cast<pang::core::ReplayGainMode>(index));
    });
    QObject::connect(search, &QLineEdit::textChanged, [&](const QString& text) {
        if (text.isEmpty()) return;
        const auto hits = controller->playlist().find(text.toStdString());  // LI-09
        if (hits.empty()) return;
        const QModelIndex index = model->index(hits.front());
        list->setCurrentIndex(index);
        list->scrollTo(index);
    });

    // ------------------------------------------------------------ temporizadores

    // VI-18 — a visualizacao roda a 60 Hz, independente da atualizacao de
    // textos e da playlist, que a 10 Hz ja e mais que suficiente.
    auto* frame_timer = new QTimer(&main_panel);
    QObject::connect(frame_timer, &QTimer::timeout, [&] { main_panel.tick(0.016f); });
    frame_timer->start(16);

    auto* slow_timer = new QTimer(&main_panel);
    QObject::connect(slow_timer, &QTimer::timeout, [&] {
        controller->poll();  // PL-24 — avanco no fim da faixa

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
        model->set_current(controller->current_index());

        QString line = QString::fromLatin1(pang::core::to_string(snap.state));
        if (snap.replaygain_present)
            line += QStringLiteral("  ·  RG %1 dB").arg(snap.replaygain_db, 0, 'f', 1);
        if (snap.clamp_hits > 0) line += QStringLiteral("  ·  CLIP");   // AU-17
        if (snap.underruns > 0) line += QStringLiteral("  ·  underruns: %1").arg(snap.underruns);
        if (snap.vis_drops > 0)
            line += QStringLiteral("  ·  vis descartados: %1").arg(snap.vis_drops);
        if (snap.state == State::Error) line = QString::fromStdString(engine->last_error());
        status->setText(line);

        int unknown = 0;
        const std::int64_t known = controller->playlist().known_duration_ms(&unknown);
        totals->setText(
            QStringLiteral("%1 itens  ·  %2%3")
                .arg(controller->playlist().size())
                .arg(format_ms(known))
                .arg(unknown > 0 ? QStringLiteral(" (+%1 de duracao desconhecida)").arg(unknown)
                                 : QString()));
    });
    slow_timer->start(100);

    // ------------------------------------------------------ posicao das janelas

    const auto screens = [] {
        QVector<QRect> rects;
        for (const QScreen* screen : QGuiApplication::screens())
            rects.append(screen->availableGeometry());
        return rects;
    };
    const QRect primary = QGuiApplication::primaryScreen()->availableGeometry();

    // AP-14 — geometria salva passa pela regra de recuperacao antes de ser
    // aplicada. Trocar de monitor entre sessoes nao pode esconder a janela.
    const auto restore = [&](QWidget& widget, const std::array<int, 4>& geometry) {
        if (geometry[2] <= 0 || geometry[3] <= 0) return;
        const QRect wanted(geometry[0], geometry[1], geometry[2], geometry[3]);
        const QRect safe = pang::ui::shell::recover(wanted, screens(), primary);
        if (safe != wanted)
            pang::core::log::warn("janela fora da area visivel; reposicionada");
        widget.move(safe.topLeft());
        if (&widget != static_cast<QWidget*>(&main_panel)) widget.resize(safe.size());
    };

    restore(main_panel, saved.main_geometry);
    restore(playlist_window, saved.playlist_geometry);
    restore(*eq_panel, saved.equalizer_geometry);

    playlist_window.setVisible(saved.playlist_visible);
    eq_panel->setVisible(saved.equalizer_visible);

    // IN-09 — grava ao sair, atomicamente.
    QObject::connect(&app, &QApplication::aboutToQuit, [&] {
        const auto capture_geometry = [](const QWidget& widget) {
            const QRect g = widget.frameGeometry();
            return std::array<int, 4>{g.x(), g.y(), widget.width(), widget.height()};
        };

        saved.volume = engine->volume();
        saved.balance = engine->balance();
        saved.shuffle = controller->shuffle();
        saved.repeat = static_cast<int>(controller->repeat());
        saved.replaygain_mode = static_cast<int>(engine->replaygain_mode());
        saved.eq = pang::core::dsp::capture(engine->equalizer());
        saved.scale = main_panel.scale();
        saved.visualization =
            main_panel.visualization() == pang::ui::MainPanel::Visualization::Scope   ? 1
            : main_panel.visualization() == pang::ui::MainPanel::Visualization::Off   ? 2
                                                                                      : 0;
        saved.playlist_visible = playlist_window.isVisible();
        saved.equalizer_visible = eq_panel->isVisible();
        saved.main_geometry = capture_geometry(main_panel);
        saved.playlist_geometry = capture_geometry(playlist_window);
        saved.equalizer_geometry = capture_geometry(*eq_panel);
        pang::ui::settings::save(saved);

        playlist_window.close();
        eq_panel->close();
    });

    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        add_paths(args);
        controller->play_index(0);
    }

    main_panel.show();
    return app.exec();
}
