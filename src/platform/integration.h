#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace pang::platform {

// MD-*, IN-04, IN-05 — integracao com o ambiente de trabalho.
//
// Um arquivo por sistema, escolhido pelo CMake: MPRIS no Linux, SMTC no
// Windows, MPNowPlayingInfoCenter no macOS. A interface e a mesma nos tres, e
// nenhum dos tres e obrigatorio — onde a integracao nao existir, available()
// devolve falso e o player funciona igual, so sem aparecer no painel do
// sistema. Nada aqui pode ser condicao para tocar.
struct NowPlaying {
    std::string title;
    std::string artist;
    std::string album;
    std::string url;               // caminho ou endereco da fonte
    std::int64_t duration_us = -1; // -1 quando a fonte nao informa (MD-09)
    std::int64_t position_us = 0;
    bool playing = false;
    bool stopped = true;
    bool can_seek = false;
    bool can_go_next = false;
    bool can_go_previous = false;
    float volume = 1.0f;
};

// O que o ambiente pode pedir ao player. IN-04 — no Linux as teclas de midia
// chegam por aqui: o ambiente de trabalho as encaminha ao player que estiver
// registrado, em vez de cada programa capturar a tecla por conta propria.
struct Commands {
    std::function<void()> play;
    std::function<void()> pause;
    std::function<void()> play_pause;
    std::function<void()> stop;
    std::function<void()> next;
    std::function<void()> previous;
    std::function<void(std::int64_t offset_us)> seek;
    std::function<void(std::int64_t position_us)> set_position;
    std::function<void(float)> set_volume;
    std::function<void()> raise;   // o ambiente pede a janela em primeiro plano
    std::function<void()> quit;
};

class Integration {
public:
    virtual ~Integration() = default;

    // Falso quando o ambiente nao oferece a integracao. Nao e erro.
    virtual bool available() const = 0;

    // Publica o estado. Chamado com frequencia; a implementacao e responsavel
    // por so notificar o barramento quando algo mudou, senao o trafego de
    // D-Bus cresce sem que nada mude na tela.
    virtual void publish(const NowPlaying& now) = 0;

    virtual std::string backend() const = 0;
};

// Devolve sempre um objeto valido. Onde nao houver integracao, devolve um que
// responde available() == false — assim o chamador nunca precisa testar nulo.
//
// native_window e o identificador de janela do sistema (HWND no Windows), ou
// nulo. Existe porque o SMTC nao e um servico do sistema como o MPRIS: ele se
// prende a UMA janela, e sem ela nao ha o que registrar. Linux e macOS
// ignoram o parametro; e o chamador que nao deve ter de saber disso.
std::unique_ptr<Integration> make_integration(const std::string& application_name,
                                              Commands commands,
                                              void* native_window = nullptr);

}  // namespace pang::platform
