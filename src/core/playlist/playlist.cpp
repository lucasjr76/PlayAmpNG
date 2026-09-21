#include "core/playlist/playlist.h"

#include <iterator>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace pang::core {
namespace {

namespace fs = std::filesystem;

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Comparacao que empurra o vazio/desconhecido para o fim, em qualquer direcao.
// Ordenar por album com metade dos itens sem tag nao deve intercalar os vazios
// no meio da lista.
template <typename T>
bool less_with_unknown_last(const T& a, const T& b, bool a_known, bool b_known, bool ascending) {
    if (a_known != b_known) return a_known;  // conhecido sempre antes
    if (!a_known) return false;              // ambos desconhecidos: estavel
    return ascending ? a < b : b < a;
}

}  // namespace

std::string Track::display_title() const {
    if (!title.empty()) {
        if (!artist.empty()) return artist + " - " + title;
        return title;
    }
    // MD-02 — sem tags, o nome do arquivo, sem diretorio e sem extensao.
    const std::filesystem::path p(path);
    const std::string stem = p.stem().string();
    return stem.empty() ? path : stem;
}

bool is_supported_audio(std::string_view path) {
    static const char* kExtensions[] = {".mp3", ".wav",  ".flac", ".ogg", ".oga",
                                        ".opus", ".m4a", ".aac",  ".mp4", ".wma",
                                        ".aiff", ".aif", ".ape",  ".wv",  ".mpc"};
    const std::string lowered = lower(path);
    for (const char* ext : kExtensions)
        if (lowered.size() > std::char_traits<char>::length(ext) &&
            lowered.compare(lowered.size() - std::char_traits<char>::length(ext),
                            std::char_traits<char>::length(ext), ext) == 0)
            return true;
    return false;
}

int Playlist::add(std::string path) {
    Track t;
    t.id = next_id_++;
    t.path = std::move(path);
    tracks_.push_back(std::move(t));
    return static_cast<int>(tracks_.size()) - 1;
}

int Playlist::add_directory(const std::string& directory, bool recursive) {
    std::error_code ec;
    std::vector<std::string> found;

    auto consider = [&](const fs::directory_entry& entry) {
        if (!entry.is_regular_file(ec)) return;
        const std::string p = entry.path().string();
        if (is_supported_audio(p)) found.push_back(p);
    };

    if (recursive) {
        // A variante com error_code nao lanca em diretorio sem permissao: o
        // item problematico e pulado e a varredura continua (RB-03).
        fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied,
                                            ec);
        if (ec) return 0;
        for (const auto& entry : it) consider(entry);
    } else {
        fs::directory_iterator it(directory, fs::directory_options::skip_permission_denied, ec);
        if (ec) return 0;
        for (const auto& entry : it) consider(entry);
    }

    // Ordem determinista: sem isso a playlist depende da ordem do sistema de
    // arquivos, que muda entre maquinas e quebra teste.
    std::sort(found.begin(), found.end());
    for (std::string& p : found) add(std::move(p));
    return static_cast<int>(found.size());
}

void Playlist::remove(std::vector<int> indices) {
    std::sort(indices.begin(), indices.end(), std::greater<int>());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    for (int i : indices)
        if (i >= 0 && i < size()) tracks_.erase(tracks_.begin() + i);
}

void Playlist::clear() { tracks_.clear(); }

void Playlist::move(int from, int to) {
    if (from < 0 || from >= size() || to < 0 || to >= size() || from == to) return;
    Track moved = std::move(tracks_[static_cast<std::size_t>(from)]);
    tracks_.erase(tracks_.begin() + from);
    tracks_.insert(tracks_.begin() + to, std::move(moved));
}

int Playlist::move(std::vector<int> indices, int before) {
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    indices.erase(std::remove_if(indices.begin(), indices.end(),
                                 [this](int i) { return i < 0 || i >= size(); }),
                  indices.end());
    if (indices.empty()) return -1;
    before = std::clamp(before, 0, size());

    // O destino muda quando ha selecionados ANTES dele: ao tira-los da lista,
    // tudo o que vinha depois anda para tras. Sem este desconto, arrastar para
    // baixo erra por exatamente a quantidade de itens arrastados — e o erro
    // cresce com a selecao, que e o que torna o defeito confuso na tela.
    int destination = before;
    for (int index : indices)
        if (index < before) --destination;

    // Rede de seguranca, e nao correcao do calculo acima: um destino fora do
    // intervalo aqui seria comportamento indefinido no insert, e um teste que
    // reprova por SEGFAULT diz muito menos do que um que reprova mostrando a
    // ordem errada.
    destination = std::clamp(destination, 0, size() - static_cast<int>(indices.size()));

    std::vector<Track> moved;
    moved.reserve(indices.size());
    for (int index : indices) moved.push_back(std::move(tracks_[static_cast<std::size_t>(index)]));

    // De tras para frente: apagar da frente invalidaria os indices seguintes.
    for (auto it = indices.rbegin(); it != indices.rend(); ++it)
        tracks_.erase(tracks_.begin() + *it);

    tracks_.insert(tracks_.begin() + destination, std::make_move_iterator(moved.begin()),
                   std::make_move_iterator(moved.end()));
    return destination;
}

void Playlist::sort(SortKey key, bool ascending) {
    auto compare = [key, ascending](const Track& a, const Track& b) {
        switch (key) {
            case SortKey::Title:
                return less_with_unknown_last(lower(a.display_title()), lower(b.display_title()),
                                              true, true, ascending);
            case SortKey::Artist:
                return less_with_unknown_last(lower(a.artist), lower(b.artist), !a.artist.empty(),
                                              !b.artist.empty(), ascending);
            case SortKey::Album:
                return less_with_unknown_last(lower(a.album), lower(b.album), !a.album.empty(),
                                              !b.album.empty(), ascending);
            case SortKey::Duration:
                return less_with_unknown_last(a.duration_ms, b.duration_ms, a.duration_ms >= 0,
                                              b.duration_ms >= 0, ascending);
            case SortKey::Path:
                return less_with_unknown_last(lower(a.path), lower(b.path), true, true, ascending);
        }
        return false;
    };
    // Estavel: itens equivalentes mantem a ordem anterior, entao ordenar duas
    // vezes pelo mesmo criterio nao embaralha nada.
    std::stable_sort(tracks_.begin(), tracks_.end(), compare);
}

std::vector<int> Playlist::find(std::string_view query) const {
    std::vector<int> hits;
    if (query.empty()) return hits;
    const std::string needle = lower(query);

    for (int i = 0; i < size(); ++i) {
        const Track& t = tracks_[static_cast<std::size_t>(i)];
        const std::string haystack =
            lower(t.title + '\n' + t.artist + '\n' + t.album + '\n' + t.path);
        if (haystack.find(needle) != std::string::npos) hits.push_back(i);
    }
    return hits;
}

std::int64_t Playlist::known_duration_ms(int* unknown_count) const {
    std::int64_t total = 0;
    int unknown = 0;
    for (const Track& t : tracks_) {
        if (t.duration_ms >= 0)
            total += t.duration_ms;
        else
            ++unknown;
    }
    if (unknown_count) *unknown_count = unknown;
    return total;
}

void Playlist::apply_metadata(std::uint64_t id, const Track& metadata) {
    const int index = index_of(id);
    if (index < 0) return;  // AR-03 — o item saiu da lista enquanto lia as tags

    Track& t = tracks_[static_cast<std::size_t>(index)];
    const std::uint64_t keep_id = t.id;
    const std::string keep_path = t.path;
    t = metadata;
    t.id = keep_id;
    t.path = keep_path;
    t.metadata_loaded = true;
}

// ponytail: busca linear. Com 10 mil itens, aplicar metadados de todos custa
// O(n^2). LI-17 mede isso; se aparecer, trocar por um mapa id->indice
// reconstruido sob demanda com flag de sujeira.
int Playlist::index_of(std::uint64_t id) const {
    for (int i = 0; i < size(); ++i)
        if (tracks_[static_cast<std::size_t>(i)].id == id) return i;
    return -1;
}

}  // namespace pang::core
