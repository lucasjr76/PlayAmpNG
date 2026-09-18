#pragma once

#include <QWidget>

#include "ui/panel/equalizer_panel.h"
#include "ui/panel/main_panel.h"
#include "ui/panel/playlist_panel.h"
#include "ui/skin/atlas.h"

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
                    skin::Atlas& atlas, QWidget* parent = nullptr);

    void set_equalizer_visible(bool visible);
    void set_playlist_visible(bool visible);
    bool equalizer_visible() const { return equalizer_visible_; }
    bool playlist_visible() const { return playlist_visible_; }

    // AP-11 — modo compacto: so a faixa de titulo do painel principal.
    void set_compact(bool compact);
    bool compact() const { return compact_; }

    void set_scale(int scale);
    void relayout();

private:
    MainPanel* main_;
    EqualizerPanel* equalizer_;
    PlaylistPanel* playlist_;
    skin::Atlas& atlas_;

    bool equalizer_visible_ = false;
    bool playlist_visible_ = true;
    bool compact_ = false;
};

}  // namespace pang::ui::shell
