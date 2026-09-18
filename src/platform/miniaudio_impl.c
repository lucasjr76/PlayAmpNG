/* Unica unidade de traducao com a implementacao do miniaudio.
 *
 * Decodificacao e reamostragem sao do FFmpeg; o miniaudio cuida apenas de
 * dispositivo e saida. Desligar os modulos abaixo deixa essa divisao explicita
 * e encolhe o binario. */
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"
