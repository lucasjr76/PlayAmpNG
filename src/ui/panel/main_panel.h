#pragma once

#include <QWidget>

#include <functional>

#include "core/dsp/analyzer.h"
#include "core/state/controller.h"
#include "ui/skin/atlas.h"

namespace pang::ui {

// Painel principal, 275x116 na escala 1x.
//
// Tudo e desenhado com sprites do atlas: nao ha QPushButton nem QSlider aqui.
// O visual do Winamp Classic e pixel art blitada, e uma arvore de widgets
// tematicos brigaria com o estilo do sistema em vez de reproduzi-lo.
class MainPanel : public QWidget {
public:
    static constexpr int kWidth = 275;
    static constexpr int kHeight = 116;
    static constexpr int kCompactHeight = 14;

    enum class Visualization { Off, Spectrum, Scope };

    MainPanel(core::Controller& controller, core::Engine& engine, skin::Atlas& atlas,
              QWidget* parent = nullptr);

    void set_scale(int scale);
    int scale() const { return atlas_.scale(); }

    // AP-11 — modo compacto: so a faixa de titulo, com o tempo e o titulo da
    // faixa. Os controles somem; a janela vira uma barra.
    void set_compact(bool compact);
    bool compact() const { return compact_; }

    void set_visualization(Visualization mode);
    Visualization visualization() const { return visualization_; }

    // Chamado pelo temporizador da interface.
    void tick(float dt_seconds);

    // Acionados pelos botoes correspondentes.
    std::function<void()> on_open;
    std::function<void()> on_toggle_equalizer;
    std::function<void()> on_toggle_playlist;
    std::function<bool()> equalizer_visible;
    std::function<bool()> playlist_visible;
    std::function<void(int)> on_scale_changed;
    std::function<void()> on_toggle_compact;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    enum class Hit {
        None, Titlebar, Previous, Play, Pause, Stop, Next, Eject,
        Shuffle, Repeat, Equalizer, Playlist, Volume, Balance, Position, Time, Vis,
        Minimize, Shade, Close
    };

    Hit hit_test(const QPoint& logical) const;
    QPoint to_logical(const QPoint& physical) const;
    const char* state_for(Hit hit) const;

    void paint_frame(QPainter&);
    void paint_display(QPainter&);
    void paint_visualization(QPainter&);
    void paint_sliders(QPainter&);
    void paint_buttons(QPainter&);

    void apply_slider(Hit hit, int logical_x);

    core::Controller& controller_;
    core::Engine& engine_;
    skin::Atlas& atlas_;
    core::dsp::SpectrumAnalyzer analyzer_;
    std::vector<float> capture_;

    Visualization visualization_ = Visualization::Spectrum;
    Hit pressed_ = Hit::None;
    Hit focus_ = Hit::None;
    Hit dragging_ = Hit::None;
    QPoint drag_origin_;

    bool compact_ = false;
    bool show_remaining_ = false;   // PL-15
    int title_offset_ = 0;          // PL-14 — rolagem
    float title_timer_ = 0.0f;
    core::Snapshot snapshot_{};
};

}  // namespace pang::ui
