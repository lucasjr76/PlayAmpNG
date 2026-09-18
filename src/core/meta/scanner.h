#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/playlist/track.h"

namespace pang::core::meta {

// LI-16 — leitura de metadados em segundo plano.
//
// Um thread consome uma fila de (id, caminho) e devolve o resultado por
// callback. O id carrega a identidade estavel do item: se ele saiu da lista
// enquanto a leitura acontecia, Playlist::apply_metadata descarta o resultado
// em vez de escrever na faixa que ocupou aquele indice (AR-03).
//
// O callback roda no thread do scanner. Quem recebe e responsavel por levar o
// resultado ao seu proprio thread — a interface faz isso com um sinal Qt.
class Scanner {
public:
    struct Result {
        std::uint64_t id;
        Track track;
    };
    using Callback = std::function<void(const Result&)>;

    explicit Scanner(Callback callback);
    ~Scanner();
    Scanner(const Scanner&) = delete;
    Scanner& operator=(const Scanner&) = delete;

    void enqueue(std::uint64_t id, std::string path);
    void enqueue_many(std::vector<std::pair<std::uint64_t, std::string>> items);

    // Descarta o que ainda nao foi lido. Usado quando a lista e limpa.
    void drop_pending();

    std::size_t pending() const;

    // Bloqueia ate a fila esvaziar. So para teste e para encerramento.
    void wait_idle();

private:
    void loop();

    Callback callback_;
    mutable std::mutex mutex_;
    std::condition_variable work_;
    std::condition_variable idle_;
    std::deque<std::pair<std::uint64_t, std::string>> queue_;
    bool busy_ = false;
    std::atomic<bool> quit_{false};
    std::thread thread_;
};

}  // namespace pang::core::meta
