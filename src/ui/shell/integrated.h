#pragma once

#include <QPoint>
#include <QRect>
#include <QVector>
#include <QWidget>

#include <functional>

#include "ui/panel/equalizer_panel.h"
#include "ui/panel/main_panel.h"
#include "ui/panel/playlist_panel.h"
#include "ui/skin/winamp_skin.h"

namespace pang::ui::shell {

// Modo integrado: os tres paineis empilhados em UMA janela.
//
// E o modo padrao e o unico garantido em toda plataforma. O Wayland nao permite
// ao cliente ler nem definir a posicao global da propria janela, entao encaixe
// magnetico entre janelas de topo separadas e impossivel la — e o ambiente
// principal de desenvolvimento e Wayland. Ver ARCHITECTURE.md secao 8.
//
// Empilhar tambem resolve, por construcao, tres defeitos observados em teste
// manual com janelas separadas (docs/DEFEITOS.md A-1, A-2 e A-3): os paineis
// abrem e fecham juntos, tem a mesma largura, e nao ha como fechar a janela
// principal e ficar sem acesso as outras.
class IntegratedShell : public QWidget {
public:
    IntegratedShell(MainPanel* main, EqualizerPanel* equalizer, PlaylistPanel* playlist,
                    skin::WinampSkin& skin, QWidget* parent = nullptr);

    void set_equalizer_visible(bool visible);
    void set_playlist_visible(bool visible);
    bool equalizer_visible() const { return equalizer_visible_; }
    bool playlist_visible() const { return playlist_visible_; }

    // AP-11 — modo compacto: so a faixa de titulo do painel principal.
    void set_compact(bool compact);

    // IN-06 — melhor esforco por plataforma. No Wayland o compositor pode
    // ignorar o pedido; a janela continua funcionando, so nao fica acima.
    void set_always_on_top(bool on);
    bool always_on_top() const { return always_on_top_; }
    bool compact() const { return compact_; }

    void set_scale(int scale);
    void relayout();

    // AP-08/AP-09 — modo destacado: cada painel vira janela de topo. Onde o
    // sistema de janelas permite, da para posicionar livremente; no Wayland o
    // compositor decide, e nao ha encaixe magnetico. Ver ARCHITECTURE.md 8.
    void set_detached(bool detached);
    bool detached() const { return detached_; }

    std::function<void()> on_layout_changed;

    // AP-08, AP-09 — arraste com encaixe. Chamado pelos paineis quando o
    // usuario pega a faixa de titulo; devolve false quando nao ha o que fazer
    // aqui (modo integrado, ou plataforma que nao deixa posicionar), e ai o
    // painel entrega o arraste ao compositor.
    bool begin_title_drag(QWidget* panel, const QPoint& global);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    // Janelas de topo existentes no modo destacado, na ordem em que aparecem.
    QVector<QWidget*> detached_windows();

    QWidget* dragging_ = nullptr;
    QPoint drag_origin_;                 // cursor, em coordenadas de tela
    QVector<QWidget*> drag_group_;       // o que se move junto
    QVector<QRect> drag_group_start_;    // geometria inicial de cada um
    QVector<QRect> drag_others_;         // o que fica parado, e serve de ima

    MainPanel* main_;
    EqualizerPanel* equalizer_;
    PlaylistPanel* playlist_;
    skin::WinampSkin& skin_;

    bool equalizer_visible_ = false;
    bool playlist_visible_ = true;
    bool compact_ = false;
    bool always_on_top_ = false;
    bool detached_ = false;
};

}  // namespace pang::ui::shell
