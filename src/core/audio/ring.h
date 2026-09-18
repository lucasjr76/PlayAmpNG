#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

namespace pang::core {

// Buffer circular SPSC lock-free de float.
//
// Um produtor (thread de decodificacao) e um consumidor (thread de audio).
// Nenhuma alocacao depois da construcao: o vetor e dimensionado uma vez.
//
// A capacidade util e `capacity`, e o armazenamento tem uma posicao a mais,
// para distinguir cheio de vazio sem um contador extra.
class FloatRing {
public:
    explicit FloatRing(std::size_t capacity) : buf_(capacity + 1) {}

    std::size_t capacity() const noexcept { return buf_.size() - 1; }

    std::size_t readable() const noexcept {
        const std::size_t w = write_.load(std::memory_order_acquire);
        const std::size_t r = read_.load(std::memory_order_acquire);
        return w >= r ? w - r : buf_.size() - r + w;
    }

    std::size_t writable() const noexcept { return capacity() - readable(); }

    // Escreve ate n floats. Devolve quantos coube. So o produtor chama.
    std::size_t write(const float* src, std::size_t n) noexcept {
        n = std::min(n, writable());
        std::size_t w = write_.load(std::memory_order_relaxed);
        const std::size_t first = std::min(n, buf_.size() - w);
        std::memcpy(buf_.data() + w, src, first * sizeof(float));
        if (n > first) std::memcpy(buf_.data(), src + first, (n - first) * sizeof(float));
        write_.store((w + n) % buf_.size(), std::memory_order_release);
        return n;
    }

    // Le ate n floats. Devolve quantos saiu. So o consumidor chama.
    std::size_t read(float* dst, std::size_t n) noexcept {
        n = std::min(n, readable());
        std::size_t r = read_.load(std::memory_order_relaxed);
        const std::size_t first = std::min(n, buf_.size() - r);
        std::memcpy(dst, buf_.data() + r, first * sizeof(float));
        if (n > first) std::memcpy(dst + first, buf_.data(), (n - first) * sizeof(float));
        read_.store((r + n) % buf_.size(), std::memory_order_release);
        return n;
    }

    // Precondicao: nem produtor nem consumidor em execucao.
    //
    // O engine garante isso suspendendo o dispositivo antes de carregar ou
    // buscar, e encerrando o thread de decodificacao antes de chamar aqui.
    void reset() noexcept {
        read_.store(0, std::memory_order_relaxed);
        write_.store(0, std::memory_order_relaxed);
    }

private:
    std::vector<float> buf_;
    std::atomic<std::size_t> read_{0};
    std::atomic<std::size_t> write_{0};
};

}  // namespace pang::core
