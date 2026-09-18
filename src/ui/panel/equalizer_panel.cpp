#include "ui/panel/equalizer_panel.h"

#include <QMenu>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <algorithm>

namespace pang::ui {
namespace {

using core::dsp::Equalizer;

// Layout do Winamp Classic: ON a esquerda, mostrador de curva no meio,
// PRESETS a direita, preamp isolado em 21 e as dez bandas a partir de 78 com
// passo de 18 px.
constexpr QRect kTitlebar{0, 0, 275, 14};
constexpr QRect kBypass{14, 18, 26, 12};
constexpr QRect kReset{44, 18, 26, 12};
constexpr QRect kCurve{87, 17, 113, 19};
constexpr QRect kPreset{206, 18, 44, 12};

// Layout simetrico: 17 px de margem dos dois lados.
//
//   17 (margem) + 14 (preamp) + 24 (separacao) + 9*21 + 14 (bandas) + 17 = 275
//
// A versao anterior deixava 11 px a direita e um vao de 35 px depois do preamp,
// o que jogava o conjunto para a esquerda e sobrava espaco de um lado so.
// Largura IMPAR, para trilho, polegar e entalhe compartilharem o mesmo pixel
// central.
//
// Com 14 px e polegar de 11, os tres centros caiam em lugares diferentes:
// trilho em X+6,5; polegar em X+6; entalhe em X+6,5. Meio pixel de desvio, que
// em escala 3x vira um pixel e meio bem visivel. Com 13 e 11 tudo cai em X+6.
constexpr int kSliderTop = 38;
constexpr int kSliderHeight = 54;
constexpr int kSliderWidth = 13;
constexpr int kThumbWidthPx = 11;
constexpr int kPreampX = 17;
constexpr int kBandX = 55;
constexpr int kBandSpacing = 21;
constexpr int kThumbHeight = 11;
constexpr int kLabelY = 96;

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
    preset_name_ = QStringLiteral("PRESETS");
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

    preset_name_ = QStringLiteral("PRESETS");
    update();
}

void EqualizerPanel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    const int s = atlas_.scale();

    painter.fillRect(rect(), atlas_.color(QStringLiteral("background")));
    atlas_.draw_tiled(painter, QStringLiteral("frame/titlebar"), kTitlebar);
    const QString caption = QStringLiteral("EQUALIZADOR");
    const int caption_w = atlas_.text_width(caption);
    const int caption_x = (kWidth - caption_w) / 2;
    painter.fillRect((caption_x - 5) * s, 2 * s, (caption_w + 10) * s, 10 * s,
                     atlas_.color(QStringLiteral("background")));
    atlas_.draw_text(painter, caption, caption_x, (14 - atlas_.glyph_height()) / 2);

    const auto labeled = [&](const char* pill, const QRect& area, const QString& label,
                             bool active) {
        atlas_.draw(painter,
                    QStringLiteral("pill/%1/%2")
                        .arg(QLatin1String(pill), QLatin1String(active ? "active" : "normal")),
                    area.x(), area.y());
        atlas_.draw_text(painter, label,
                         area.x() + (area.width() - atlas_.text_width(label)) / 2,
                         area.y() + (area.height() - atlas_.glyph_height()) / 2);
    };
    labeled("small", kBypass, equalizer_.bypass() ? QStringLiteral("OFF") : QStringLiteral("ON"),
            !equalizer_.bypass());
    labeled("small", kReset, QStringLiteral("RST"), false);
    labeled("preset", kPreset, QStringLiteral("PRESETS"), false);

    // Mostrador da curva de resposta: poco escuro com a linha ligando os ganhos
    // das bandas. E o retorno que falta quando os sliders nao tem numero.
    painter.fillRect(kCurve.x() * s, kCurve.y() * s, kCurve.width() * s, kCurve.height() * s,
                     atlas_.color(QStringLiteral("well")));
    painter.fillRect(kCurve.x() * s, kCurve.y() * s, kCurve.width() * s, s,
                     atlas_.color(QStringLiteral("bevel_dark")));
    painter.fillRect(kCurve.x() * s, kCurve.bottom() * s, kCurve.width() * s, s,
                     atlas_.color(QStringLiteral("bevel_light")));

    const int middle_y = kCurve.y() + kCurve.height() / 2;
    painter.fillRect((kCurve.x() + 2) * s, middle_y * s, (kCurve.width() - 4) * s, s,
                     atlas_.color(QStringLiteral("green_dim")));

    if (!equalizer_.bypass()) {
        painter.setPen(atlas_.color(QStringLiteral("green")));
        const int span = kCurve.width() - 6;
        const int half = kCurve.height() / 2 - 2;
        QPointF previous;
        for (int x = 0; x <= span; ++x) {
            // Interpolacao linear entre as dez bandas.
            const float position =
                static_cast<float>(x) / span * (Equalizer::kBands - 1);
            const int left = std::clamp(static_cast<int>(position), 0, Equalizer::kBands - 1);
            const int right = std::min(left + 1, Equalizer::kBands - 1);
            const float blend = position - left;
            const float db = equalizer_.band_db(left) * (1.0f - blend) +
                             equalizer_.band_db(right) * blend;

            const qreal y = (middle_y - db / Equalizer::kRangeDb * half) * s;
            const QPointF point((kCurve.x() + 3 + x) * s, y);
            if (x > 0) painter.drawLine(previous, point);
            previous = point;
        }
    }

    // Sliders.
    for (int index = -1; index < Equalizer::kBands; ++index) {
        const QRect area = slider_rect(index);
        const bool active = index < 0 || equalizer_.band_active(index);

        // Trilho de COR SOLIDA, escolhida pelo GANHO da banda.
        //
        // No original cada banda tem a cor do proprio ajuste: verde quando
        // corta, amarelo perto do plano, laranja quando reforca. A versao
        // anterior desenhava uma fenda preta igual para todas, sem informacao
        // nenhuma.
        const int center = area.x() + area.width() / 2;
        const float gain = index < 0 ? equalizer_.preamp_db() : equalizer_.band_db(index);
        const float normalized = (gain + Equalizer::kRangeDb) / (2.0f * Equalizer::kRangeDb);

        const QVector<QColor>& gradient = atlas_.spectrum();
        QColor track = atlas_.color(QStringLiteral("green"));
        if (!gradient.isEmpty()) {
            const int gi = static_cast<int>(std::clamp(normalized, 0.0f, 1.0f) *
                                            (gradient.size() - 1));
            track = gradient[static_cast<std::size_t>(gi)];
        }
        if (!active || equalizer_.bypass()) track = track.darker(220);

        painter.fillRect((center - 3) * s, area.y() * s, 7 * s, area.height() * s, track);
        painter.fillRect((center - 4) * s, area.y() * s, s, area.height() * s,
                         atlas_.color(QStringLiteral("bevel_dark")));
        painter.fillRect((center + 4) * s, area.y() * s, s, area.height() * s,
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
        atlas_.draw(painter, thumb, area.x() + (area.width() - kThumbWidthPx) / 2, y);
    }

    // Marcas de escala em +12 dB, 0 dB e -12 dB, nos vaos ENTRE os sliders.
    //
    // Eu as tinha removido achando que a regua tracejada era invencao minha; a
    // referencia mostra que ela existe no original. O que estava errado era o
    // comprimento: traco curto no vao le como escala, traco colado no slider
    // vira uma linha continua atravessando o painel.
    const int travel = kSliderHeight - kThumbHeight;
    for (int step = 0; step <= 2; ++step) {
        const int y = kSliderTop + kThumbHeight / 2 + step * travel / 2;
        for (int band = -1; band < Equalizer::kBands; ++band) {
            const QRect area = slider_rect(band);
            const int gap_x = area.x() + area.width() + 2;
            if (band == Equalizer::kBands - 1) continue;
            painter.fillRect(gap_x * s, y * s, 4 * s, s,
                             atlas_.color(QStringLiteral("green_dim")));
        }
        // Tambem antes do primeiro e depois do ultimo, para fechar a escala.
        painter.fillRect((slider_rect(-1).x() - 6) * s, y * s, 4 * s, s,
                         atlas_.color(QStringLiteral("green_dim")));
        const QRect last = slider_rect(Equalizer::kBands - 1);
        painter.fillRect((last.x() + last.width() + 2) * s, y * s, 4 * s, s,
                         atlas_.color(QStringLiteral("green_dim")));
    }

    // Rotulos de frequencia.
    const auto centered = [&](const QString& label, const QRect& area, const QColor& tint) {
        atlas_.draw_text(painter, label,
                         area.x() + (area.width() - atlas_.text_width(label)) / 2, kLabelY, tint);
    };
    centered(QStringLiteral("PRE"), slider_rect(-1), QColor());
    for (int band = 0; band < Equalizer::kBands; ++band)
        centered(band_label(band), slider_rect(band),
                 equalizer_.band_active(band) ? QColor()
                                              : atlas_.color(QStringLiteral("green_dim")));
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
    if (kPreset.contains(p) || kCurve.contains(p)) {
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
