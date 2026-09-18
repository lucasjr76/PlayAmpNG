#pragma once

#include <string>

#include "core/playlist/track.h"

namespace pang::core::meta {

// MD-01 — le titulo, artista, album, numero da faixa, genero, ano e duracao.
//
// Operacao de disco: NUNCA chamar do thread de audio nem do thread de
// interface. Use Scanner, que a executa em segundo plano (LI-16).
//
// Campos ausentes na fonte permanecem vazios ou negativos. A funcao nao inventa
// titulo a partir do caminho: quem decide isso e Track::display_title(), na
// hora de exibir, para que a ausencia continue distinguivel.
Track read_tags(const std::string& path);

}  // namespace pang::core::meta
