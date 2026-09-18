#pragma once

#include <QRect>
#include <QVector>

namespace pang::ui::shell {

// AP-14 — recuperacao de janela fora da area visivel.
//
// A regra olha a FAIXA DE ARRASTE, e nao a janela inteira: e ela que o usuario
// precisa alcancar para resgatar a janela. Exigir uma intersecao de 64x64 px,
// como dizia a versao 0.1 do documento, reprovaria uma barra compacta de
// 275x14 px perfeitamente visivel.
//
// Considera-se visivel quando a faixa superior de altura min(16, altura) tem ao
// menos 64 px de largura dentro da geometria disponivel de alguma tela.
constexpr int kDragStripHeight = 16;
constexpr int kMinimumVisibleWidth = 64;

bool is_reachable(const QRect& window, const QVector<QRect>& screens);

// Devolve a posicao corrigida. Quando ja esta alcancavel, devolve o mesmo
// retangulo; caso contrario reposiciona no canto superior esquerdo da tela
// primaria, com uma folga.
QRect recover(const QRect& window, const QVector<QRect>& screens, const QRect& primary);

}  // namespace pang::ui::shell
