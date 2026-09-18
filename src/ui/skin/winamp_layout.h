#pragma once

#include <QRect>
#include <QString>

// Geometria do formato de skin do Winamp 2.x.
//
// Isto e ESPECIFICACAO, nao arte: sao as coordenadas que qualquer skin do
// formato assume — onde cada peca vive dentro de cada bitmap, e onde cada peca
// e desenhada dentro da janela. Adotar esta geometria e o que faz as proporcoes
// baterem, independentemente de quem desenhou os pixels.
//
// As coordenadas anteriores do projeto eram aproximacoes minhas que cabiam em
// 275x116 mas nao tinham o ritmo do original.
namespace pang::ui::skin::winamp {

// ---------------------------------------------------------------- janelas

constexpr QSize kMainWindow{275, 116};
constexpr QSize kEqualizerWindow{275, 116};
constexpr int kPlaylistMinWidth = 275;
constexpr int kPlaylistMinHeight = 116;

// Uma peca: em qual bitmap ela mora e qual retangulo ocupa dentro dele.
struct Sprite {
    const char* bitmap;
    QRect source;
};

// ------------------------------------------------------ main.bmp (275x116)

constexpr Sprite kMainBackground{"main", {0, 0, 275, 116}};

// ------------------------------------------------- cbuttons.bmp (136x36)
//
// Cinco botoes de 23x18 na fileira de cima (repouso) e na de baixo
// (pressionado); o eject e menor e fica a direita.

constexpr Sprite kPreviousNormal{"cbuttons", {0, 0, 23, 18}};
constexpr Sprite kPreviousPressed{"cbuttons", {0, 18, 23, 18}};
constexpr Sprite kPlayNormal{"cbuttons", {23, 0, 23, 18}};
constexpr Sprite kPlayPressed{"cbuttons", {23, 18, 23, 18}};
constexpr Sprite kPauseNormal{"cbuttons", {46, 0, 23, 18}};
constexpr Sprite kPausePressed{"cbuttons", {46, 18, 23, 18}};
constexpr Sprite kStopNormal{"cbuttons", {69, 0, 23, 18}};
constexpr Sprite kStopPressed{"cbuttons", {69, 18, 23, 18}};
constexpr Sprite kNextNormal{"cbuttons", {92, 0, 23, 18}};
constexpr Sprite kNextPressed{"cbuttons", {92, 18, 23, 18}};
constexpr Sprite kEjectNormal{"cbuttons", {114, 0, 22, 16}};
constexpr Sprite kEjectPressed{"cbuttons", {114, 16, 22, 16}};

// Onde cada botao fica na janela principal.
constexpr QPoint kPreviousAt{16, 88};
constexpr QPoint kPlayAt{39, 88};
constexpr QPoint kPauseAt{62, 88};
constexpr QPoint kStopAt{85, 88};
constexpr QPoint kNextAt{108, 88};
constexpr QPoint kEjectAt{136, 89};

// ------------------------------------------------ titlebar.bmp (275x87)

constexpr Sprite kTitlebarInactive{"titlebar", {27, 0, 275, 14}};
constexpr Sprite kTitlebarActive{"titlebar", {27, 15, 275, 14}};
constexpr Sprite kShadeNormal{"titlebar", {0, 0, 9, 9}};
constexpr Sprite kShadePressed{"titlebar", {0, 9, 9, 9}};
constexpr Sprite kMinimizeNormal{"titlebar", {9, 0, 9, 9}};
constexpr Sprite kMinimizePressed{"titlebar", {9, 9, 9, 9}};
constexpr Sprite kCloseNormal{"titlebar", {18, 0, 9, 9}};
constexpr Sprite kClosePressed{"titlebar", {18, 9, 9, 9}};

constexpr QPoint kShadeAt{254, 3};
constexpr QPoint kMinimizeAt{244, 3};
constexpr QPoint kCloseAt{264, 3};

// -------------------------------------------- numbers.bmp (108x13)
//
// Onze celulas de 9x13: digitos 0 a 9 e uma celula vazia. O dois-pontos nao e
// um digito — ele faz parte do fundo em main.bmp.

constexpr int kDigitWidth = 9;
constexpr int kDigitHeight = 13;
constexpr const char* kDigitsBitmap = "numbers";

// As cinco casas do mostrador. O vao entre 60 e 78 e onde o dois-pontos do
// fundo aparece.
constexpr QPoint kTimeSignAt{36, 26};
constexpr QPoint kTimeMinuteTensAt{48, 26};
constexpr QPoint kTimeMinuteUnitsAt{60, 26};
constexpr QPoint kTimeSecondTensAt{78, 26};
constexpr QPoint kTimeSecondUnitsAt{90, 26};

// -------------------------------------------------- text.bmp (155x18)
//
// Fonte de 5x6, em tres fileiras de 31 caracteres. Minusculas usam o mesmo
// desenho das maiusculas.

constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 6;
constexpr int kGlyphsPerRow = 31;
constexpr const char* kTextBitmap = "text";

// Ordem das fileiras, do jeito que o formato define.
constexpr const char* kTextRow0 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ\"@ ";
constexpr const char* kTextRow1 = "0123456789.:()-'!_+\\/[]^&%,=$#";
constexpr const char* kTextRow2 = "ÅÖÄ?* ";

constexpr QRect kSongTitle{111, 27, 153, 6};
constexpr QRect kBitrate{111, 43, 15, 6};
constexpr QRect kSampleRate{156, 43, 10, 6};

// ------------------------------------------- volume.bmp (68x433)
//
// Vinte e oito fundos de 68x13, empilhados de 15 em 15: o fundo escolhido diz
// o NIVEL. Nao ha degrade de fundo — e a cor da faixa inteira que muda.

constexpr int kVolumeFrames = 28;
constexpr int kVolumeFrameStride = 15;
constexpr QSize kVolumeSize{68, 13};
constexpr Sprite kVolumeThumbNormal{"volume", {15, 422, 14, 11}};
constexpr Sprite kVolumeThumbPressed{"volume", {0, 422, 14, 11}};
constexpr QPoint kVolumeAt{107, 57};

constexpr int kBalanceFrames = 28;
constexpr QSize kBalanceSize{38, 13};
constexpr Sprite kBalanceThumbNormal{"balance", {15, 422, 14, 11}};
constexpr Sprite kBalanceThumbPressed{"balance", {0, 422, 14, 11}};
constexpr QPoint kBalanceAt{177, 57};

// --------------------------------------------- posbar.bmp (307x10)

constexpr Sprite kPositionBackground{"posbar", {0, 0, 248, 10}};
constexpr Sprite kPositionThumbNormal{"posbar", {248, 0, 29, 10}};
constexpr Sprite kPositionThumbPressed{"posbar", {278, 0, 29, 10}};
constexpr QPoint kPositionAt{16, 72};

// -------------------------------------------- shufrep.bmp (92x92)

constexpr Sprite kRepeatOff{"shufrep", {0, 0, 28, 15}};
constexpr Sprite kRepeatOffPressed{"shufrep", {0, 15, 28, 15}};
constexpr Sprite kRepeatOn{"shufrep", {0, 30, 28, 15}};
constexpr Sprite kRepeatOnPressed{"shufrep", {0, 45, 28, 15}};
constexpr Sprite kShuffleOff{"shufrep", {28, 0, 47, 15}};
constexpr Sprite kShuffleOffPressed{"shufrep", {28, 15, 47, 15}};
constexpr Sprite kShuffleOn{"shufrep", {28, 30, 47, 15}};
constexpr Sprite kShuffleOnPressed{"shufrep", {28, 45, 47, 15}};
constexpr Sprite kEqualizerOff{"shufrep", {0, 61, 23, 12}};
constexpr Sprite kEqualizerOn{"shufrep", {0, 73, 23, 12}};
constexpr Sprite kPlaylistOff{"shufrep", {23, 61, 23, 12}};
constexpr Sprite kPlaylistOn{"shufrep", {23, 73, 23, 12}};

constexpr QPoint kShuffleAt{164, 89};
constexpr QPoint kRepeatAt{210, 89};
constexpr QPoint kEqualizerAt{219, 58};
constexpr QPoint kPlaylistAt{242, 58};

// ------------------------------------------ monoster.bmp (56x24)

constexpr Sprite kStereoOn{"monoster", {29, 0, 29, 12}};
constexpr Sprite kStereoOff{"monoster", {29, 12, 29, 12}};
constexpr Sprite kMonoOn{"monoster", {0, 0, 29, 12}};
constexpr Sprite kMonoOff{"monoster", {0, 12, 29, 12}};
constexpr QPoint kMonoAt{212, 41};
constexpr QPoint kStereoAt{239, 41};

// ----------------------------------------- playpaus.bmp (42x9)

constexpr Sprite kIndicatorPlay{"playpaus", {0, 0, 9, 9}};
constexpr Sprite kIndicatorPause{"playpaus", {9, 0, 9, 9}};
constexpr Sprite kIndicatorStop{"playpaus", {18, 0, 9, 9}};
constexpr QPoint kIndicatorAt{26, 28};

// ---------------------------------------- visualizacao

constexpr QRect kVisualization{24, 43, 76, 16};

// --------------------------------------- eqmain.bmp (275x315)

constexpr Sprite kEqualizerBackground{"eqmain", {0, 0, 275, 116}};
constexpr Sprite kEqualizerThumbNormal{"eqmain", {0, 164, 11, 11}};
constexpr Sprite kEqualizerThumbPressed{"eqmain", {0, 176, 11, 11}};

// Vinte e oito fundos de slider, de 14x63, empilhados a partir de (13,164).
constexpr int kEqSliderFrames = 28;
constexpr QSize kEqSliderSize{14, 63};
constexpr QPoint kEqSliderOrigin{13, 164};

constexpr QPoint kPreampAt{21, 38};
constexpr QPoint kBandsOrigin{78, 38};
constexpr int kBandSpacing = 18;
constexpr QRect kEqCurve{87, 17, 113, 19};

constexpr QPoint kEqOnAt{14, 18};
constexpr QPoint kEqAutoAt{39, 18};
constexpr QPoint kEqPresetsAt{217, 18};

// Posicao de uma banda do equalizador, 0 a 9.
constexpr QPoint band_at(int band) {
    return QPoint(kBandsOrigin.x() + band * kBandSpacing, kBandsOrigin.y());
}

// Nome de arquivo de cada bitmap, sem extensao. O formato aceita .bmp e, em
// skins mais novos, .png — por isso o nome vem sem sufixo.
constexpr const char* kBitmaps[] = {
    "main",     "cbuttons", "titlebar", "numbers",  "text",     "volume",
    "balance",  "posbar",   "shufrep",  "monoster", "playpaus", "eqmain",
    "eq_ex",    "pledit",
};

}  // namespace pang::ui::skin::winamp
