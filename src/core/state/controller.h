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

    // MD-05 — o titulo do que esta tocando AGORA.
    //
    // Em stream, o que o servidor anuncia (ICY StreamTitle) vale mais que o
    // titulo da entrada da playlist, que para uma radio e so o endereco. Vazio
    // quando nada esta carregado.
    //
    // Uma rota so, usada pela janela principal e pelo painel de midia do
    // sistema. Antes cada um decidia por conta propria, e divergiram: o painel
    // do sistema mostrava a musica da radio, e a janela do proprio player
    // mostrava o endereco.
    std::string now_playing_title() const;

    // MD-04 — o estado da fonte, em palavras, para quem olha a janela.
    //
    // O motor ja distinguia conectando, buffering e erro, mas nada disso
    // chegava a tela: uma radio conectando era identica a uma parada, e uma
    // que falhou nao dizia nada. Como no Winamp, o aviso vai no letreiro do
    // titulo — sem arte nova, no lugar para onde o usuario ja olha.
    //
    // Vazio quando nao ha o que dizer. "Conectando" so para fonte remota:
    // abrir um arquivo local e instantaneo, e o aviso so piscaria.
    std::string now_playing_status() const;

    // Chamar periodicamente pela interface (ou pelo teste). Detecta fim de
    // faixa e avanca conforme repeat e shuffle.
    void poll();

private:
    void start(int index, State desired);
    int step_forward();          // -1 quando nao ha proxima; AVANCA o shuffle
    int peek_forward() const;    // mesma decisao, sem mexer no ciclo
    int step_backward();         // -1 quando nao ha anterior
    void queue_next();           // informa ao engine a faixa a emendar
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
