#pragma once

#include <QWidget>

#include <array>
#include <functional>
#include <vector>

#include "core/dsp/equalizer.h"
#include "core/dsp/presets.h"
#include "ui/skin/atlas.h"

namespace pang::ui {

// Painel do equalizador, 275x116 na escala 1x — a mesma largura do painel
// principal, para os tres empilharem sem degrau.
class EqualizerPanel : public QWidget {
public:
    static constexpr int kWidth = 275;
    static constexpr int kHeight = 116;

    EqualizerPanel(core::dsp::Equalizer& equalizer,
                   std::vector<core::dsp::EqPreset>& user_presets, skin::Atlas& atlas,
                   QWidget* parent = nullptr);

    void set_scale(int scale);
    void refresh();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    // -1 = preamp, 0..9 = bandas.
    int slider_at(const QPoint& logical) const;
    QRect slider_rect(int index) const;
    void apply_slider(int index, int logical_y);
    QPoint to_logical(const QPoint& physical) const;
    void open_preset_menu();

    core::dsp::Equalizer& equalizer_;
    std::vector<core::dsp::EqPreset>& user_presets_;
    skin::Atlas& atlas_;

    int dragging_ = -2;  // -2 = nenhum
    QString preset_name_;
};

}  // namespace pang::ui
