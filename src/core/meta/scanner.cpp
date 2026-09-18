#include "core/meta/scanner.h"

#include "core/meta/tags.h"

namespace pang::core::meta {

Scanner::Scanner(Callback callback) : callback_(std::move(callback)) {
    thread_ = std::thread([this] { loop(); });
}

Scanner::~Scanner() {
    quit_.store(true);
    work_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void Scanner::enqueue(std::uint64_t id, std::string path) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.emplace_back(id, std::move(path));
    }
    work_.notify_one();
}

void Scanner::enqueue_many(std::vector<std::pair<std::uint64_t, std::string>> items) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : items) queue_.push_back(std::move(item));
    }
    work_.notify_all();
}

void Scanner::drop_pending() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
}

std::size_t Scanner::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void Scanner::wait_idle() {
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [this] { return queue_.empty() && !busy_; });
}

void Scanner::loop() {
    for (;;) {
        std::pair<std::uint64_t, std::string> item;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            work_.wait(lock, [this] { return quit_.load() || !queue_.empty(); });
            if (quit_.load()) return;
            item = std::move(queue_.front());
            queue_.pop_front();
            busy_ = true;
        }

        Track track = read_tags(item.second);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            busy_ = false;
        }
        if (callback_) callback_({item.first, std::move(track)});
        idle_.notify_all();
    }
}

}  // namespace pang::core::meta
