#pragma once

#include <string>
#include <vector>

#include "core/playlist/track.h"

namespace pang::core::playlist_io {

enum class Format { M3U, M3U8, PLS };

// Deduz pelo sufixo do nome. Desconhecido vira M3U8, que e o unico dos tres
// que nao perde acentuacao.
Format format_for(const std::string& path);

// LI-12, LI-13 — importa M3U, M3U8 e PLS.
//
// Caminhos relativos sao resolvidos contra o diretorio do arquivo de playlist,
// nao contra o diretorio de trabalho do processo. Esse detalhe e o que faz uma
// playlist gravada em um pendrive continuar funcionando em outra maquina.
std::vector<Track> load(const std::string& path, std::string& error);

// Grava caminhos absolutos: uma playlist exportada pode ser aberta de qualquer
// lugar, e o requisito de caminho relativo (LI-13) e sobre leitura.
bool save(const std::string& path, const std::vector<Track>& tracks, std::string& error);

}  // namespace pang::core::playlist_io
