#include "ui/panel/main_panel.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWindow>

#include <algorithm>

namespace pang::ui {
namespace {

using core::State;

// Geometria em coordenadas logicas (escala 1x). Aproximacao documentada do
// layout classico: as proporcoes e a densidade sao as mesmas, as coordenadas
// exatas sao nossas — ver docs/APROXIMACOES.md.
constexpr QRect kTitlebar{0, 0, 275, 14};
constexpr QRect kTime{12, 24, 45, 13};
constexpr QRect kVis{66, 24, 76, 16};
constexpr QRect kInfo{150, 27, 113, 7};
constexpr QRect kTitle{12, 44, 251, 7};
constexpr QRect kVolume{12, 57, 90, 11};
constexpr QRect kBalance{110, 57, 50, 11};
constexpr QRect kEqualizer{170, 57, 28, 13};
constexpr QRect kPlaylist{200, 57, 28, 13};
constexpr QRect kPosition{12, 74, 251, 11};
constexpr QRect kPrevious{12, 90, 23, 18};
constexpr QRect kPlay{36, 90, 23, 18};
constexpr QRect kPause{60, 90, 23, 18};
constexpr QRect kStop{84, 90, 23, 18};
constexpr QRect kNext{108, 90, 23, 18};
constexpr QRect kEject{134, 90, 23, 18};
constexpr QRect kShuffle{170, 92, 28, 13};
constexpr QRect kRepeat{202, 92, 28, 13};

constexpr int kThumbWidth = 11;
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
    const QString caption = QStringLiteral("PLAYAMPNG");
    atlas_.draw_text(painter, caption, (kWidth - atlas_.text_width(caption)) / 2, 4);

    // Pocos escuros dos mostradores: aresta escura em cima, clara embaixo.
    for (const QRect& well : {kTime, kVis, kPosition, kVolume, kBalance}) {
        painter.fillRect(QRect(well.x() * s, well.y() * s, well.width() * s, well.height() * s),
                         atlas_.color(QStringLiteral("well")));
        painter.setPen(atlas_.color(QStringLiteral("bevel_dark")));
        painter.drawLine(well.left() * s, well.top() * s, well.right() * s, well.top() * s);
        painter.setPen(atlas_.color(QStringLiteral("bevel_light")));
        painter.drawLine(well.left() * s, well.bottom() * s, well.right() * s, well.bottom() * s);
    }
}

void MainPanel::paint_display(QPainter& painter) {
    // PL-15 — decorrido ou restante, alternado clicando no mostrador.
    std::int64_t frames = snapshot_.position_frames;
    QString prefix;
    if (show_remaining_ && snapshot_.duration_frames > 0) {
        frames = snapshot_.duration_frames - snapshot_.position_frames;
        prefix = QStringLiteral("-");
    }
    const QString time = format_time(frames, engine_.sample_rate());
    atlas_.draw_time(painter, prefix.isEmpty() ? time : prefix + time.mid(1), kTime.x(),
                     kTime.y());

    // PL-17, PL-18, PL-19 — ausencia nao vira numero plausivel.
    QString info;
    info += snapshot_.bitrate_bps > 0 ? QStringLiteral("%1K").arg(snapshot_.bitrate_bps / 1000)
                                      : QStringLiteral("---K");
    info += QStringLiteral(" ");
    info += snapshot_.sample_rate > 0 ? QStringLiteral("%1H").arg(snapshot_.sample_rate / 1000)
                                      : QStringLiteral("--H");
    info += QStringLiteral(" ");
    info += snapshot_.channels == 1 ? QStringLiteral("MONO")
            : snapshot_.channels > 1 ? QStringLiteral("STEREO")
                                     : QStringLiteral("------");
    atlas_.draw_text(painter, info, kInfo.x(), kInfo.y());

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

    const int bars = core::dsp::SpectrumAnalyzer::kBars;
    const int bar_width = std::max(1, kVis.width() / bars);
    for (int bar = 0; bar < bars; ++bar) {
        const float value = analyzer_.bars()[static_cast<std::size_t>(bar)];
        const int height = static_cast<int>(value * kVis.height());
        const int x = (kVis.x() + bar * bar_width) * s;

        // VI-10 — verde no grave passando a amarelo no agudo, na paleta classica.
        const int hue = 120 - 60 * bar / bars;
        painter.fillRect(x, (kVis.y() + kVis.height() - height) * s, (bar_width - 1) * s,
                         height * s, QColor::fromHsv(hue, 255, 230));

        const int peak = static_cast<int>(analyzer_.peaks()[static_cast<std::size_t>(bar)] *
                                          kVis.height());
        if (peak > 0)
            painter.fillRect(x, (kVis.y() + kVis.height() - peak) * s, (bar_width - 1) * s, s,
                             QColor(220, 220, 220));
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
    // fonte permite busca e informa duracao.
    if (snapshot_.seekable && snapshot_.duration_frames > 0) {
        const float fraction =
            std::clamp(static_cast<float>(snapshot_.position_frames) / snapshot_.duration_frames,
                       0.0f, 1.0f);
        thumb(kPosition, fraction, Hit::Position);
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

    const auto toggle = [&](const char* name, const QRect& area, Hit hit, bool active) {
        const char* state = pressed_ == hit  ? "pressed"
                            : active         ? "active"
                            : focus_ == hit  ? "focus"
                                             : "normal";
        atlas_.draw(painter,
                    QStringLiteral("toggle/%1/%2").arg(QLatin1String(name), QLatin1String(state)),
                    area.x(), area.y());
    };

    toggle("eq", kEqualizer, Hit::Equalizer, equalizer_visible && equalizer_visible());
    toggle("playlist", kPlaylist, Hit::Playlist, playlist_visible && playlist_visible());
    toggle("shuffle", kShuffle, Hit::Shuffle, controller_.shuffle());
    toggle("repeat", kRepeat, Hit::Repeat, controller_.repeat() != core::Repeat::Off);
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
        const int travel = kPosition.width() - kThumbWidth;
        const float fraction = std::clamp(
            static_cast<float>(p.x() - kPosition.x() - kThumbWidth / 2) / travel, 0.0f, 1.0f);
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
