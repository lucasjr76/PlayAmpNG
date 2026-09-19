// M6-1 — fonte de rede.
//
// Sobe um servidor HTTP local de verdade em vez de simular: o que se quer
// verificar sao as decisoes que o libavformat toma diante de um servidor real
// — cabecalho ICY, redirecionamento, conexao cortada no meio. Um mock do lado
// de ca nao exercitaria nada disso.
//
// O servidor e um script Python lancado pelo proprio teste, para que a suite
// continue sendo uma so ordem e nao exija preparo externo.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "core/audio/decoder.h"
#include "core/audio/engine.h"
#include "core/audio/network.h"
#include "core/audio/probe.h"
#include "core/util/check.h"

namespace {

using namespace pang::core;

// Espera ate que `pred` seja verdadeiro ou o prazo acabe. Devolve se ocorreu.
//
// Amostrar estado num instante arbitrario ja produziu passe acidental neste
// projeto; o certo e esperar pelo evento observavel e falhar por prazo.
template <typename Pred>
bool wait_for(Pred pred, std::chrono::milliseconds limit) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

void protocol_whitelist() {
    PANG_CHECK(is_remote("http://exemplo/radio"), "http e remoto");
    PANG_CHECK(is_remote("HTTPS://exemplo/radio"), "esquema e insensivel a caixa");
    PANG_CHECK(!is_remote("/musica/faixa.mp3"), "caminho absoluto nao e remoto");
    PANG_CHECK(!is_remote("file:///musica/faixa.mp3"), "file:// nao e remoto");

    // A lista precisa recusar os protocolos que permitem alcancar o disco por
    // caminho indireto. Uma entrada de M3U e texto que o usuario nao escreveu.
    const std::string list = kProtocolWhitelist;
    for (const char* proibido : {"concat", "subfile", "ffrtmpcrypt", "pipe", "fd"})
        PANG_CHECK(list.find(proibido) == std::string::npos,
                   (std::string("protocolo ") + proibido + " fora da lista").c_str());
}

void local_file_is_not_live() {
    std::string error;
    const auto info = probe(PANG_TEST_ASSETS "/tone.wav", error);
    PANG_CHECK(info.has_value(), "arquivo local abre");
    if (!info) return;
    PANG_CHECK(!info->live, "arquivo local nao e fonte ao vivo");
    PANG_CHECK(info->seekable, "arquivo local e pesquisavel");
    PANG_CHECK(info->duration_us.has_value(), "arquivo local informa duracao");
    PANG_CHECK(!info->station.has_value(), "arquivo local nao tem estacao");
}

void served_over_http(int port) {
    const std::string base = "http://127.0.0.1:" + std::to_string(port);

    // Arquivo servido por HTTP com Content-Length: tem duracao e e pesquisavel.
    // Nao e stream ao vivo so por vir da rede.
    {
        std::string error;
        const auto info = probe(base + "/tone.wav", error);
        PANG_CHECK(info.has_value(), ("arquivo por HTTP abre: " + error).c_str());
        if (info) {
            PANG_CHECK(!info->live, "arquivo com tamanho conhecido nao e ao vivo");
            PANG_CHECK(info->duration_us.has_value(), "arquivo por HTTP informa duracao");
        }
    }

    // MD-06 — redirecionamento e seguido.
    {
        std::string error;
        const auto info = probe(base + "/redir", error);
        PANG_CHECK(info.has_value(), ("redirecionamento e seguido: " + error).c_str());
    }

    // MD-09 — stream sem tamanho: nunca exibido como duracao finita.
    // MD-05 — o nome da estacao vem do cabecalho icy-name.
    {
        std::string error;
        const auto info = probe(base + "/stream", error);
        PANG_CHECK(info.has_value(), ("stream abre: " + error).c_str());
        if (info) {
            PANG_CHECK(!info->duration_us.has_value(), "stream nao informa duracao");
            PANG_CHECK(info->live, "stream e marcado como ao vivo");
            PANG_CHECK(!info->seekable, "stream nao e pesquisavel");
            PANG_CHECK(info->station.has_value() && *info->station == "Radio de Teste",
                       "nome da estacao lido do cabecalho ICY");
        }
    }

    // MD-08 — busca temporal recusada na fonte que nao a suporta. A interface
    // ja esconde o cursor, mas a recusa precisa existir tambem no nivel de
    // baixo: um atalho de teclado nao passa pela interface.
    {
        Decoder decoder;
        std::string error;
        PANG_CHECK(decoder.open(base + "/stream", 44100, 2, error),
                   ("stream abre no decodificador: " + error).c_str());
        if (decoder.is_open()) {
            PANG_CHECK(!decoder.info().seekable, "stream nao se declara pesquisavel");
            PANG_CHECK(!decoder.seek(1.0), "busca recusada em fonte nao pesquisavel");
            PANG_CHECK(decoder.duration_frames() < 0, "stream nao informa duracao em quadros");
        }
    }

    // RB-04 — conexao cortada no meio: o player tenta reconectar e, como o
    // servidor recusa dai em diante, termina em Error explicito, nunca parado
    // em silencio nem travado.
    {
        Engine engine(44100, 2);
        engine.load(base + "/corta", State::Playing);
        PANG_CHECK(wait_for([&] { return engine.live(); }, std::chrono::seconds(5)),
                   "stream reconhecido como ao vivo");

        std::vector<float> block(512 * 2);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        bool saw_error = false;
        while (std::chrono::steady_clock::now() < deadline && !saw_error) {
            engine.render(block.data(), 512);
            saw_error = engine.snapshot().state == State::Error;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        PANG_CHECK(saw_error, "stream cortado termina em Error apos esgotar as tentativas");
        PANG_CHECK(!engine.last_error().empty(), "o erro traz mensagem de diagnostico");
        engine.stop();
    }
}

}  // namespace

int main(int argc, char** argv) {
    protocol_whitelist();
    local_file_is_not_live();

    if (argc > 1)
        served_over_http(std::atoi(argv[1]));
    else
        std::printf("  sem porta: a parte de rede foi pulada\n");

    return pang::check::exit_code();
}
