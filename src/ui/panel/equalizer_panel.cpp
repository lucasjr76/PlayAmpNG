#include "ui/panel/equalizer_panel.h"

#include <QMenu>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

namespace pang::ui {
namespace {

using core::dsp::Equalizer;

constexpr QRect kTitlebar{0, 0, 275, 14};
constexpr QRect kBypass{8, 17, 28, 13};
constexpr QRect kReset{40, 17, 28, 13};
constexpr QRect kPreset{72, 17, 28, 13};
constexpr QRect kPresetName{106, 19, 160, 7};

constexpr int kSliderTop = 36;
constexpr int kSliderHeight = 58;
constexpr int kSliderWidth = 20;
constexpr int kPreampX = 8;
constexpr int kBandX = 36;
constexpr int kBandSpacing = 23;
constexpr int kThumbHeight = 11;
constexpr int kLabelY = 98;

QString band_label(int band) {
    const float hz = Equalizer::frequencies()[static_cast<std::size_t>(band)];
    return hz >= 1000.0f ? QStringLiteral("%1K").arg(static_cast<int>(hz / 1000.0f))
                         : QStringLiteral("%1").arg(static_cast<int>(hz));
}

}  // namespace

EqualizerPanel::EqualizerPanel(core::dsp::Equalizer& equalizer,
                               std::vector<core::dsp::EqPreset>& user_presets, skin::Atlas& atlas,
                               QWidget* parent)
    : QWidget(parent), equalizer_(equalizer), user_presets_(user_presets), atlas_(atlas) {
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    preset_name_ = QStringLiteral("PERSONALIZADO");
    set_scale(atlas.scale());
}

void EqualizerPanel::set_scale(int) {
    setFixedSize(kWidth * atlas_.scale(), kHeight * atlas_.scale());
    update();
}

void EqualizerPanel::refresh() { update(); }

QPoint EqualizerPanel::to_logical(const QPoint& physical) const {
    const int s = std::max(1, atlas_.scale());
    return QPoint(physical.x() / s, physical.y() / s);
}

QRect EqualizerPanel::slider_rect(int index) const {
    const int x = index < 0 ? kPreampX : kBandX + index * kBandSpacing;
    return QRect(x, kSliderTop, kSliderWidth, kSliderHeight);
}

int EqualizerPanel::slider_at(const QPoint& p) const {
    if (slider_rect(-1).contains(p)) return -1;
    for (int band = 0; band < Equalizer::kBands; ++band)
        if (slider_rect(band).contains(p)) return band;
    return -2;
}

void EqualizerPanel::apply_slider(int index, int logical_y) {
    const QRect area = slider_rect(index);
    const int travel = area.height() - kThumbHeight;
    const float fraction =
        std::clamp(static_cast<float>(logical_y - area.y() - kThumbHeight / 2) / travel, 0.0f,
                   1.0f);
    // Topo e ganho maximo: o slider sobe para reforcar, como qualquer
    // equalizador grafico.
    const float db = (0.5f - fraction) * 2.0f * Equalizer::kRangeDb;

    if (index < 0)
        equalizer_.set_preamp_db(db);
    else
        equalizer_.set_band_db(index, db);

    preset_name_ = QStringLiteral("PERSONALIZADO");
    update();
}

void EqualizerPanel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    const int s = atlas_.scale();

    painter.fillRect(rect(), atlas_.color(QStringLiteral("background")));
    atlas_.draw_tiled(painter, QStringLiteral("frame/titlebar"), kTitlebar);
    const QString caption = QStringLiteral("EQUALIZADOR");
    atlas_.draw_text(painter, caption, (kWidth - atlas_.text_width(caption)) / 2, 4);

    // Botao sem rotulo assado no sprite: o texto e desenhado por cima, e nao
    // sobrepoe nada.
    const auto labeled = [&](const QRect& area, const QString& label, bool active) {
        atlas_.draw(painter,
                    QStringLiteral("toggle/blank/%1")
                        .arg(QLatin1String(active ? "active" : "normal")),
                    area.x(), area.y());
        atlas_.draw_text(painter, label,
                         area.x() + (area.width() - atlas_.text_width(label)) / 2, area.y() + 3);
    };
    labeled(kBypass, equalizer_.bypass() ? QStringLiteral("OFF") : QStringLiteral("ON"),
            !equalizer_.bypass());
    labeled(kReset, QStringLiteral("RST"), false);
    labeled(kPreset, QStringLiteral("PRE"), false);
    atlas_.draw_text(painter, preset_name_, kPresetName.x(), kPresetName.y());

    // Sliders.
    for (int index = -1; index < Equalizer::kBands; ++index) {
        const QRect area = slider_rect(index);
        const bool active = index < 0 || equalizer_.band_active(index);

        // Trilho: poco escuro estreito no centro da area clicavel.
        const QRect groove(area.x() + area.width() / 2 - 1, area.y(), 3, area.height());
        painter.fillRect(groove.x() * s, groove.y() * s, groove.width() * s, groove.height() * s,
                         atlas_.color(QStringLiteral("well")));

        // Marca do zero, para o usuario achar a resposta plana sem contar pixel.
        const int middle = area.y() + (area.height() - kThumbHeight) / 2 + kThumbHeight / 2;
        painter.fillRect(area.x() * s, middle * s, area.width() * s, s,
                         atlas_.color(QStringLiteral("bevel_light")));

        const float db = index < 0 ? equalizer_.preamp_db() : equalizer_.band_db(index);
        const float fraction = 0.5f - db / (2.0f * Equalizer::kRangeDb);
        const int travel = area.height() - kThumbHeight;
        const int y = area.y() + static_cast<int>(fraction * travel);

        // EQ-12 — banda inativa por Nyquist aparece esmaecida.
        const QString thumb = QStringLiteral("slider/thumb/%1")
                                  .arg(QLatin1String(dragging_ == index ? "pressed"
                                                     : active           ? "normal"
                                                                        : "active"));
        atlas_.draw(painter, thumb, area.x() + (area.width() - 11) / 2, y);
    }

    // Rotulos de frequencia.
    atlas_.draw_text(painter, QStringLiteral("PRE"), kPreampX + 1, kLabelY);
    for (int band = 0; band < Equalizer::kBands; ++band) {
        const QString label = band_label(band);
        const QRect area = slider_rect(band);
        const int x = area.x() + (area.width() - atlas_.text_width(label)) / 2;
        atlas_.draw_text(painter, label, x, kLabelY,
                         equalizer_.band_active(band) ? QColor()
                                                      : atlas_.color(QStringLiteral("green_dim")));
    }
}

void EqualizerPanel::open_preset_menu() {
    QMenu menu(this);
    const auto& builtin = core::dsp::builtin_presets();
    for (const auto& preset : builtin)
        menu.addAction(QString::fromStdString(preset.name));
    if (!user_presets_.empty()) {
        menu.addSeparator();
        for (const auto& preset : user_presets_)
            menu.addAction(QStringLiteral("* %1").arg(QString::fromStdString(preset.name)));
    }

    QAction* chosen = menu.exec(QCursor::pos());
    if (!chosen) return;

    const QString name = chosen->text();
    for (const auto& preset : builtin)
        if (QString::fromStdString(preset.name) == name) {
            core::dsp::apply(equalizer_, core::dsp::from_preset(preset));
            preset_name_ = name.toUpper();
            update();
            return;
        }
    for (const auto& preset : user_presets_)
        if (QStringLiteral("* %1").arg(QString::fromStdString(preset.name)) == name) {
            core::dsp::apply(equalizer_, core::dsp::from_preset(preset));
            preset_name_ = QString::fromStdString(preset.name).toUpper();
            update();
            return;
        }
}

void EqualizerPanel::mousePressEvent(QMouseEvent* event) {
    const QPoint p = to_logical(event->pos());

    if (kBypass.contains(p)) {
        equalizer_.set_bypass(!equalizer_.bypass());
        update();
        return;
    }
    if (kReset.contains(p)) {
        equalizer_.reset();
        preset_name_ = QStringLiteral("PLANO");
        update();
        return;
    }
    if (kPreset.contains(p)) {
        open_preset_menu();
        return;
    }

    const int index = slider_at(p);
    if (index != -2) {
        dragging_ = index;
        apply_slider(index, p.y());
    }
}

void EqualizerPanel::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_ == -2) return;
    apply_slider(dragging_, to_logical(event->pos()).y());
}

void EqualizerPanel::mouseReleaseEvent(QMouseEvent*) {
    dragging_ = -2;
    update();
}

}  // namespace pang::ui
