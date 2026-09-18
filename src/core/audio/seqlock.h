#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace pang::core {

// Publicacao de estado do thread de audio para a interface.
//
// Por que nao `std::atomic<T>`: o bloco publicado passa de 16 bytes, e
// `std::atomic` para um tipo desse tamanho e implementado com lock na maioria
// das plataformas — o que colocaria um mutex dentro do callback de audio.
//
// No seqlock o escritor nunca espera: incrementa o contador para impar,
// escreve, incrementa para par. So o leitor gira, e o leitor e a interface,
// que pode girar a vontade.
//
// O conteudo e guardado como palavras `std::atomic<uint64_t>` acessadas com
// ordem relaxed, e nao como um T cru. A versao com memcpy sobre um membro
// comum funciona na pratica, mas e uma corrida de dados — comportamento
// indefinido que o otimizador tem permissao de explorar. Palavras atomicas
// custam o mesmo, eliminam a UB e impedem que o compilador iceste a leitura
// para fora do laco de repeticao. As fences continuam fornecendo a ordem.
//
// Um unico escritor por instancia. Aqui, o thread de audio.
template <typename T>
class Seqlock {
    static_assert(std::is_trivially_copyable_v<T>,
                  "Seqlock copia bytes crus: o tipo precisa ser trivialmente copiavel");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "AR-11: nenhum tipo e presumido lock-free sem verificacao");

    using Word = std::uint64_t;
    static constexpr std::size_t kWords = (sizeof(T) + sizeof(Word) - 1) / sizeof(Word);

public:
    void store(const T& value) noexcept {
        Word words[kWords] = {};
        std::memcpy(words, &value, sizeof(T));

        seq_.fetch_add(1, std::memory_order_relaxed);  // impar: escrita em curso
        std::atomic_thread_fence(std::memory_order_release);
        for (std::size_t i = 0; i < kWords; ++i)
            words_[i].store(words[i], std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        seq_.fetch_add(1, std::memory_order_relaxed);  // par: consistente
    }

    T load() const noexcept {
        Word words[kWords];
        for (;;) {
            const Word before = seq_.load(std::memory_order_acquire);
            if (before & 1u) continue;  // escrita em andamento

            for (std::size_t i = 0; i < kWords; ++i)
                words[i] = words_[i].load(std::memory_order_relaxed);

            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_relaxed) == before) break;
        }
        T out;
        std::memcpy(&out, words, sizeof(T));
        return out;
    }

    // Numero de publicacoes concluidas. Zero significa que nada foi publicado
    // ainda, e o conteudo e o valor inicial — nao um bloco rasgado.
    std::uint64_t versions() const noexcept {
        return seq_.load(std::memory_order_acquire) / 2;
    }

private:
    mutable std::atomic<Word> seq_{0};
    std::atomic<Word> words_[kWords] = {};
};

}  // namespace pang::core
