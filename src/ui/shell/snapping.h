#pragma once

#include <QPoint>
#include <QRect>
#include <QVector>

namespace pang::ui::shell {

// AP-08, AP-09 — encaixe magnetico entre janelas destacadas.
//
// Geometria pura, sem janela e sem Qt Widgets: e o mesmo arranjo de
// recovery.h, e pelo mesmo motivo. A regra de encaixe e o que erra na
// pratica — limiar, sentido do deslocamento, qual borda casa com qual — e
// isso se verifica com retangulos, num teste que roda sem tela. O que sobra
// para a janela e chamar estas funcoes.
//
// Todas as coordenadas sao em pixels de TELA, ja escalados. Encaixe e uma
// relacao entre janelas na area de trabalho, e nao entre pecas do skin.

// Distancia maxima, em pixels, para que duas bordas se atraiam. O valor vem da
// especificacao.
constexpr int kSnapThreshold = 10;

// Deslocamento a somar a `moving` para que ele encoste em algum de `fixed`.
//
// Os eixos sao decididos separadamente: uma janela pode encaixar a borda de
// baixo na de cima de outra (eixo Y) e, ao mesmo tempo, alinhar a lateral
// esquerda (eixo X). Sao dois encaixes, e exigir que viessem do mesmo vizinho
// faria o alinhamento lateral se perder toda vez que a janela de referencia
// nao fosse a mesma.
//
// Devolve QPoint(0, 0) quando nada esta perto o bastante — e o chamador nao
// precisa tratar "nao encaixou" como caso especial.
QPoint snap_offset(const QRect& moving, const QVector<QRect>& fixed,
                   int threshold = kSnapThreshold);

// AP-09 — duas janelas estao ENCOSTADAS quando uma borda coincide exatamente e
// ha sobreposicao na outra direcao.
//
// A sobreposicao importa: dois retangulos cujas bordas se tocam apenas num
// canto, sem lado em comum, nao formam um conjunto que o usuario reconheceria
// como grudado — e arrasta-los juntos surpreenderia.
bool touching(const QRect& a, const QRect& b);

// AP-09 — indices das janelas que se movem junto com `origin`, incluindo ele.
//
// E o fecho TRANSITIVO de `touching`: com o painel principal, o equalizador
// colado nele e a playlist colada no equalizador, arrastar o principal leva os
// tres. Parar no vizinho direto deixaria a playlist para tras, que e
// exatamente o defeito que um usuario nota na primeira tentativa.
QVector<int> group_of(int origin, const QVector<QRect>& windows);

}  // namespace pang::ui::shell
