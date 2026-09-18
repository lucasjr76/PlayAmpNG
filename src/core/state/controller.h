#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "core/audio/engine.h"
#include "core/playlist/playlist.h"
#include "core/playlist/shuffle.h"
#include "core/state/player.h"

namespace pang::core {

enum class Repeat : std::uint8_t { Off, Track, All };

// Liga engine e playlist: navegacao, shuffle, repeticao e avanco no fim da
// faixa. Sem Qt e sem dispositivo, entao PL-04, PL-24 e PL-25 sao verificaveis
// headless.
//
// `suspend` e `resume` envolvem as operacoes cuja precondicao e "render()
// parado". Na aplicacao sao ma_device_stop/ma_device_start; no teste sao vazios,
// porque quem chama render() e o proprio teste, em sequencia.
class Controller {
public:
    using Guard = std::function<void()>;

    Controller(Engine& engine, Guard suspend = {}, Guard resume = {});

    Playlist& playlist() { return playlist_; }
    const Playlist& playlist() const { return playlist_; }

    // A playlist foi alterada por fora (inclusao, remocao, ordenacao...).
    // Invalida o ciclo de shuffle: LI-15 fala de um ciclo sobre a lista atual.
    void playlist_changed();

    void play_index(int index);
    void play();
    void pause();
    void stop();

    // PL-05 / PL-04. "Anterior" sempre vai a faixa anterior; nunca reinicia a
    // atual. Em shuffle, retrocede pelo historico de navegacao (LI-14).
    void next();
    void previous();

    bool seek(double seconds);

    void set_repeat(Repeat r) { repeat_ = r; }
    Repeat repeat() const { return repeat_; }

    void set_shuffle(bool on);
    bool shuffle() const { return shuffle_on_; }

    int current_index() const;
    std::uint64_t current_id() const { return current_id_; }

    // Chamar periodicamente pela interface (ou pelo teste). Detecta fim de
    // faixa e avanca conforme repeat e shuffle.
    void poll();

private:
    void start(int index, State desired);
    int step_forward();   // -1 quando nao ha proxima
    int step_backward();  // -1 quando nao ha anterior
    void guarded(const std::function<void()>& action);

    Engine& engine_;
    Guard suspend_;
    Guard resume_;

    Playlist playlist_;
    ShuffleOrder shuffle_;
    Repeat repeat_ = Repeat::Off;
    bool shuffle_on_ = false;
    std::uint64_t current_id_ = 0;
    std::uint64_t shuffle_seed_ = 0x9E3779B97F4A7C15ull;
};

}  // namespace pang::core
