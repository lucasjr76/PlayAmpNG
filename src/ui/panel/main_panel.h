#pragma once

#include <QWidget>

#include <functional>
#include <vector>

#include "core/dsp/analyzer.h"
#include "core/state/controller.h"
#include "ui/skin/winamp_skin.h"

namespace pang::ui {

// Painel principal, 275x116 na escala 1x.
//
// Tudo e desenhado a partir do skin carregado, nas coordenadas do formato
// (ui/skin/winamp_layout.h). Nao ha QPushButton nem QSlider aqui: o visual e
// pixel art blitada, e uma arvore de widgets tematicos brigaria com o estilo
// do sistema em vez de reproduzi-lo.
class MainPanel : public QWidget {
public:
    static constexpr int kWidth = 275;
    static constexpr int kHeight = 116;
    static constexpr int kCompactHeight = 14;

    enum class Visualization { Off, Spectrum, Scope };

    MainPanel(core::Controller& controller, core::Engine& engine, skin::WinampSkin& skin,
              QWidget* parent = nullptr);

    void set_scale(int scale);
    int scale() const { return skin_.scale(); }

    // AP-11 — modo compacto: so a faixa de titulo.
    void set_compact(bool compact);
    bool compact() const { return compact_; }

    void set_visualization(Visualization mode);
    Visualization visualization() const { return visualization_; }

    void tick(float dt_seconds);

    std::function<void()> on_open;
    std::function<void()> on_toggle_equalizer;
    std::function<void()> on_toggle_playlist;
    std::function<bool()> equalizer_visible;
    std::function<bool()> playlist_visible;
    std::function<void(int)> on_scale_changed;
    std::function<void()> on_toggle_compact;

    // AU-11 — menu de contexto. O painel nao conhece dispositivos de audio;
    // quem monta o menu e o shell, que ja conhece o AudioOutput.
    std::function<void(const QPoint&)> on_context_menu;

protected:
    void contextMenuEvent(QContextMenuEvent*) override;

    // IN-03 — o painel e desenhado a mao e nao tem widget por controle, entao
    // nao ha a quem pendurar tooltip. O teste de acerto por posicao, que ja
    // existe para o clique, serve tambem para dizer o que esta sob o cursor.
    bool event(QEvent*) override;
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
    static QString control_name(Hit hit);
    QPoint to_logical(const QPoint& physical) const;

    void paint_display(QPainter&);
    void paint_visualization(QPainter&);
    void paint_sliders(QPainter&);
    void paint_buttons(QPainter&);

    core::Controller& controller_;
    core::Engine& engine_;
    skin::WinampSkin& skin_;
    core::dsp::SpectrumAnalyzer analyzer_;
    std::vector<float> capture_;

    Visualization visualization_ = Visualization::Spectrum;
    Hit pressed_ = Hit::None;
    Hit dragging_ = Hit::None;

    bool compact_ = false;
    bool show_remaining_ = false;   // PL-15
    int title_offset_ = 0;          // PL-14 — rolagem
    float title_timer_ = 0.0f;
    core::Snapshot snapshot_{};
};

}  // namespace pang::ui
