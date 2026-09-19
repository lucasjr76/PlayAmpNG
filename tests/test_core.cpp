// Testes do M0. Rodam sem Qt, sem janela e sem dispositivo de audio, nos tres
// sistemas alvo.

#include <cstdio>
#include <string>

#include "core/audio/probe.h"
#include "core/util/check.h"
#include "platform/audio_device.h"

namespace {

// AR-10 — o guarda de regressao do proprio mecanismo de verificacao.
//
// Se alguem trocar PANG_CHECK por assert(), este teste passa a nao avaliar a
// condicao em RelWithDebInfo e `evaluated` fica em zero, reprovando aqui.
void test_check_survives_ndebug() {
    int evaluated = 0;
    PANG_CHECK(
        [&] {
            ++evaluated;
            return true;
        }(),
        "PANG_CHECK avalia a condicao");
    PANG_CHECK(evaluated == 1,
               "a condicao foi avaliada exatamente uma vez, mesmo com NDEBUG definido");
}

void test_probe_missing_file() {
    std::string error;
    const auto r = pang::core::probe("/caminho/que/nao/existe.flac", error);
    PANG_CHECK(!r.has_value(), "arquivo ausente nao devolve resultado");
    PANG_CHECK(!error.empty(), "arquivo ausente devolve mensagem de erro");
}

void test_probe_empty_url() {
    std::string error;
    const auto r = pang::core::probe("", error);
    PANG_CHECK(!r.has_value(), "url vazia nao devolve resultado");
    PANG_CHECK(!error.empty(), "url vazia devolve mensagem de erro");
}

void test_ffmpeg_present() {
    PANG_CHECK(!pang::core::ffmpeg_version().empty(), "versao do FFmpeg disponivel");
    // A licenca decide a licenca do binario distribuido; ver DEPENDENCIES.md.
    PANG_CHECK(!pang::core::ffmpeg_license().empty(), "licenca do FFmpeg disponivel");
}

// Nao exige dispositivos: runner de CI costuma nao ter saida de audio. Exige
// que a enumeracao termine sem travar e sem estado inconsistente.
void test_device_enumeration() {
    std::string error;
    const auto devices = pang::platform::list_playback_devices(error);
    PANG_CHECK(error.empty() || devices.empty(),
               "enumeracao devolve lista OU erro, nunca os dois");
    PANG_CHECK(!pang::platform::backend_name().empty(), "backend de audio identificado");
}

}  // namespace

// Cada etapa se anuncia antes de correr. Custa cinco linhas e responde a
// pergunta "onde travou?" sem depender de depurador na maquina de integracao.
#define RUN(f)                   \
    do {                         \
        std::printf("-> %s\n", #f); \
        f();                     \
    } while (0)

int main() {
    RUN(test_check_survives_ndebug);
    RUN(test_probe_missing_file);
    RUN(test_probe_empty_url);
    RUN(test_ffmpeg_present);
    RUN(test_device_enumeration);
    return pang::check::exit_code();
}
