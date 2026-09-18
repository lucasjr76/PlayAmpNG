#include "core/state/controller.h"

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

void Controller::playlist_changed() { shuffle_.invalidate(); }

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
    guarded([&] { engine_.load(path, desired); });
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
