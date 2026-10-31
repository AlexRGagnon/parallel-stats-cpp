#pragma once
#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace parallel_stats {
// Blocking, bounded submission; graceful close drains accepted tasks.
// Same-pool nested submission is rejected rather than risking starvation deadlock.
class BoundedPool final {
public:
    struct Snapshot { std::size_t queued, active, peak_queued, peak_active; bool closed; };
    explicit BoundedPool(std::size_t workers, std::size_t capacity) : capacity_(capacity) {
        if (workers == 0 || workers > 256 || capacity == 0 || capacity > 1000000)
            throw std::invalid_argument("workers must be 1..256 and capacity 1..1000000");
        try {
            threads_.reserve(workers);
            for (std::size_t i = 0; i < workers; ++i)
                threads_.emplace_back([this] { work(); });
        } catch (...) {
            close();
            threads_.clear();
            throw;
        }
    }
    BoundedPool(const BoundedPool&) = delete;
    BoundedPool& operator=(const BoundedPool&) = delete;
    ~BoundedPool() {
        // The owner must outlive its tasks; destruction from a worker is invalid.
        close();
        threads_.clear();
    }
    template<class F>
    auto submit(F&& f) -> std::future<std::invoke_result_t<std::decay_t<F>&>> {
        if (current_ == this) throw std::logic_error("nested same-pool submission is prohibited");
        using R = std::invoke_result_t<std::decay_t<F>&>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        auto future = task->get_future();
        {
            std::unique_lock lock(mutex_);
            space_.wait(lock, [this] { return closed_ || queue_.size() < capacity_; });
            if (closed_) throw std::runtime_error("pool is closed");
            queue_.emplace_back([task] { (*task)(); });
            peak_queued_ = std::max(peak_queued_, queue_.size());
        }
        ready_.notify_one();
        return future;
    }
    void close() {
        { std::lock_guard lock(mutex_); closed_ = true; }
        ready_.notify_all();
        space_.notify_all();
    }
    Snapshot snapshot() const {
        std::lock_guard lock(mutex_);
        return {queue_.size(), active_, peak_queued_, peak_active_, closed_};
    }
private:
    void work() {
        current_ = this;
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [this] { return closed_ || !queue_.empty(); });
                if (queue_.empty()) break;
                task = std::move(queue_.front());
                queue_.pop_front();
                ++active_;
                peak_active_ = std::max(peak_active_, active_);
            }
            space_.notify_one();
            task(); // packaged_task transports user exceptions to its future.
            { std::lock_guard lock(mutex_); --active_; }
        }
        current_ = nullptr;
    }
    inline static thread_local BoundedPool* current_ = nullptr;
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable ready_, space_;
    std::deque<std::function<void()>> queue_;
    bool closed_ = false;
    std::size_t active_ = 0, peak_queued_ = 0, peak_active_ = 0;
    std::vector<std::jthread> threads_;
};
}
