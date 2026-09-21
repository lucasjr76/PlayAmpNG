// Testes do M2 — playlist, navegacao, shuffle, metadados e import/export.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "core/audio/engine.h"
#include "core/meta/scanner.h"
#include "core/meta/tags.h"
#include "core/playlist/m3u.h"
#include "core/playlist/playlist.h"
#include "core/playlist/shuffle.h"
#include "core/state/controller.h"
#include "core/util/check.h"

namespace fs = std::filesystem;
using namespace std::chrono_literals;
using namespace pang::core;

namespace {

constexpr int kRate = 44100;
constexpr int kChannels = 2;
constexpr std::uint32_t kBlock = 512;

std::string asset(const char* name) { return std::string(PANG_TEST_ASSETS) + "/" + name; }

fs::path temp_dir() {
    static const fs::path dir = [] {
        fs::path d = fs::temp_directory_path() / "playampng-tests";
        std::error_code ec;
        fs::remove_all(d, ec);
        fs::create_directories(d, ec);
        return d;
    }();
    return dir;
}

void write_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

Track make(std::string path, std::string artist, std::string album, std::int64_t duration_ms) {
    Track t;
    t.path = std::move(path);
    t.artist = std::move(artist);
    t.album = std::move(album);
    t.duration_ms = duration_ms;
    return t;
}

// Roda o engine como a interface faria: renderiza blocos e chama poll().
void run(Controller& controller, Engine& engine, double max_seconds) {
    std::vector<float> block(kBlock * kChannels);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(int(max_seconds * 1000));
    while (std::chrono::steady_clock::now() < deadline) {
        engine.render(block.data(), kBlock);
        controller.poll();
        std::this_thread::sleep_for(1ms);
    }
}

// Roda ate acontecer a PROXIMA troca de faixa, e para ali.
//
// Amostrar o estado depois de um tempo fixo nao serve para testar avanco de
// faixa: com faixas de 0,5 s, "rodar 1,5 s e olhar o indice" cai ora depois de
// uma troca, ora depois de duas. A geracao do engine sobe a cada carga, entao
// esperar por ela torna o teste deterministico.
bool run_until_next_load(Controller& controller, Engine& engine, double max_seconds) {
    const std::uint64_t start = engine.snapshot().generation;
    std::vector<float> block(kBlock * kChannels);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(int(max_seconds * 1000));
    while (std::chrono::steady_clock::now() < deadline) {
        engine.render(block.data(), kBlock);
        controller.poll();
        if (engine.snapshot().generation != start) return true;
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

// Espera o engine sair de Loading.
void settle(Engine& engine) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (engine.snapshot().state == State::Loading &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(2ms);
}

// ------------------------------------------------------------ LI-06..LI-11

void test_playlist_basics() {
    Playlist pl;
    pl.add("/musica/b.mp3");
    pl.add("/musica/a.mp3");
    pl.add("/musica/c.mp3");
    PANG_CHECK(pl.size() == 3, "tres itens inseridos");

    const std::uint64_t id_of_a = pl.at(1).id;
    PANG_CHECK(pl.index_of(id_of_a) == 1, "id resolve para o indice atual");

    pl.move(2, 0);  // LI-04
    PANG_CHECK(pl.at(0).path == "/musica/c.mp3", "move leva o item para a posicao pedida");
    PANG_CHECK(pl.index_of(id_of_a) == 2, "o id acompanha o item apos a reordenacao");

    pl.remove({0});  // LI-06
    PANG_CHECK(pl.size() == 2, "remocao tira exatamente um item");
    PANG_CHECK(pl.index_of(id_of_a) == 1, "id continua valido depois da remocao");

    pl.clear();  // LI-07
    PANG_CHECK(pl.empty(), "clear esvazia a lista");
    PANG_CHECK(pl.index_of(id_of_a) == -1, "id de item removido nao resolve mais");
}

void test_duration_and_title() {
    Playlist pl;
    pl.add("/x/um.mp3");
    pl.add("/x/dois.mp3");
    pl.add("/x/tres.mp3");
    pl.apply_metadata(pl.at(0).id, make("/x/um.mp3", "A", "Alb", 60000));
    pl.apply_metadata(pl.at(1).id, make("/x/dois.mp3", "B", "Alb", 30000));
    // O terceiro fica sem metadados: duracao desconhecida.

    int unknown = 0;
    const std::int64_t total = pl.known_duration_ms(&unknown);
    // LI-11 — somar so o que se conhece. Contar o desconhecido como zero faria
    // a interface exibir um total menor do que o real, sem avisar.
    PANG_CHECK(total == 90000, "total soma apenas as duracoes conhecidas");
    PANG_CHECK(unknown == 1, "itens de duracao desconhecida sao contados a parte");

    // MD-02 — sem tag de titulo, o nome do arquivo, sem diretorio e sem extensao.
    PANG_CHECK(pl.at(2).display_title() == "tres", "sem tags, o titulo e o nome do arquivo");
    // Artista sem titulo tambem cai no nome do arquivo: metade da tag nao
    // basta para montar um nome de exibicao.
    PANG_CHECK(pl.at(0).display_title() == "um",
               "com artista mas sem titulo, ainda usa o nome do arquivo");

    Track full = make("/x/um.mp3", "A", "Alb", 60000);
    full.title = "Faixa";
    pl.apply_metadata(pl.at(0).id, full);
    PANG_CHECK(pl.at(0).display_title() == "A - Faixa",
               "com artista e titulo, exibe \"artista - titulo\"");
}

void test_sort_and_find() {
    Playlist pl;
    pl.add("/z.mp3");
    pl.add("/a.mp3");
    pl.add("/m.mp3");
    pl.apply_metadata(pl.at(0).id, make("/z.mp3", "Zeta", "", 300));
    pl.apply_metadata(pl.at(1).id, make("/a.mp3", "", "", 100));  // sem artista
    pl.apply_metadata(pl.at(2).id, make("/m.mp3", "Mu", "", 200));

    pl.sort(SortKey::Path, true);
    PANG_CHECK(pl.at(0).path == "/a.mp3" && pl.at(2).path == "/z.mp3",
               "ordenacao por caminho, crescente");

    pl.sort(SortKey::Artist, true);
    // LI-08 — o item sem artista vai para o fim, nao para o comeco.
    PANG_CHECK(pl.at(2).artist.empty(), "campo ausente fica no fim da ordenacao");
    PANG_CHECK(pl.at(0).artist == "Mu" && pl.at(1).artist == "Zeta",
               "os conhecidos ficam em ordem entre si");

    pl.sort(SortKey::Duration, false);
    PANG_CHECK(pl.at(0).duration_ms == 300, "ordenacao decrescente por duracao");

    const auto hits = pl.find("mu");
    PANG_CHECK(hits.size() == 1, "busca textual encontra pelo artista, sem diferenciar caixa");
}

// ------------------------------------------------------------------- LI-03

void test_directory_scan() {
    const fs::path root = temp_dir() / "varredura";
    write_file(root / "a.mp3", "x");
    write_file(root / "b.txt", "x");  // nao e audio
    write_file(root / "sub" / "c.flac", "x");

    Playlist shallow;
    PANG_CHECK(shallow.add_directory(root.string(), false) == 1,
               "varredura nao recursiva pega so o nivel de cima");

    Playlist deep;
    const int found = deep.add_directory(root.string(), true);
    PANG_CHECK(found == 2, "varredura recursiva desce nos subdiretorios");
    PANG_CHECK(deep.at(0).path.find("a.mp3") != std::string::npos,
               "resultado vem em ordem determinista de caminho");

    Playlist missing;
    PANG_CHECK(missing.add_directory((root / "nao-existe").string(), true) == 0,
               "diretorio inexistente devolve zero sem lancar");
}

// ------------------------------------------------------------ LI-12, LI-13

void test_playlist_io() {
    const fs::path dir = temp_dir() / "listas";
    write_file(dir / "musicas" / "um.mp3", "x");
    write_file(dir / "musicas" / "dois.mp3", "x");

    // LI-13 — caminho relativo resolvido contra o diretorio DA PLAYLIST.
    write_file(dir / "relativa.m3u8",
               "#EXTM3U\n"
               "#EXTINF:123,Artista - Faixa Um\n"
               "musicas/um.mp3\n"
               "#EXTINF:-1,Faixa Sem Duracao\n"
               "musicas/dois.mp3\n");

    std::string error;
    const auto loaded = playlist_io::load((dir / "relativa.m3u8").string(), error);
    PANG_CHECK(loaded.size() == 2, "M3U8 com duas entradas foi lido");
    PANG_CHECK(error.empty(), "leitura sem erro");
    PANG_CHECK(fs::exists(loaded[0].path),
               "caminho relativo resolvido contra o diretorio da playlist, e o arquivo existe");
    PANG_CHECK(loaded[0].duration_ms == 123000, "EXTINF traz a duracao em segundos");
    PANG_CHECK(loaded[0].artist == "Artista" && loaded[0].title == "Faixa Um",
               "EXTINF separa artista e titulo");
    // PL-26 — -1 no EXTINF significa desconhecido, nao zero.
    PANG_CHECK(loaded[1].duration_ms == -1, "duracao -1 no EXTINF permanece desconhecida");

    // Round-trip M3U8
    const std::string out_m3u8 = (dir / "saida.m3u8").string();
    PANG_CHECK(playlist_io::save(out_m3u8, loaded, error), "exportacao M3U8");
    const auto back = playlist_io::load(out_m3u8, error);
    PANG_CHECK(back.size() == loaded.size(), "round-trip M3U8 preserva a quantidade");
    PANG_CHECK(back[0].path == loaded[0].path && back[1].path == loaded[1].path,
               "round-trip M3U8 preserva ordem e caminhos");
    PANG_CHECK(back[1].duration_ms == -1, "round-trip preserva duracao desconhecida");

    // Round-trip PLS
    const std::string out_pls = (dir / "saida.pls").string();
    PANG_CHECK(playlist_io::save(out_pls, loaded, error), "exportacao PLS");
    const auto pls = playlist_io::load(out_pls, error);
    PANG_CHECK(pls.size() == loaded.size(), "round-trip PLS preserva a quantidade");
    PANG_CHECK(pls[0].path == loaded[0].path && pls[1].path == loaded[1].path,
               "round-trip PLS preserva ordem e caminhos");

    // PLS com as chaves fora de ordem: o formato e indexado, nao sequencial.
    write_file(dir / "desordenada.pls",
               "[playlist]\n"
               "File2=musicas/dois.mp3\n"
               "Title1=Primeira\n"
               "File1=musicas/um.mp3\n"
               "NumberOfEntries=2\n");
    const auto unordered = playlist_io::load((dir / "desordenada.pls").string(), error);
    PANG_CHECK(unordered.size() == 2, "PLS com chaves fora de ordem e lido");
    PANG_CHECK(unordered[0].path.find("um.mp3") != std::string::npos,
               "PLS respeita o indice da chave, nao a ordem das linhas");

    const auto broken = playlist_io::load((dir / "nao-existe.m3u").string(), error);
    PANG_CHECK(broken.empty() && !error.empty(), "playlist ausente devolve erro, sem lancar");
}

// ------------------------------------------------------------ LI-14, LI-15

void test_shuffle_cycle_and_history() {
    constexpr int kCount = 10;
    ShuffleOrder order;
    order.reset(kCount, 0, 12345);

    PANG_CHECK(order.current() == 0, "o ciclo comeca na faixa atual, sem interromper nada");

    std::set<int> seen{order.current()};
    std::vector<int> sequence{order.current()};
    for (;;) {
        const int next = order.next();
        if (next < 0) break;
        PANG_CHECK(seen.insert(next).second, "nenhuma faixa se repete dentro do ciclo");
        sequence.push_back(next);
    }
    PANG_CHECK(static_cast<int>(seen.size()) == kCount,
               "o ciclo cobre todas as faixas antes de terminar");

    // LI-14 — voltar percorre o historico real, na ordem inversa da ida.
    bool history_ok = true;
    for (int i = static_cast<int>(sequence.size()) - 2; i >= 0; --i)
        history_ok = history_ok && order.previous() == sequence[static_cast<std::size_t>(i)];
    PANG_CHECK(history_ok, "anterior percorre o historico de navegacao, nao um sorteio novo");
    PANG_CHECK(order.previous() == -1, "no inicio do historico, anterior nao tem para onde ir");

    ShuffleOrder other;
    other.reset(kCount, 0, 999);
    order.reset(kCount, 0, 12345);
    bool differs = false;
    for (int i = 0; i < kCount - 1; ++i) differs = differs || other.next() != order.next();
    PANG_CHECK(differs, "sementes diferentes produzem ordens diferentes");
}

// ------------------------------------- PL-04, PL-05, PL-12, PL-24, PL-25

struct Fixture {
    Engine engine{kRate, kChannels};
    Controller controller{engine};

    explicit Fixture(int count) {
        for (int i = 0; i < count; ++i) controller.playlist().add(asset("tone.wav"));
        controller.playlist_changed();
    }
};

void test_navigation() {
    Fixture f(3);
    f.controller.play_index(0);
    settle(f.engine);
    PANG_CHECK(f.controller.current_index() == 0, "play_index carrega o indice pedido");

    f.controller.next();
    settle(f.engine);
    PANG_CHECK(f.controller.current_index() == 1, "proxima avanca uma posicao");

    // PL-04 — anterior vai a faixa anterior, nunca reinicia a atual.
    f.controller.previous();
    settle(f.engine);
    PANG_CHECK(f.controller.current_index() == 0, "anterior volta uma posicao");

    f.controller.previous();
    settle(f.engine);
    PANG_CHECK(f.controller.current_index() == 0,
               "na primeira faixa com repeat=off, anterior permanece nela");
}

void test_pause_then_change_track() {
    Fixture f(3);
    f.controller.play_index(0);
    settle(f.engine);

    f.controller.pause();
    PANG_CHECK(f.engine.snapshot().state == State::Paused, "pausado antes da troca");

    // PL-25 — trocar de faixa durante a pausa mantem pausado, na posicao 0.
    f.controller.next();
    settle(f.engine);
    const auto snap = f.engine.snapshot();
    PANG_CHECK(f.controller.current_index() == 1, "a troca aconteceu");
    PANG_CHECK(snap.state == State::Paused, "trocar de faixa durante a pausa continua pausado");
    PANG_CHECK(snap.position_frames == 0, "a nova faixa comeca na posicao 0");
}

void test_end_of_playlist() {
    // repeat=off: para na ultima faixa.
    {
        Fixture f(2);
        f.controller.set_repeat(Repeat::Off);
        f.controller.play_index(1);  // ultima
        settle(f.engine);
        // Aqui o correto e NAO haver proxima carga: o teste passa justamente
        // quando run_until_next_load estoura o prazo sem ver troca.
        PANG_CHECK(!run_until_next_load(f.controller, f.engine, 1.5),
                   "repeat=off nao carrega outra faixa no fim da playlist");
        const auto snap = f.engine.snapshot();
        PANG_CHECK(snap.state == State::Stopped, "fim da playlist com repeat=off para");
        PANG_CHECK(snap.position_frames == 0, "ao parar no fim, a posicao volta a 0");
        PANG_CHECK(f.controller.current_index() == 1, "a faixa continua sendo a ultima");
    }
    // repeat=all: volta para a primeira.
    {
        Fixture f(2);
        f.controller.set_repeat(Repeat::All);
        f.controller.play_index(1);  // ultima
        settle(f.engine);
        PANG_CHECK(run_until_next_load(f.controller, f.engine, 3.0),
                   "repeat=all carrega outra faixa no fim, em vez de parar");
        PANG_CHECK(f.controller.current_index() == 0,
                   "fim da playlist com repeat=all volta para a primeira");
    }
    // repeat=track: repete a mesma.
    {
        Fixture f(3);
        f.controller.set_repeat(Repeat::Track);
        f.controller.play_index(1);
        settle(f.engine);
        const std::uint64_t first = f.engine.snapshot().generation;

        PANG_CHECK(run_until_next_load(f.controller, f.engine, 3.0),
                   "repeat=track recarrega a faixa no fim");
        PANG_CHECK(f.controller.current_index() == 1,
                   "repeat=track recomeca a mesma faixa, sem avancar");

        run(f.controller, f.engine, 1.5);
        // Contar recargas em vez de amostrar o estado: com faixa de 0,5 s em
        // 1,5 s de execucao, checar o estado no fim cairia ora em Playing, ora
        // em Loading, ora no instante exato do fim. A geracao e monotonica e
        // nao depende de onde o relogio parou.
        const std::uint64_t reloads = f.engine.snapshot().generation - first;
        PANG_CHECK(reloads >= 2, "repeat=track recarregou a faixa varias vezes em 1,5 s");
    }
}

// -------------------------------------------------------------------- AR-03

void test_metadata_scanner() {
    Playlist pl;
    const int index = pl.add(asset("tone.mp3"));
    const std::uint64_t id = pl.at(index).id;

    std::atomic<int> received{0};
    std::vector<meta::Scanner::Result> results;
    std::mutex results_mutex;

    meta::Scanner scanner([&](const meta::Scanner::Result& r) {
        std::lock_guard<std::mutex> lock(results_mutex);
        results.push_back(r);
        received.fetch_add(1);
    });

    scanner.enqueue(id, pl.at(index).path);
    scanner.wait_idle();
    PANG_CHECK(received.load() == 1, "o scanner devolve o resultado da leitura");

    {
        std::lock_guard<std::mutex> lock(results_mutex);
        PANG_CHECK(results[0].id == id, "o resultado carrega o id de origem");
        PANG_CHECK(results[0].track.duration_ms > 400 && results[0].track.duration_ms < 600,
                   "duracao lida das tags bate com o arquivo de 0,5 s");
        pl.apply_metadata(results[0].id, results[0].track);
    }
    PANG_CHECK(pl.at(0).metadata_loaded, "metadados aplicados ao item certo");

    // AR-03 — resultado de um item que ja saiu da lista nao pode escrever na
    // faixa que ocupou aquele indice.
    Playlist other;
    other.add("/a.mp3");
    const std::uint64_t vanished = 999999;
    Track intruder = make("/invasor.mp3", "Invasor", "X", 12345);
    other.apply_metadata(vanished, intruder);
    PANG_CHECK(other.at(0).artist.empty(),
               "resultado com id inexistente e descartado, nao aplicado a outro item");
}

// ------------------------------------------------------------ LI-17, RB-06

void test_large_playlist() {
    constexpr int kCount = 10000;
    Playlist pl;

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kCount; ++i)
        pl.add("/musica/album" + std::to_string(i % 100) + "/faixa" + std::to_string(i) + ".mp3");
    const auto add_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
            .count();

    start = std::chrono::steady_clock::now();
    pl.sort(SortKey::Path, true);
    const auto sort_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
            .count();

    start = std::chrono::steady_clock::now();
    const auto hits = pl.find("faixa9999");
    const auto find_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
            .count();

    // Aplicar metadados a todos exercita index_of(), que hoje e linear.
    start = std::chrono::steady_clock::now();
    for (int i = 0; i < kCount; ++i) {
        Track t = pl.at(i);
        t.duration_ms = 1000;
        pl.apply_metadata(t.id, t);
    }
    const auto apply_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
            .count();

    std::printf("  10k itens: inserir %lld ms · ordenar %lld ms · buscar %lld ms · "
                "aplicar metadados %lld ms\n",
                (long long)add_ms, (long long)sort_ms, (long long)find_ms, (long long)apply_ms);

    PANG_CHECK(pl.size() == kCount, "10 mil itens inseridos");
    PANG_CHECK(!hits.empty(), "busca encontra em lista grande");
    PANG_CHECK(sort_ms < 200, "ordenar 10 mil itens fica abaixo do limite de 200 ms");
    PANG_CHECK(find_ms < 200, "buscar em 10 mil itens fica abaixo do limite de 200 ms");
    PANG_CHECK(pl.known_duration_ms() == static_cast<std::int64_t>(kCount) * 1000,
               "duracao total consistente apos aplicar metadados em todos");
}

// ------------------------------------------------------------ RB-02, RB-03

void test_broken_files() {
    const fs::path dir = temp_dir() / "quebrados";
    write_file(dir / "corrompido.mp3", std::string(4096, '\x01'));

    Engine engine(kRate, kChannels);
    engine.load((dir / "corrompido.mp3").string(), State::Playing);
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (engine.snapshot().state == State::Loading &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(2ms);

    const State s = engine.snapshot().state;
    PANG_CHECK(s == State::Error || s == State::Stopped,
               "arquivo corrompido termina em Error ou Stopped, sem travar nem tocar lixo");

    // Tags de arquivo ilegivel nao podem lancar.
    const Track t = meta::read_tags((dir / "corrompido.mp3").string());
    PANG_CHECK(t.duration_ms == -1, "arquivo corrompido nao produz duracao inventada");
    PANG_CHECK(meta::read_tags("/nao/existe.mp3").title.empty(),
               "arquivo ausente devolve tags vazias, sem lancar");
}

// LI-04 com LI-05 — reordenar uma SELECAO, que nao e repetir o move de um.
void test_move_selection() {
    const auto montar = [] {
        Playlist pl;
        for (const char* nome : {"a", "b", "c", "d", "e"})
            pl.add(std::string("/musica/") + nome + ".mp3");
        return pl;
    };
    const auto ordem = [](const Playlist& pl) {
        std::string s;
        for (int i = 0; i < pl.size(); ++i) s += pl.at(i).path.substr(8, 1);
        return s;
    };

    {   // Para cima: o destino nao muda, porque nada selecionado estava antes.
        Playlist pl = montar();
        pl.move(std::vector<int>{3, 4}, 1);
        PANG_CHECK(ordem(pl) == "adebc", "d e e vao para antes de b, nessa ordem");
    }

    {   // Para BAIXO: o caso que erra sem o desconto de indices.
        //
        // Tirar a e b da lista faz tudo andar duas casas para tras. Sem
        // descontar, o destino 4 apontaria para depois de e, e o erro cresce
        // com o tamanho da selecao — que e o que torna o defeito confuso na
        // tela: com um item arrastado, quase acerta; com cinco, vai longe.
        Playlist pl = montar();
        pl.move(std::vector<int>{0, 1}, 4);
        PANG_CHECK(ordem(pl) == "cdabe", "a e b entram antes de e, e nao no fim");
    }

    {   // Selecao descontinua mantem a ordem relativa, e nao a de clique.
        Playlist pl = montar();
        pl.move(std::vector<int>{4, 0}, 2);
        PANG_CHECK(ordem(pl) == "baecd", "a antes de e, como estavam na lista");
    }

    {   // Soltar dentro da propria selecao nao pode embaralhar nada.
        Playlist pl = montar();
        pl.move(std::vector<int>{1, 2}, 2);
        PANG_CHECK(ordem(pl) == "abcde", "mover para onde ja estava nao muda a lista");
    }

    {   // Entradas invalidas nao podem corromper a lista nem derrubar o player.
        Playlist pl = montar();
        pl.move(std::vector<int>{-1, 99}, 2);
        PANG_CHECK(ordem(pl) == "abcde", "indices fora da lista sao ignorados");
        pl.move(std::vector<int>{1, 1, 1}, 0);
        PANG_CHECK(ordem(pl) == "bacde", "indice repetido conta uma vez so");
        pl.move(std::vector<int>{}, 0);
        PANG_CHECK(ordem(pl) == "bacde", "selecao vazia nao faz nada");
        pl.move(std::vector<int>{0}, 999);
        PANG_CHECK(ordem(pl) == "acdeb", "destino alem do fim vai para o fim");
    }

    {   // LI-04 com a identidade estavel: reordenar nao pode fazer o player
        //  perder a faixa que esta tocando.
        Playlist pl = montar();
        const std::uint64_t id = pl.at(0).id;
        pl.move(std::vector<int>{0}, 5);
        PANG_CHECK(pl.index_of(id) == 4, "o id acompanha o item para a nova posicao");
    }
}

}  // namespace

int main() {
    test_move_selection();
    test_playlist_basics();
    test_duration_and_title();
    test_sort_and_find();
    test_directory_scan();
    test_playlist_io();
    test_shuffle_cycle_and_history();
    test_navigation();
    test_pause_then_change_track();
    test_end_of_playlist();
    test_metadata_scanner();
    test_large_playlist();
    test_broken_files();
    return pang::check::exit_code();
}
