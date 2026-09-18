#include "ui/panel/main_panel.h"

#include "ui/panel/main_layout.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWindow>

#include <algorithm>
#include <string_view>
#include <utility>

namespace pang::ui {
namespace {

using core::State;

using namespace pang::ui::layout;

constexpr int kThumbWidth = 11;
constexpr int kPositionThumbWidth = 29;
constexpr float kScrollInterval = 0.18f;  // segundos por pixel de rolagem

QString format_time(std::int64_t frames, int rate) {
    if (frames < 0 || rate <= 0) return QStringLiteral("--:--");
    const std::int64_t total = frames / rate;
    return QStringLiteral("%1:%2")
        .arg(std::min<std::int64_t>(total / 60, 99), 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

}  // namespace

MainPanel::MainPanel(core::Controller& controller, core::Engine& engine, skin::Atlas& atlas,
                     QWidget* parent)
    : QWidget(parent), controller_(controller), engine_(engine), atlas_(atlas) {
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setMouseTracking(true);
    analyzer_.configure(engine.sample_rate(), engine.channels());
    capture_.resize(8192);
    set_scale(atlas.scale());
    set_visualization(Visualization::Spectrum);
}

void MainPanel::set_scale(int scale) {
    atlas_.set_scale(scale);
    // AP-13 — as areas clicaveis escalam junto: o alvo de toque CRESCE com a
    // escala, em vez de encolher como aconteceria com escala fracionaria.
    setFixedSize(kWidth * atlas_.scale(),
                 (compact_ ? kCompactHeight : kHeight) * atlas_.scale());
    update();
}

void MainPanel::set_compact(bool compact) {
    compact_ = compact;
    set_scale(atlas_.scale());
}

void MainPanel::set_visualization(Visualization mode) {
    visualization_ = mode;
    engine_.set_capture_enabled(mode != Visualization::Off);  // VI-17
    if (mode == Visualization::Off) analyzer_.reset();
    update();
}

QPoint MainPanel::to_logical(const QPoint& physical) const {
    const int s = std::max(1, atlas_.scale());
    return QPoint(physical.x() / s, physical.y() / s);
}

void MainPanel::tick(float dt_seconds) {
    snapshot_ = engine_.snapshot();

    if (visualization_ != Visualization::Off) {
        if (snapshot_.sample_rate > 0)
            analyzer_.set_source_nyquist(static_cast<float>(snapshot_.sample_rate) / 2.0f);

        if (snapshot_.state == State::Stopped || snapshot_.state == State::Error)
            analyzer_.reset();
        else if (snapshot_.state == State::Paused)
            analyzer_.hold();
        else
            for (;;) {
                const std::size_t got = engine_.read_visualization(capture_.data(), capture_.size());
                if (got == 0) break;
                analyzer_.feed(capture_.data(), got / engine_.channels());
                if (got < capture_.size()) break;
            }
        analyzer_.advance_peaks(dt_seconds);
    }

    // PL-14 — rolagem do titulo, um pixel por vez.
    title_timer_ += dt_seconds;
    if (title_timer_ >= kScrollInterval) {
        title_timer_ = 0.0f;
        ++title_offset_;
    }
    update();
}

// ---------------------------------------------------------------- desenho

void MainPanel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    if (compact_) {
        // AP-11 — a barra mostra o essencial: tempo e titulo. Sem controles,
        // porque nao ha onde clicar em 14 px sem alvo pequeno demais.
        atlas_.draw_tiled(painter, QStringLiteral("frame/titlebar"), kTitlebar);
        const QString time = format_time(snapshot_.position_frames, engine_.sample_rate());
        atlas_.draw_text(painter, time, 6, 4);

        const int index = controller_.current_index();
        const QString title =
            index >= 0 ? QString::fromStdString(controller_.playlist().at(index).display_title())
                       : QStringLiteral("PLAYAMPNG");
        painter.save();
        const int s = atlas_.scale();
        painter.setClipRect(46 * s, 0, (kWidth - 52) * s, kCompactHeight * s);
        atlas_.draw_text(painter, title, 46, 4);
        painter.restore();
        return;
    }

    paint_frame(painter);
    paint_display(painter);
    paint_visualization(painter);
    paint_sliders(painter);
    paint_buttons(painter);
}

void MainPanel::paint_frame(QPainter& painter) {
    const int s = atlas_.scale();
    painter.fillRect(rect(), atlas_.color(QStringLiteral("background")));

    atlas_.draw_tiled(painter, QStringLiteral("frame/titlebar"), kTitlebar);

    // Bloco liso atras do titulo: sobre as listras o texto fica ilegivel, e e
    // assim que o classico resolve.
    const QString caption = QStringLiteral("PLAYAMPNG");
    const int caption_w = atlas_.text_width(caption);
    const int caption_x = (kWidth - caption_w) / 2;
    const int caption_y = (kTitlebar.height() - atlas_.glyph_height()) / 2;
    painter.fillRect((caption_x - 5) * s, 2 * s, (caption_w + 10) * s,
                     (kTitlebar.height() - 4) * s, atlas_.color(QStringLiteral("background")));
    atlas_.draw_text(painter, caption, caption_x, caption_y);

    for (const auto& button : {std::pair{"minimize", kMinimize}, std::pair{"shade", kShade},
                               std::pair{"close", kClose}}) {
        const Hit hit = button.first == std::string_view("minimize") ? Hit::Minimize
                        : button.first == std::string_view("shade")  ? Hit::Shade
                                                                     : Hit::Close;
        atlas_.draw(painter,
                    QStringLiteral("title/%1/%2")
                        .arg(QLatin1String(button.first),
                             QLatin1String(pressed_ == hit ? "pressed" : "normal")),
                    button.second.x(), button.second.y());
    }

    // Poco escuro: fundo preto com aresta escura em cima e clara embaixo, que e
    // o que da a impressao de rebaixo.
    const auto well = [&](const QRect& area) {
        painter.fillRect(area.x() * s, area.y() * s, area.width() * s, area.height() * s,
                         atlas_.color(QStringLiteral("well")));
        painter.fillRect(area.x() * s, area.y() * s, area.width() * s, s,
                         atlas_.color(QStringLiteral("bevel_dark")));
        painter.fillRect(area.x() * s, area.y() * s, s, area.height() * s,
                         atlas_.color(QStringLiteral("bevel_dark")));
        painter.fillRect(area.x() * s, (area.bottom()) * s, area.width() * s, s,
                         atlas_.color(QStringLiteral("bevel_light")));
        painter.fillRect((area.right()) * s, area.y() * s, s, area.height() * s,
                         atlas_.color(QStringLiteral("bevel_light")));
    };

    for (const QRect& area : {kTimeWell, kTitleWell, kVisFrame, kPosition, kVolume, kBalance})
        well(area);
    // Interior do poco do espectro, ja dentro da moldura.
    painter.fillRect(kVis.x() * s, kVis.y() * s, kVis.width() * s, kVis.height() * s,
                     atlas_.color(QStringLiteral("well")));

    // Linha guia do trilho de posicao: DUAS linhas da mesma cor, ocupando as
    // duas fileiras centrais do interior.
    //
    // Antes eram uma escura e uma clara. A escura sumia no fundo preto e so a
    // clara aparecia, uma fileira abaixo do centro — dai a guia parecer
    // desalinhada. O interior tem altura par, entao uma linha de 1 px nao tem
    // como ficar centrada; duas tem.
    for (int row : {4, 5})
        painter.fillRect((kPosition.x() + 1) * s, (kPosition.y() + row) * s,
                         (kPosition.width() - 2) * s, s, QColor(48, 48, 48));

    // O trilho do volume carrega o degrade do espectro na horizontal: o proprio
    // trilho diz o nivel, sem precisar de numero. Vem do classico.
    const QVector<QColor>& gradient = atlas_.spectrum();
    if (!gradient.isEmpty()) {
        const int inner_x = kVolume.x() + 1;
        const int inner_w = kVolume.width() - 2;
        for (int i = 0; i < inner_w; ++i) {
            const int index = i * (static_cast<int>(gradient.size()) - 1) / std::max(1, inner_w - 1);
            painter.fillRect((inner_x + i) * s, (kVolume.y() + 4) * s, s, 5 * s,
                             gradient[static_cast<std::size_t>(index)].darker(160));
        }
        const int filled = static_cast<int>(engine_.volume() * inner_w);
        for (int i = 0; i < filled; ++i) {
            const int index = i * (static_cast<int>(gradient.size()) - 1) / std::max(1, inner_w - 1);
            painter.fillRect((inner_x + i) * s, (kVolume.y() + 4) * s, s, 5 * s,
                             gradient[static_cast<std::size_t>(index)]);
        }
    }

    // O trilho do balanco espelha o degrade a partir do centro: o desvio para
    // um lado aparece como cor, e o centro fica verde.
    if (!gradient.isEmpty()) {
        const int inner_x = kBalance.x() + 1;
        const int inner_w = kBalance.width() - 2;
        for (int i = 0; i < inner_w; ++i) {
            const float offset =
                std::fabs(static_cast<float>(i) / std::max(1, inner_w - 1) * 2.0f - 1.0f);
            const int index = static_cast<int>(offset * (gradient.size() - 1));
            painter.fillRect((inner_x + i) * s, (kBalance.y() + 4) * s, s, 5 * s,
                             gradient[static_cast<std::size_t>(std::clamp<int>(
                                 index, 0, static_cast<int>(gradient.size()) - 1))]);
        }
    }

    // Borda externa do painel, de um pixel: sem ela a janela sem moldura do
    // sistema fica sem limite visivel contra um fundo escuro.
    painter.fillRect(0, 0, width(), s, atlas_.color(QStringLiteral("bevel_light")));
    painter.fillRect(0, 0, s, height(), atlas_.color(QStringLiteral("bevel_light")));
    painter.fillRect(0, height() - s, width(), s, atlas_.color(QStringLiteral("bevel_dark")));
    painter.fillRect(width() - s, 0, s, height(), atlas_.color(QStringLiteral("bevel_dark")));
}

void MainPanel::paint_display(QPainter& painter) {
    // PL-15 — decorrido ou restante, alternado clicando no mostrador.
    std::int64_t frames = snapshot_.position_frames;
    QString prefix;
    if (show_remaining_ && snapshot_.duration_frames > 0) {
        frames = snapshot_.duration_frames - snapshot_.position_frames;
        prefix = QStringLiteral("-");
    }
    // Centrado no poco pela largura MEDIDA do texto. Com posicao fixa, "00:00"
    // e "-99:99" ficam desalinhados um em relacao ao outro.
    const QString time = prefix + format_time(frames, engine_.sample_rate());
    const int time_x = kTimeWell.x() + (kTimeWell.width() - atlas_.time_width(time)) / 2;
    atlas_.draw_time(painter, time, time_x, kTime.y());

    // PL-17, PL-18, PL-19 — ausencia nao vira numero plausivel.
    const QColor dim = atlas_.color(QStringLiteral("green_dim"));
    const auto right_aligned = [&](const QString& text, int right, const QColor& tint) {
        atlas_.draw_text(painter, text, right - atlas_.text_width(text), kInfoY, tint);
    };

    const bool has_bitrate = snapshot_.bitrate_bps > 0;
    right_aligned(has_bitrate ? QStringLiteral("%1").arg(snapshot_.bitrate_bps / 1000)
                              : QStringLiteral("---"),
                  kBitrateRight, has_bitrate ? QColor() : dim);
    atlas_.draw_text(painter, QStringLiteral("KBPS"), kBitrateLabel, kInfoY, dim);

    const bool has_rate = snapshot_.sample_rate > 0;
    right_aligned(has_rate ? QStringLiteral("%1").arg(snapshot_.sample_rate / 1000)
                           : QStringLiteral("--"),
                  kSampleRateRight, has_rate ? QColor() : dim);
    atlas_.draw_text(painter, QStringLiteral("KHZ"), kSampleRateLabel, kInfoY, dim);

    // Mono e estereo como dois rotulos fixos, com o inativo esmaecido: le-se o
    // estado sem precisar lembrar qual palavra apareceria ali.
    atlas_.draw_text(painter, QStringLiteral("MONO"), kMono, kInfoY,
                     snapshot_.channels == 1 ? QColor() : dim);
    atlas_.draw_text(painter, QStringLiteral("STEREO"), kStereo, kInfoY,
                     snapshot_.channels > 1 ? QColor() : dim);

    // PL-20 e PL-14 — posicao na playlist e titulo com rolagem.
    const int index = controller_.current_index();
    QString title;
    if (index >= 0)
        title = QStringLiteral("%1. %2")
                    .arg(index + 1)
                    .arg(QString::fromStdString(controller_.playlist().at(index).display_title()));
    else if (snapshot_.state == State::Error)
        title = QStringLiteral("ERRO");
    else
        title = QStringLiteral("PLAYAMPNG");

    const int width = atlas_.text_width(title);
    if (width <= kTitle.width()) {
        title_offset_ = 0;
        atlas_.draw_text(painter, title, kTitle.x(), kTitle.y());
    } else {
        // Rolagem continua: o texto e emendado consigo mesmo com um separador,
        // entao nao ha salto ao dar a volta.
        const QString marquee = title + QStringLiteral("   ***   ");
        const int span = atlas_.text_width(marquee);
        if (title_offset_ >= span) title_offset_ = 0;

        painter.save();
        const int s = atlas_.scale();
        painter.setClipRect(kTitle.x() * s, kTitle.y() * s, kTitle.width() * s,
                            kTitle.height() * s);
        atlas_.draw_text(painter, marquee + marquee, kTitle.x() - title_offset_, kTitle.y());
        painter.restore();
    }
}

void MainPanel::paint_visualization(QPainter& painter) {
    if (visualization_ == Visualization::Off) return;
    const int s = atlas_.scale();
    const QRect well(kVis.x() * s, kVis.y() * s, kVis.width() * s, kVis.height() * s);

    if (visualization_ == Visualization::Scope) {
        const std::vector<float>& scope = analyzer_.scope();
        if (scope.empty()) return;
        painter.setPen(atlas_.color(QStringLiteral("green")));
        const qreal middle = well.center().y() + 0.5;
        const qreal amplitude = well.height() / 2.0;
        QPointF previous(well.left(), middle);
        for (int x = 0; x < well.width(); ++x) {
            const std::size_t i =
                static_cast<std::size_t>(qreal(x) / well.width() * (scope.size() - 1));
            const QPointF point(well.left() + x, middle - scope[i] * amplitude);
            painter.drawLine(previous, point);
            previous = point;
        }
        return;
    }

    // VI-10 — degrade VERTICAL: verde na base, amarelo no meio, vermelho no
    // pico. A cor diz a altura da barra, nao a posicao dela no espectro.
    const int bars = core::dsp::SpectrumAnalyzer::kBars;
    const QVector<QColor>& gradient = atlas_.spectrum();
    const int rows = kVis.height();

    // 19 barras de 3 px com 1 px de intervalo ocupam exatamente os 76 px do
    // poco. Largura e espacamento constantes, sem sobra a distribuir.
    constexpr int kBarWidth = 3;
    constexpr int kBarPitch = 4;

    for (int bar = 0; bar < bars; ++bar) {
        const int x = (kVis.x() + bar * kBarPitch) * s;
        const float value = analyzer_.bars()[static_cast<std::size_t>(bar)];
        const int filled = static_cast<int>(value * rows);

        for (int row = 0; row < filled; ++row) {
            // row 0 e a base da barra.
            const int index = gradient.size() > 1
                                  ? row * (static_cast<int>(gradient.size()) - 1) / (rows - 1)
                                  : 0;
            painter.fillRect(x, (kVis.y() + rows - 1 - row) * s, kBarWidth * s, s,
                             gradient[std::clamp<int>(index, 0, static_cast<int>(gradient.size()) - 1)]);
        }

        const int peak = static_cast<int>(analyzer_.peaks()[static_cast<std::size_t>(bar)] * rows);
        if (peak > 0)
            painter.fillRect(x, (kVis.y() + rows - peak) * s, kBarWidth * s, s,
                             atlas_.peak_color());
    }
}

void MainPanel::paint_sliders(QPainter& painter) {
    const auto thumb = [&](const QRect& groove, float fraction, Hit hit) {
        const int travel = groove.width() - kThumbWidth;
        const int x = groove.x() + static_cast<int>(fraction * travel);
        atlas_.draw(painter, QStringLiteral("slider/thumb/%1").arg(QString::fromLatin1(
                                 dragging_ == hit ? "pressed" : "normal")),
                    x, groove.y());
    };

    thumb(kVolume, engine_.volume(), Hit::Volume);
    thumb(kBalance, (engine_.balance() + 1.0f) / 2.0f, Hit::Balance);

    // PL-21/PL-23 — a barra acompanha a posicao real, e so aparece quando a
    // fonte permite busca e informa duracao. O cursor e largo, como no
    // classico: um cursor de 11 px numa barra de 248 px some.
    if (snapshot_.seekable && snapshot_.duration_frames > 0) {
        const float fraction =
            std::clamp(static_cast<float>(snapshot_.position_frames) / snapshot_.duration_frames,
                       0.0f, 1.0f);
        const int travel = kPosition.width() - kPositionThumbWidth;
        atlas_.draw(painter,
                    QStringLiteral("slider/position/%1")
                        .arg(QLatin1String(dragging_ == Hit::Position ? "pressed" : "normal")),
                    kPosition.x() + static_cast<int>(fraction * travel), kPosition.y());
    }
}

const char* MainPanel::state_for(Hit hit) const {
    if (pressed_ == hit) return "pressed";
    if (focus_ == hit) return "focus";
    return "normal";
}

void MainPanel::paint_buttons(QPainter& painter) {
    const struct {
        const char* name;
        QRect area;
        Hit hit;
    } transport[] = {
        {"previous", kPrevious, Hit::Previous}, {"play", kPlay, Hit::Play},
        {"pause", kPause, Hit::Pause},          {"stop", kStop, Hit::Stop},
        {"next", kNext, Hit::Next},             {"eject", kEject, Hit::Eject},
    };
    for (const auto& button : transport)
        atlas_.draw(painter,
                    QStringLiteral("button/%1/%2")
                        .arg(QLatin1String(button.name), QLatin1String(state_for(button.hit))),
                    button.area.x(), button.area.y());

    // O sprite e so a pastilha; o rotulo e desenhado por cima com a fonte de
    // verdade. Antes havia um sprite por rotulo, com o texto assado dentro.
    const auto toggle = [&](const char* pill, const QString& label, const QRect& area, Hit hit,
                            bool active) {
        const char* state = pressed_ == hit  ? "pressed"
                            : active         ? "active"
                            : focus_ == hit  ? "focus"
                                             : "normal";
        atlas_.draw(painter,
                    QStringLiteral("pill/%1/%2").arg(QLatin1String(pill), QLatin1String(state)),
                    area.x(), area.y());
        atlas_.draw_text(painter, label,
                         area.x() + (area.width() - atlas_.text_width(label)) / 2,
                         area.y() + (area.height() - atlas_.glyph_height()) / 2,
                         active ? atlas_.color(QStringLiteral("green"))
                                : atlas_.color(QStringLiteral("green_text")));
    };

    toggle("eq", QStringLiteral("EQ"), kEqualizer, Hit::Equalizer,
           equalizer_visible && equalizer_visible());
    toggle("eq", QStringLiteral("PL"), kPlaylist, Hit::Playlist,
           playlist_visible && playlist_visible());
    toggle("shuffle", QStringLiteral("SHUFFLE"), kShuffle, Hit::Shuffle, controller_.shuffle());
    toggle("repeat", QStringLiteral("REP"), kRepeat, Hit::Repeat,
           controller_.repeat() != core::Repeat::Off);
}

// ------------------------------------------------------------------ entrada

MainPanel::Hit MainPanel::hit_test(const QPoint& p) const {
    const struct {
        QRect area;
        Hit hit;
    } regions[] = {
        {kPrevious, Hit::Previous}, {kPlay, Hit::Play},         {kPause, Hit::Pause},
        {kStop, Hit::Stop},         {kNext, Hit::Next},         {kEject, Hit::Eject},
        {kShuffle, Hit::Shuffle},   {kRepeat, Hit::Repeat},     {kEqualizer, Hit::Equalizer},
        {kPlaylist, Hit::Playlist}, {kVolume, Hit::Volume},     {kBalance, Hit::Balance},
        {kPosition, Hit::Position}, {kTime, Hit::Time},         {kVis, Hit::Vis},
        {kMinimize, Hit::Minimize}, {kShade, Hit::Shade},       {kClose, Hit::Close},
        {kTitlebar, Hit::Titlebar},
    };
    for (const auto& region : regions)
        if (region.area.contains(p)) return region.hit;
    return Hit::None;
}

void MainPanel::apply_slider(Hit hit, int logical_x) {
    const QRect groove = hit == Hit::Volume     ? kVolume
                         : hit == Hit::Balance  ? kBalance
                                                : kPosition;
    const int travel = groove.width() - kThumbWidth;
    const float fraction =
        std::clamp(static_cast<float>(logical_x - groove.x() - kThumbWidth / 2) / travel, 0.0f,
                   1.0f);

    if (hit == Hit::Volume) engine_.set_volume(fraction);
    else if (hit == Hit::Balance) engine_.set_balance(fraction * 2.0f - 1.0f);
    update();
}

void MainPanel::mousePressEvent(QMouseEvent* event) {
    const QPoint p = to_logical(event->pos());
    const Hit hit = hit_test(p);
    focus_ = hit;

    if (hit == Hit::Volume || hit == Hit::Balance) {
        dragging_ = hit;
        apply_slider(hit, p.x());
        return;
    }
    if (hit == Hit::Position && snapshot_.seekable && snapshot_.duration_frames > 0) {
        dragging_ = hit;
        update();
        return;
    }
    if (hit == Hit::Titlebar && event->type() == QEvent::MouseButtonDblClick) {
        if (on_toggle_compact) on_toggle_compact();  // AP-11, gesto classico
        return;
    }
    if (hit == Hit::Titlebar) {
        // Wayland nao permite ao cliente mover a propria janela; o compositor
        // cuida disso. Onde ha suporte, systemMove faz a coisa certa.
        if (window()->windowHandle()) window()->windowHandle()->startSystemMove();
        return;
    }
    pressed_ = hit;
    update();
}

void MainPanel::mouseDoubleClickEvent(QMouseEvent* event) { mousePressEvent(event); }

void MainPanel::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_ == Hit::None) return;
    const QPoint p = to_logical(event->pos());
    if (dragging_ == Hit::Position) {
        update();  // o arraste so confirma no release (nao dispara seek em serie)
        drag_origin_ = p;
        return;
    }
    apply_slider(dragging_, p.x());
}

void MainPanel::mouseReleaseEvent(QMouseEvent* event) {
    const QPoint p = to_logical(event->pos());

    if (dragging_ == Hit::Position) {
        const int travel = kPosition.width() - kPositionThumbWidth;
        const float fraction = std::clamp(
            static_cast<float>(p.x() - kPosition.x() - kPositionThumbWidth / 2) / travel, 0.0f,
            1.0f);
        controller_.seek(static_cast<double>(fraction) * snapshot_.duration_frames /
                         engine_.sample_rate());
        dragging_ = Hit::None;
        update();
        return;
    }
    if (dragging_ != Hit::None) {
        dragging_ = Hit::None;
        return;
    }

    const Hit hit = hit_test(p);
    if (hit != pressed_) {
        pressed_ = Hit::None;
        update();
        return;
    }

    switch (hit) {
        case Hit::Previous:  controller_.previous(); break;
        case Hit::Play:      controller_.play(); break;
        case Hit::Pause:     controller_.pause(); break;
        case Hit::Stop:      controller_.stop(); break;
        case Hit::Next:      controller_.next(); break;
        case Hit::Eject:     if (on_open) on_open(); break;
        case Hit::Shuffle:   controller_.set_shuffle(!controller_.shuffle()); break;
        case Hit::Repeat: {
            const core::Repeat current = controller_.repeat();
            controller_.set_repeat(current == core::Repeat::Off   ? core::Repeat::All
                                   : current == core::Repeat::All ? core::Repeat::Track
                                                                  : core::Repeat::Off);
            break;
        }
        case Hit::Equalizer: if (on_toggle_equalizer) on_toggle_equalizer(); break;
        case Hit::Playlist:  if (on_toggle_playlist) on_toggle_playlist(); break;
        case Hit::Time:      show_remaining_ = !show_remaining_; break;  // PL-15
        case Hit::Vis:
            set_visualization(visualization_ == Visualization::Spectrum ? Visualization::Scope
                              : visualization_ == Visualization::Scope  ? Visualization::Off
                                                                        : Visualization::Spectrum);
            break;
        case Hit::Minimize: window()->showMinimized(); break;
        case Hit::Shade:    if (on_toggle_compact) on_toggle_compact(); break;
        case Hit::Close:    window()->close(); break;
        default: break;
    }
    pressed_ = Hit::None;
    update();
}

// IN-02 — todos os controles operaveis por teclado.
void MainPanel::keyPressEvent(QKeyEvent* event) {
    // Teclas com Ctrl pertencem ao shell (escala, compacto, destacado): deixar
    // passar e o que faz o atalho funcionar com qualquer painel focado.
    if (event->modifiers() & Qt::ControlModifier) {
        event->ignore();
        QWidget::keyPressEvent(event);
        return;
    }

    switch (event->key()) {
        case Qt::Key_X: case Qt::Key_Space: controller_.play(); break;
        case Qt::Key_C: controller_.pause(); break;
        case Qt::Key_V: controller_.stop(); break;
        case Qt::Key_Z: controller_.previous(); break;
        case Qt::Key_B: controller_.next(); break;
        case Qt::Key_S: controller_.set_shuffle(!controller_.shuffle()); break;
        case Qt::Key_L: if (on_open) on_open(); break;
        case Qt::Key_Up:
            engine_.set_volume(std::min(1.0f, engine_.volume() + 0.05f));
            break;
        case Qt::Key_Down:
            engine_.set_volume(std::max(0.0f, engine_.volume() - 0.05f));
            break;
        case Qt::Key_Left:
            if (snapshot_.seekable)
                controller_.seek(std::max(0.0, double(snapshot_.position_frames) /
                                                       engine_.sample_rate() - 5.0));
            break;
        case Qt::Key_Right:
            if (snapshot_.seekable)
                controller_.seek(double(snapshot_.position_frames) / engine_.sample_rate() + 5.0);
            break;
        default:
            QWidget::keyPressEvent(event);
            return;
    }
    update();
}

}  // namespace pang::ui
