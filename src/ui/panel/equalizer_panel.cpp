#include "ui/panel/equalizer_panel.h"

#include <QMenu>
#include <QMouseEvent>
#include <QWindow>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>

#include "ui/skin/winamp_layout.h"

namespace pang::ui {

// Altura da faixa de titulo, em pixels logicos — a mesma dos outros paineis.
constexpr int kTitlebarHeight = 14;
namespace {

namespace wa = skin::winamp;
using core::dsp::Equalizer;

constexpr int kSliderWidth = 14;
constexpr int kSliderHeight = 63;
constexpr int kThumbHeight = 11;
constexpr int kThumbWidth = 11;

const QRect kOnArea{wa::kEqOnAt.x(), wa::kEqOnAt.y(), 26, 12};
const QRect kResetArea{wa::kEqAutoAt.x(), wa::kEqAutoAt.y(), 32, 12};
const QRect kPresetsArea{wa::kEqPresetsAt.x(), wa::kEqPresetsAt.y(), 44, 12};

}  // namespace

EqualizerPanel::EqualizerPanel(core::dsp::Equalizer& equalizer,
                               std::vector<core::dsp::EqPreset>& user_presets,
                               skin::WinampSkin& skin, QWidget* parent)
    : QWidget(parent), equalizer_(equalizer), user_presets_(user_presets), skin_(skin) {
    setFocusPolicy(Qt::StrongFocus);
    // IN-03 — o painel nao tem widget por controle, entao o que o leitor de
    // tela anuncia e a janela. O detalhe de cada controle vem do tooltip, que
    // segue a posicao do cursor.
    setAccessibleName(tr("Equalizador"));
    setAccessibleDescription(tr("Preamp e dez bandas, de 60 Hz a 16 kHz"));
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    set_scale(skin.scale());
}

void EqualizerPanel::set_scale(int) {
    setFixedSize(kWidth * skin_.scale(), kHeight * skin_.scale());
    update();
}

void EqualizerPanel::refresh() { update(); }

QPoint EqualizerPanel::to_logical(const QPoint& physical) const {
    const int s = std::max(1, skin_.scale());
    return QPoint(physical.x() / s, physical.y() / s);
}

QRect EqualizerPanel::slider_rect(int index) const {
    const QPoint at = index < 0 ? wa::kPreampAt : wa::band_at(index);
    return QRect(at.x(), at.y(), kSliderWidth, kSliderHeight);
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
    const float fraction = std::clamp(
        static_cast<float>(logical_y - area.y() - kThumbHeight / 2) / travel, 0.0f, 1.0f);
    // Topo e ganho maximo.
    const float db = (0.5f - fraction) * 2.0f * Equalizer::kRangeDb;

    if (index < 0)
        equalizer_.set_preamp_db(db);
    else
        equalizer_.set_band_db(index, db);
    update();
}

void EqualizerPanel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    const int s = skin_.scale();

    skin_.draw(painter, wa::kEqualizerBackground, {0, 0});

    // Rotulos dos botoes, que o fundo do formato nao traz.
    //
    // O ON e alternancia, e alternancia precisa de um sinal que se leia a 1x:
    // a diferenca entre dois tons de verde no texto nao se le. O LED e o mesmo
    // recurso dos botoes EQ, PL, SHUFFLE e REP do painel principal.
    const int led = skin_.scale();
    const QColor kCream{252, 251, 233};
    painter.fillRect((kOnArea.x() + 3) * led, (kOnArea.y() + 3) * led, 5 * led, 5 * led,
                     equalizer_.bypass() ? QColor(34, 38, 50) : QColor(84, 142, 38));
    skin_.draw_text(painter, equalizer_.bypass() ? QStringLiteral("off") : QStringLiteral("on"),
                    {kOnArea.x() + 10, kOnArea.y() + 3}, kCream);
    skin_.draw_text(painter, QStringLiteral("reset"), {kResetArea.x() + 6, kResetArea.y() + 3},
                    kCream);
    skin_.draw_text(painter, QStringLiteral("presets"),
                    {kPresetsArea.x() + 6, kPresetsArea.y() + 3}, kCream);

    // Curva de resposta: interpolacao de Catmull-Rom entre as dez bandas. A
    // linear ligava por segmentos retos e virava zigue-zague com ganhos
    // alternados.
    const QRect curve = wa::kEqCurve;
    const int middle_y = curve.y() + curve.height() / 2;
    // A horizontal de 0 dB ja vem gravada na grade do eqmain; aqui so reforca
    // no trecho que a curva cobre.
    painter.fillRect((curve.x() + 2) * s, middle_y * s, (curve.width() - 4) * s, s,
                     QColor(86, 86, 116));

    if (!equalizer_.bypass()) {
        // Curva em dourado, a cor que a referencia usa para ela.
        painter.setPen(QPen(QColor(217, 203, 71), s));
        const int span = curve.width() - 6;
        const int half = curve.height() / 2 - 2;
        const auto band = [this](int i) {
            return equalizer_.band_db(std::clamp(i, 0, Equalizer::kBands - 1));
        };
        QPointF previous;
        for (int x = 0; x <= span; ++x) {
            const float position = static_cast<float>(x) / span * (Equalizer::kBands - 1);
            const int i = std::clamp(static_cast<int>(position), 0, Equalizer::kBands - 2);
            const float t = position - i;
            const float p0 = band(i - 1), p1 = band(i), p2 = band(i + 1), p3 = band(i + 2);
            const float db = 0.5f * ((2.0f * p1) + (-p0 + p2) * t +
                                     (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t +
                                     (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t * t * t);
            const qreal y =
                (middle_y - std::clamp(db, -Equalizer::kRangeDb, Equalizer::kRangeDb) /
                                Equalizer::kRangeDb * half) * s;
            const QPointF point((curve.x() + 3 + x) * s, y);
            if (x > 0) painter.drawLine(previous, point);
            previous = point;
        }
    }

    // Sliders: o fundo escolhido representa o GANHO da banda — verde no corte,
    // laranja no reforco. E a cor que diz o valor, como no formato.
    for (int index = -1; index < Equalizer::kBands; ++index) {
        const QRect area = slider_rect(index);
        const float gain = index < 0 ? equalizer_.preamp_db() : equalizer_.band_db(index);
        const float normalized = (gain + Equalizer::kRangeDb) / (2.0f * Equalizer::kRangeDb);
        const int frame = std::clamp(
            static_cast<int>(normalized * (wa::kEqSliderFrames - 1)), 0, wa::kEqSliderFrames - 1);

        skin_.draw(painter, wa::Sprite{"eqmain", wa::eq_slider_frame(frame)}, area.topLeft());

        const int travel = area.height() - kThumbHeight;
        const int y = area.y() + static_cast<int>((0.5f - gain / (2.0f * Equalizer::kRangeDb)) *
                                                  travel);
        skin_.draw(painter,
                   dragging_ == index ? wa::kEqualizerThumbPressed : wa::kEqualizerThumbNormal,
                   {area.x() + (area.width() - kThumbWidth) / 2, y});
    }
}

void EqualizerPanel::open_preset_menu() {
    QMenu menu(this);
    const auto& builtin = core::dsp::builtin_presets();
    for (const auto& preset : builtin) menu.addAction(QString::fromStdString(preset.name));
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
            update();
            return;
        }
    for (const auto& preset : user_presets_)
        if (QStringLiteral("* %1").arg(QString::fromStdString(preset.name)) == name) {
            core::dsp::apply(equalizer_, core::dsp::from_preset(preset));
            update();
            return;
        }
}

void EqualizerPanel::mousePressEvent(QMouseEvent* event) {
    const QPoint p = to_logical(event->pos());

    if (kOnArea.contains(p)) {
        equalizer_.set_bypass(!equalizer_.bypass());
        update();
        return;
    }
    if (kResetArea.contains(p)) {
        equalizer_.reset();
        update();
        return;
    }
    if (kPresetsArea.contains(p) || wa::kEqCurve.contains(p)) {
        open_preset_menu();
        return;
    }

    const int index = slider_at(p);
    if (index != -2) {
        dragging_ = index;
        apply_slider(index, p.y());
        return;
    }

    // AP-08 — a faixa de titulo arrasta a janela. Ate aqui o equalizador
    // destacado nao podia ser movido de lugar nenhum: era uma janela sem
    // decoracao e sem arraste proprio.
    if (p.y() < kTitlebarHeight) {
        if (on_title_drag && on_title_drag(event->globalPosition().toPoint())) return;
        if (window()->windowHandle()) window()->windowHandle()->startSystemMove();
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
