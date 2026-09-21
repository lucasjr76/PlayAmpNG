#pragma once

#include <QPoint>
#include <QRect>
#include <QString>
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
// Quem chama decide QUANDO usar isto: no player, so o arraste do painel
// principal forma conjunto. Arrastar uma janela secundaria move apenas ela, e
// e assim que se desprende uma do grupo — sem essa assimetria as janelas
// grudam e nunca mais se separam.
//
// E o fecho TRANSITIVO de `touching`: com o painel principal, o equalizador
// colado nele e a playlist colada no equalizador, arrastar o principal leva os
// tres. Parar no vizinho direto deixaria a playlist para tras, que e
// exatamente o defeito que um usuario nota na primeira tentativa.
QVector<int> group_of(int origin, const QVector<QRect>& windows);

// AP-18 — se a plataforma deixa o cliente posicionar a propria janela.
//
// No Wayland nao deixa: o xdg-shell nao expoe posicao absoluta nem aceita
// posicionamento pelo cliente, e o Qt nao contorna restricao de compositor.
// Sem posicionar, nao ha encaixe nem movimento em grupo — e a interface nao
// pode oferecer um controle que nao funciona.
//
// Decidido em tempo de EXECUCAO, e nao de compilacao: o mesmo binario roda em
// X11 e em Wayland, e um #ifdef responderia pela maquina que compilou.
bool platform_can_position_windows();

// A decisao em si, separada de quem pergunta ao Qt.
//
// Existe para ser TESTAVEL: nao da para subir uma sessao Wayland num teste, e
// depender de QGuiApplication::platformName() faria a regra so ser exercitada
// na plataforma em que o teste roda. Recebe o nome como o Qt o informa —
// minusculo e estavel: "wayland", "xcb", "windows", "cocoa", "offscreen".
bool snapping_available(const QString& platform_name);

// Frase curta para a interface explicar por que a opcao esta desabilitada.
// Vazia quando o encaixe esta disponivel.
QString snapping_unavailable_reason();

}  // namespace pang::ui::shell
