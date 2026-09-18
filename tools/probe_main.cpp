// pang_probe — ferramenta headless do M0.
//
// Existe por dois motivos concretos:
//   1. Prova que core/ e platform/ compilam, ligam e rodam sem Qt e sem janela,
//      que e a premissa de toda a estrategia de testes (ARCHITECTURE.md 11).
//   2. Resolve no M0 o risco que so apareceria no M1: se FFmpeg e miniaudio
//      tiverem alguma surpresa de integracao, ela aparece aqui.
//
// Uso: pang_probe [arquivo-ou-url ...]

#include <cstdio>
#include <string>

#include "core/audio/probe.h"
#include "platform/audio_device.h"

namespace {

void print_environment() {
    std::printf("FFmpeg   %s  (licenca: %s)\n", pang::core::ffmpeg_version().c_str(),
                pang::core::ffmpeg_license().c_str());
    std::printf("Backend  %s\n\n", pang::platform::backend_name().c_str());

    std::string error;
    const auto devices = pang::platform::list_playback_devices(error);
    if (!error.empty()) {
        std::printf("dispositivos de saida: erro — %s\n", error.c_str());
        return;
    }
    std::printf("dispositivos de saida (%zu):\n", devices.size());
    for (const auto& d : devices)
        std::printf("  %s %s\n", d.is_default ? "*" : " ", d.name.c_str());
}

// Campo ausente imprime "-", nunca um numero plausivel (PL-26).
void print_probe(const std::string& url) {
    std::string error;
    const auto r = pang::core::probe(url, error);
    std::printf("\n%s\n", url.c_str());
    if (!r) {
        std::printf("  erro: %s\n", error.c_str());
        return;
    }
    std::printf("  formato    %s\n", r->format.c_str());
    std::printf("  codec      %s\n", r->codec.c_str());
    std::printf("  taxa       %d Hz\n", r->sample_rate);
    std::printf("  canais     %d (%s)\n", r->channels, r->channels == 1 ? "mono" : "estereo");
    if (r->duration_us)
        std::printf("  duracao    %.3f s\n", static_cast<double>(*r->duration_us) / 1e6);
    else
        std::printf("  duracao    -  (fonte nao informa)\n");
    if (r->bitrate_bps)
        std::printf("  bitrate    %lld bps\n", static_cast<long long>(*r->bitrate_bps));
    else
        std::printf("  bitrate    -  (fonte nao informa)\n");
    std::printf("  busca      %s\n", r->seekable ? "suportada" : "nao suportada");
}

}  // namespace

int main(int argc, char** argv) {
    print_environment();
    for (int i = 1; i < argc; ++i) print_probe(argv[i]);
    return 0;
}
