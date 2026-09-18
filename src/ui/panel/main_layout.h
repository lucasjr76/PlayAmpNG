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
constexpr int kRightEdge = 265;                       // ultima coluna util
constexpr int kLeftColumnX = kMarginX;                // 9
constexpr int kLeftColumnW = 78;                      // 9..86
constexpr int kRightColumnX = 95;                     // 95..265
constexpr int kRightColumnW = kRightEdge - kRightColumnX + 1;   // 171
constexpr int kContentW = kRightEdge - kMarginX + 1;            // 257

constexpr int kRow1Y = 19, kRow1H = 19;   // mostradores: tempo e titulo
constexpr int kRow2Y = 41, kRow2H = 19;   // visualizacao, volume, balanco, EQ/PL
constexpr int kRow3Y = 63;                // linha de informacao
constexpr int kRow4Y = 74, kRow4H = 11;   // barra de posicao
constexpr int kRow5Y = 88, kRow5H = 18;   // transporte

// Centra verticalmente uma altura dentro de uma linha da grade.
constexpr int centered_in(int row_y, int row_h, int height) {
    return row_y + (row_h - height) / 2;
}

constexpr QRect kTitlebar{0, 0, 275, 14};
constexpr QRect kMinimize{237, 3, 9, 9};
constexpr QRect kShade{247, 3, 9, 9};
constexpr QRect kClose{257, 3, 9, 9};

constexpr QRect kTimeWell{kLeftColumnX, kRow1Y, kLeftColumnW, kRow1H};
constexpr QRect kTime{kLeftColumnX, kRow1Y + 2, kLeftColumnW, 14};
constexpr QRect kTitleWell{kRightColumnX, kRow1Y, kRightColumnW, kRow1H};
constexpr QRect kTitle{kRightColumnX + 3, kRow1Y + 4, kRightColumnW - 6, 12};

// Moldura e area util sao retangulos diferentes: 19 barras de 3 px com 1 px de
// intervalo pedem exatamente 76 px uteis, e a moldura e 1 px maior de cada lado.
constexpr QRect kVisFrame{kLeftColumnX, kRow2Y, kLeftColumnW, kRow2H};
constexpr QRect kVis{kLeftColumnX + 1, kRow2Y + 1, 76, kRow2H - 2};

constexpr QRect kVolume{kRightColumnX, centered_in(kRow2Y, kRow2H, 13), 76, 13};
constexpr QRect kBalance{175, centered_in(kRow2Y, kRow2H, 13), 42, 13};
constexpr QRect kEqualizer{221, centered_in(kRow2Y, kRow2H, 13), 22, 13};
constexpr QRect kPlaylist{244, centered_in(kRow2Y, kRow2H, 13), 22, 13};

constexpr QRect kPosition{kMarginX, kRow4Y, kContentW, kRow4H};

constexpr QRect kPrevious{kMarginX, kRow5Y, 23, 18};
constexpr QRect kPlay{kMarginX + 23, kRow5Y, 23, 18};
constexpr QRect kPause{kMarginX + 46, kRow5Y, 23, 18};
constexpr QRect kStop{kMarginX + 69, kRow5Y, 23, 18};
constexpr QRect kNext{kMarginX + 92, kRow5Y, 23, 18};
constexpr QRect kEject{127, centered_in(kRow5Y, kRow5H, 16), 22, 16};
constexpr QRect kShuffle{174, centered_in(kRow5Y, kRow5H, 15), 46, 15};
constexpr QRect kRepeat{238, centered_in(kRow5Y, kRow5H, 15), 28, 15};

// Linha de informacao: grupo da esquerda a partir da coluna direita, grupo de
// canais encostado na borda direita.
constexpr int kBitrateRight = 117;
constexpr int kBitrateLabel = 120;
constexpr int kSampleRateRight = 161;
constexpr int kSampleRateLabel = 164;
constexpr int kMono = 202;
constexpr int kStereo = 233;
constexpr int kInfoY = kRow3Y;

}  // namespace pang::ui::layout
