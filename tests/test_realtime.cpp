// AU-21 — o callback de audio nao pode alocar.
//
// Binario separado dos demais testes porque ele substitui operator new
// globalmente: misturar isso com testes que alocam a vontade tornaria a
// contagem inutil.
//
// Limite conhecido: a interposicao pega operator new, nao malloc cru. Serve
// porque render() so executa codigo nosso — o libav fica no thread de
// decodificacao, do outro lado do ring.

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "core/audio/engine.h"
#include "core/util/check.h"

namespace {
std::atomic<bool> g_watching{false};
std::atomic<int> g_allocations{0};
}  // namespace

// O compilador avisa de "mismatched new/delete" nestes substitutos, e e falso
// positivo: ele enxerga o std::free ao inlinar um destrutor da biblioteca
// padrao, mas nao enxerga que o operator new correspondente TAMBEM foi
// substituido logo acima e tambem usa malloc. Silenciado aqui, e so aqui, com
// a razao anotada — desligar o aviso no projeto inteiro esconderia o caso em
// que ele estivesse certo.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

void* operator new(std::size_t size) {
    if (g_watching.load(std::memory_order_relaxed)) g_allocations.fetch_add(1);
    void* pointer = std::malloc(size ? size : 1);
    if (!pointer) throw std::bad_alloc();
    return pointer;
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

using namespace std::chrono_literals;
using namespace pang::core;

int main() {
    constexpr int kRate = 44100;
    constexpr int kChannels = 2;
    constexpr std::uint32_t kBlock = 512;

    Engine engine(kRate, kChannels);
    engine.load(std::string(PANG_TEST_ASSETS) + "/tone.wav", State::Playing);

    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (engine.snapshot().state == State::Loading &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(2ms);

    std::vector<float> block(kBlock * kChannels);

    // Exercita tambem os caminhos que mexem em parametros durante a reproducao:
    // e neles que uma alocacao descuidada costuma se esconder.
    engine.equalizer().set_band_db(3, 9.0f);
    engine.equalizer().set_preamp_db(-3.0f);
    engine.set_volume(0.7f);
    engine.set_balance(-0.4f);

    g_allocations.store(0);
    g_watching.store(true);
    for (int i = 0; i < 200; ++i) {
        engine.render(block.data(), kBlock);
        if (i == 80) engine.equalizer().set_bypass(true);
        if (i == 140) engine.equalizer().set_bypass(false);
        if (i == 100) engine.set_volume(0.2f);
    }
    g_watching.store(false);

    const int allocations = g_allocations.load();
    std::printf("  alocacoes em 200 chamadas de render(): %d\n", allocations);
    PANG_CHECK(allocations == 0,
               "AU-21: render() nao aloca, nem durante mudanca de parametros e bypass");

    return pang::check::exit_code();
}
