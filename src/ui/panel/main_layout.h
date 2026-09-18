#pragma once

#include <QRect>

// Grade do painel principal, 275 x 116 na escala 1x.
//
// Vive num cabecalho proprio para que o teste de layout possa conferir as
// relacoes de alinhamento sem precisar do widget.
namespace pang::ui::layout {

// // A versao anterior tinha 21 bordas esquerdas distintas, 21 direitas e 12
// topos: nenhum elemento compartilhava aresta com outro. O poco do tempo
// comecava em 30 e o da visualizacao em 23, sendo que os dois formam a mesma
// coluna. Dai a impressao de que nada alinha com nada — porque nao alinhava.
//
// Tres colunas de referencia e cinco linhas. Um teste verifica as relacoes
// (tests/test_layout.cpp), para nao voltar a soltar.
constexpr int kMarginX = 9;
constexpr int kRightEdge = 265;
constexpr int kLeftColumnX = kMarginX;
constexpr int kLeftColumnW = 78;                                // 9..86
constexpr int kRightColumnX = 95;
constexpr int kRightColumnW = kRightEdge - kRightColumnX + 1;   // 171
constexpr int kContentW = kRightEdge - kMarginX + 1;            // 257

// Linhas da grade.
//
// O ritmo vertical foi refeito para encostar o transporte na base: antes
// sobravam 15 px abaixo dos botoes, o que deixava o painel com a impressao de
// estar cortado pela metade. Agora a margem inferior e de 8 px, igual a
// folga do topo.
constexpr int kRow1Y = 18;                 // mostradores
constexpr int kDisplayH = 41;              // bloco da esquerda: 18..58
constexpr int kTitleH = 19;                // poco do titulo: 18..36
constexpr int kInfoY = 43;
constexpr int kControlsY = 55, kControlsH = 13;   // 55..67
constexpr int kPositionY = 73, kPositionH = 10;   // 73..82
constexpr int kTransportY = 90, kTransportH = 18; // 90..107, base em 116

constexpr int centered_in(int row_y, int row_h, int height) {
    return row_y + (row_h - height) / 2;
}

constexpr QRect kTitlebar{0, 0, 275, 14};
constexpr QRect kMinimize{237, 3, 9, 9};
constexpr QRect kShade{247, 3, 9, 9};
constexpr QRect kClose{257, 3, 9, 9};

// BLOCO da esquerda: tempo e visualizacao dentro do MESMO poco preto.
//
// Eram dois pocos separados, com bordas diferentes. No original os dois formam
// um bloco continuo, e e isso que da a leitura de "mostrador" em vez de duas
// caixinhas soltas.
constexpr QRect kDisplayBlock{kLeftColumnX, kRow1Y, kLeftColumnW, kDisplayH};   // 9..86, 19..59
constexpr QRect kTime{kLeftColumnX + 1, kRow1Y + 2, kLeftColumnW - 2, 14};      // dentro do bloco
constexpr QRect kVis{kLeftColumnX + 1, kRow1Y + 19, 76, 20};                    // 10..85, 38..57

constexpr QRect kTitleWell{kRightColumnX, kRow1Y, kRightColumnW, kTitleH};
constexpr QRect kTitle{kRightColumnX + 3, kRow1Y + 4, kRightColumnW - 6, 12};

constexpr QRect kVolume{kRightColumnX, kControlsY, 76, kControlsH};             // 95..170
constexpr QRect kBalance{175, kControlsY, 42, kControlsH};                      // 175..216
constexpr QRect kEqualizer{221, kControlsY, 22, kControlsH};                    // 221..242
constexpr QRect kPlaylist{244, kControlsY, 22, kControlsH};                     // 244..265

constexpr QRect kPosition{kMarginX, kPositionY, kContentW, kPositionH};         // 9..265

constexpr QRect kPrevious{kMarginX, kTransportY, 23, 18};
constexpr QRect kPlay{kMarginX + 23, kTransportY, 23, 18};
constexpr QRect kPause{kMarginX + 46, kTransportY, 23, 18};
constexpr QRect kStop{kMarginX + 69, kTransportY, 23, 18};
constexpr QRect kNext{kMarginX + 92, kTransportY, 23, 18};
constexpr QRect kEject{127, centered_in(kTransportY, kTransportH, 16), 22, 16};
constexpr QRect kShuffle{174, centered_in(kTransportY, kTransportH, 15), 46, 15};
constexpr QRect kRepeat{238, centered_in(kTransportY, kTransportH, 15), 28, 15};

constexpr int kBitrateRight = 117;
constexpr int kBitrateLabel = 120;
constexpr int kSampleRateRight = 161;
constexpr int kSampleRateLabel = 164;
constexpr int kMono = 202;
constexpr int kStereo = 233;

}  // namespace pang::ui::layout
