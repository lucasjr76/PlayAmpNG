#pragma once

#include <QWidget>

#include <vector>

#include "core/dsp/equalizer.h"
#include "core/dsp/presets.h"
#include "ui/skin/winamp_skin.h"

namespace pang::ui {

// Painel do equalizador, 275x116, desenhado a partir do skin nas coordenadas
// do formato: preamp em 21,38 e as dez bandas a partir de 78 com passo 18.
class EqualizerPanel : public QWidget {
public:
    static constexpr int kWidth = 275;
    static constexpr int kHeight = 116;

    EqualizerPanel(core::dsp::Equalizer& equalizer,
                   std::vector<core::dsp::EqPreset>& user_presets, skin::WinampSkin& skin,
                   QWidget* parent = nullptr);

    void set_scale(int scale);
    void refresh();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    int slider_at(const QPoint& logical) const;   // -1 preamp, 0..9 bandas, -2 nenhum
    QRect slider_rect(int index) const;
    void apply_slider(int index, int logical_y);
    QPoint to_logical(const QPoint& physical) const;
    void open_preset_menu();

    core::dsp::Equalizer& equalizer_;
    std::vector<core::dsp::EqPreset>& user_presets_;
    skin::WinampSkin& skin_;
    int dragging_ = -2;
};

}  // namespace pang::ui
