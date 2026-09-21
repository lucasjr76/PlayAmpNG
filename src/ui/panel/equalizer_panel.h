#pragma once

#include <QWidget>

#include <functional>

#include <vector>

#include "core/dsp/equalizer.h"
#include "core/dsp/presets.h"
#include "ui/skin/winamp_skin.h"

namespace pang::ui {

// Painel do equalizador, 275x116, desenhado a partir do skin nas coordenadas
// do formato: preamp em 21,38 e as dez bandas a partir de 78 com passo 18.
class EqualizerPanel : public QWidget {
public:
    // AP-08 — quem assume o arraste da faixa de titulo.
    //
    // Devolve true quando o shell tomou conta e vai aplicar encaixe. Quando
    // devolve false — ou nao existe — cai no startSystemMove(), que entrega o
    // arraste ao compositor: e o unico caminho no Wayland, e la nao ha encaixe
    // porque o cliente nao sabe nem define a propria posicao.
    std::function<bool(const QPoint& global)> on_title_drag;

    static constexpr int kWidth = 275;
    static constexpr int kHeight = 116;

    EqualizerPanel(core::dsp::Equalizer& equalizer,
                   std::vector<core::dsp::EqPreset>& user_presets, skin::WinampSkin& skin,
                   QWidget* parent = nullptr);

    void set_scale(int scale);
    void refresh();

    // EQ-07 — a colecao de presets do usuario mudou e deve ser gravada JA.
    // Um preset e criado de proposito; esperar o encerramento do player para
    // grava-lo perderia o trabalho se o processo fosse morto antes.
    std::function<void()> on_presets_changed;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;



private:
    // EQ-07 — nome sugerido ao salvar: o do ultimo preset do usuario
    // aplicado. E o que transforma "salvar como" em "editar" sem um comando a
    // mais: aplica, ajusta as bandas, salva, confirma o nome que ja vem
    // preenchido.
    std::string last_user_preset_;

    void save_current_as_preset();
    void delete_preset(const std::string& name);

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
