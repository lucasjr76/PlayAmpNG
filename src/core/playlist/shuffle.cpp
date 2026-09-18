#include "core/playlist/shuffle.h"

#include <algorithm>
#include <random>

namespace pang::core {

void ShuffleOrder::reset(int count, int start, std::uint64_t seed) {
    order_.resize(static_cast<std::size_t>(std::max(0, count)));
    for (int i = 0; i < count; ++i) order_[static_cast<std::size_t>(i)] = i;

    std::mt19937_64 rng(seed);
    std::shuffle(order_.begin(), order_.end(), rng);

    // A faixa atual vai para a frente do ciclo: ligar shuffle nao deve
    // interromper o que esta tocando.
    if (start >= 0 && start < count) {
        auto it = std::find(order_.begin(), order_.end(), start);
        if (it != order_.end()) std::iter_swap(order_.begin(), it);
        cursor_ = 0;
    } else {
        cursor_ = -1;
    }
}

int ShuffleOrder::current() const {
    if (cursor_ < 0 || cursor_ >= size()) return -1;
    return order_[static_cast<std::size_t>(cursor_)];
}

int ShuffleOrder::next() {
    if (order_.empty()) return -1;
    if (cursor_ + 1 >= size()) return -1;  // ciclo completo
    ++cursor_;
    return current();
}

int ShuffleOrder::peek_next() const {
    if (order_.empty() || cursor_ + 1 >= size()) return -1;
    return order_[static_cast<std::size_t>(cursor_ + 1)];
}

int ShuffleOrder::previous() {
    if (order_.empty() || cursor_ <= 0) return -1;
    --cursor_;
    return current();
}

}  // namespace pang::core
