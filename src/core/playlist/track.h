#pragma once

#include <cstdint>
#include <string>

namespace pang::core {

// Item da playlist.
//
// Campos desconhecidos ficam vazios ou negativos e assim chegam a interface.
// PL-26 e LI-11 proibem substituir ausencia por valor plausivel: uma duracao
// desconhecida nao entra na soma total, e um ano ausente nao vira zero exibido.
struct Track {
    // Identidade estavel ao longo de ordenacoes e remocoes. Atribuido pela
    // Playlist; 0 significa "item solto", ainda nao inserido.
    std::uint64_t id = 0;

    std::string path;  // caminho local ou URL

    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    int track_number = 0;             // 0 = ausente
    int year = 0;                     // 0 = ausente
    std::int64_t duration_ms = -1;    // -1 = desconhecida

    // false enquanto a leitura assincrona de tags nao terminou.
    bool metadata_loaded = false;

    // MD-02 — sem tags, o nome do arquivo e o titulo.
    std::string display_title() const;
};

enum class SortKey { Title, Artist, Album, Duration, Path };

}  // namespace pang::core
