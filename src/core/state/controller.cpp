#include "core/state/controller.h"

#include "core/audio/network.h"

namespace pang::core {

Controller::Controller(Engine& engine, Guard suspend, Guard resume)
    : engine_(engine), suspend_(std::move(suspend)), resume_(std::move(resume)) {}

void Controller::guarded(const std::function<void()>& action) {
    if (suspend_) suspend_();
    action();
    if (resume_) resume_();
}

int Controller::current_index() const {
    return current_id_ == 0 ? -1 : playlist_.index_of(current_id_);
}

std::string Controller::now_playing_title() const {
    const int index = current_index();
    if (index < 0) return {};
    if (std::string icy = engine_.icy_title(); !icy.empty()) return icy;
    return playlist_.at(index).display_title();
}

std::string Controller::now_playing_status() const {
    switch (engine_.snapshot().state) {
        case State::Loading: {
            const int index = current_index();
            return index >= 0 && is_remote(playlist_.at(index).path) ? "[CONECTANDO]"
                                                                      : std::string{};
        }
        case State::Buffering:
            return "[BUFFER]";
        case State::Error: {
            // O erro do motor ja vem com o endereco redigido (AR-06).
            const std::string error = engine_.last_error();
            return error.empty() ? "[ERRO]" : "[ERRO] " + error;
        }
        default:
            return {};
    }
}

void Controller::playlist_changed() {
    shuffle_.invalidate();
    // A pre-carga aponta para uma faixa que talvez nao seja mais a proxima.
    queue_next();
}

void Controller::set_shuffle(bool on) {
    shuffle_on_ = on;
    if (on)
        shuffle_.reset(playlist_.size(), current_index(), shuffle_seed_++);
    else
        shuffle_.invalidate();
}

void Controller::start(int index, State desired) {
    if (index < 0 || index >= playlist_.size()) return;
    current_id_ = playlist_.at(index).id;

    // Mantem o ciclo de shuffle apontando para o que esta tocando, para que
    // "anterior" depois de um salto manual volte ao ponto certo.
    if (shuffle_on_ && !shuffle_.valid_for(playlist_.size()))
        shuffle_.reset(playlist_.size(), index, shuffle_seed_++);

    const std::string path = playlist_.at(index).path;
    const std::uint64_t id = playlist_.at(index).id;
    guarded([&] { engine_.load(path, desired, id); });
    queue_next();
}

void Controller::play_index(int index) { start(index, State::Playing); }

void Controller::play() {
    if (current_id_ == 0 && !playlist_.empty()) {
        play_index(0);
        return;
    }
    engine_.play();
}

void Controller::pause() { engine_.pause(); }

void Controller::stop() {
    guarded([&] { engine_.stop(); });
}

int Controller::step_forward() {
    const int count = playlist_.size();
    if (count == 0) return -1;

    if (shuffle_on_) {
        if (!shuffle_.valid_for(count)) shuffle_.reset(count, current_index(), shuffle_seed_++);
        const int next = shuffle_.next();
        if (next >= 0) return next;

        // LI-15 — o ciclo cobriu todos os itens sem repetir. Um ciclo novo so
        // comeca se a repeticao da playlist estiver ligada.
        if (repeat_ == Repeat::All) {
            shuffle_.reset(count, -1, shuffle_seed_++);
            return shuffle_.next();
        }
        return -1;
    }

    const int index = current_index();
    if (index < 0) return 0;
    if (index + 1 < count) return index + 1;
    return repeat_ == Repeat::All ? 0 : -1;
}

// Mesma decisao de step_forward, sem efeito colateral.
//
// No fim de um ciclo de shuffle com repeat=all seria preciso reembaralhar para
// saber a proxima, e reembaralhar e mutacao. Nesse caso devolvemos -1: nao ha
// pre-carga, a troca acontece pelo caminho normal e o gapless perde essa
// transicao — uma por ciclo inteiro.
int Controller::peek_forward() const {
    const int count = playlist_.size();
    if (count == 0) return -1;

    if (repeat_ == Repeat::Track) return current_index();

    if (shuffle_on_) {
        if (!shuffle_.valid_for(count)) return -1;
        return shuffle_.peek_next();
    }

    const int index = current_index();
    if (index < 0) return 0;
    if (index + 1 < count) return index + 1;
    return repeat_ == Repeat::All ? 0 : -1;
}

// AU-13 — entrega ao engine a faixa seguinte para ele emendar sem lacuna.
void Controller::queue_next() {
    const int index = peek_forward();
    if (index < 0) {
        engine_.clear_next();
        return;
    }
    engine_.set_next(playlist_.at(index).path, playlist_.at(index).id);
}

int Controller::step_backward() {
    const int count = playlist_.size();
    if (count == 0) return -1;

    if (shuffle_on_) {
        if (!shuffle_.valid_for(count)) return -1;
        return shuffle_.previous();  // LI-14 — historico real, nao sorteio novo
    }

    const int index = current_index();
    if (index < 0) return -1;
    if (index > 0) return index - 1;
    return repeat_ == Repeat::All ? count - 1 : -1;
}

void Controller::next() {
    // PL-25 — trocar de faixa durante a pausa mantem pausado; parado mantem
    // parado. So continua tocando quem ja estava tocando.
    const State current = engine_.snapshot().state;
    const State desired = current == State::Playing  ? State::Playing
                          : current == State::Paused ? State::Paused
                                                     : State::Stopped;
    const int index = step_forward();
    if (index < 0) {
        guarded([&] { engine_.stop(); });
        return;
    }
    start(index, desired);
}

void Controller::previous() {
    const State current = engine_.snapshot().state;
    const State desired = current == State::Playing  ? State::Playing
                          : current == State::Paused ? State::Paused
                                                     : State::Stopped;
    const int index = step_backward();
    if (index < 0) return;  // PL-04 — na primeira faixa, permanece nela
    start(index, desired);
}

bool Controller::seek(double seconds) {
    bool ok = false;
    guarded([&] { ok = engine_.seek(seconds); });
    return ok;
}

void Controller::poll() {
    // AU-13 — o engine emendou sozinho: o token publicado passou a ser o da
    // faixa seguinte. Aqui apenas acompanhamos a mudanca e ja preparamos a
    // proxima emenda.
    const std::uint64_t token = engine_.current_token();
    if (token != 0 && token != current_id_ && playlist_.index_of(token) >= 0) {
        if (shuffle_on_ && repeat_ != Repeat::Track) shuffle_.next();  // consome o ciclo
        current_id_ = token;
        queue_next();
        return;
    }

    if (!engine_.ended()) return;

    // PL-24 — repeticao da faixa recomeca a mesma; caso contrario, avanca.
    if (repeat_ == Repeat::Track) {
        const int index = current_index();
        if (index >= 0) {
            start(index, State::Playing);
            return;
        }
    }

    const int index = step_forward();
    if (index < 0) {
        // Fim da playlist com repeat=off: para na ultima faixa, posicao 0.
        guarded([&] { engine_.stop(); });
        return;
    }
    start(index, State::Playing);
}

}  // namespace pang::core
