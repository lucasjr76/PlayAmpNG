#pragma once

#include <cstdint>

namespace pang::platform {

// AU-12, RB-05 — politica de recuperacao do dispositivo de saida.
//
// Separada da abertura do dispositivo de proposito: a decisao de QUANDO tentar
// de novo, QUANTAS vezes e QUANDO desistir e logica pura, e logica pura pode
// ser verificada sem placa de som. Misturada ao codigo do miniaudio, so daria
// para exercita-la desconectando um fone na mao.
//
// O dono chama tick() periodicamente com o tempo decorrido e, quando ela
// responde Reopen, tenta reabrir e informa o resultado por succeeded/failed.
class DeviceRecovery {
public:
    enum class Action : std::uint8_t {
        Idle,     // dispositivo saudavel, nada a fazer
        Wait,     // perdido, aguardando o intervalo antes da proxima tentativa
        Reopen,   // reabra agora
        GiveUp,   // tentativas esgotadas; o dono deve reportar erro uma vez
    };

    static constexpr int kMaxAttempts = 3;
    static constexpr int kDelayMs = 500;

    // O dispositivo sumiu. Idempotente: varias notificacoes da mesma perda
    // nao reiniciam a contagem, senao um dispositivo que pisca faria o player
    // tentar para sempre.
    void lost() {
        if (state_ == State::Healthy) {
            state_ = State::Waiting;
            attempts_ = 0;
            elapsed_ms_ = 0;
        }
    }

    Action tick(int elapsed_ms) {
        switch (state_) {
            case State::Healthy:
                return Action::Idle;
            case State::Waiting:
                elapsed_ms_ += elapsed_ms;
                if (elapsed_ms_ < kDelayMs) return Action::Wait;
                elapsed_ms_ = 0;
                ++attempts_;
                state_ = State::Trying;
                return Action::Reopen;
            case State::Trying:
                // O dono deve responder antes do proximo tick.
                return Action::Wait;
            case State::Exhausted:
                return Action::GiveUp;
        }
        return Action::Idle;
    }

    void succeeded() {
        state_ = State::Healthy;
        attempts_ = 0;
        elapsed_ms_ = 0;
    }

    void failed() {
        state_ = attempts_ >= kMaxAttempts ? State::Exhausted : State::Waiting;
        elapsed_ms_ = 0;
    }

    bool healthy() const { return state_ == State::Healthy; }
    bool exhausted() const { return state_ == State::Exhausted; }
    int attempts() const { return attempts_; }

    // Depois de esgotar, o usuario pode pedir para tentar de novo (trocando de
    // dispositivo, por exemplo). Sem isto o player ficaria mudo ate reiniciar.
    void reset() {
        state_ = State::Healthy;
        attempts_ = 0;
        elapsed_ms_ = 0;
    }

private:
    enum class State : std::uint8_t { Healthy, Waiting, Trying, Exhausted };
    State state_ = State::Healthy;
    int attempts_ = 0;
    int elapsed_ms_ = 0;
};

}  // namespace pang::platform
