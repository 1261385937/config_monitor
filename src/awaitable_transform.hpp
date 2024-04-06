#pragma once
#include <coroutine>
#include <string>

namespace coro {

template <typename RetType, typename Func>
struct callback_awaiter {
    Func cb_;
    RetType result_;
    std::coroutine_handle<> handle_;

    callback_awaiter(Func cb) : cb_(std::move(cb)) {}
    bool await_ready() noexcept { return false; }
    RetType await_resume() noexcept { return std::move(result_); }
    void resume() noexcept { handle_.resume(); }
    void set_resume_value(RetType t) noexcept { result_ = std::move(t); }

    void await_suspend(std::coroutine_handle<> handle) noexcept {
        handle_ = handle;
        cb_(this);
    }
};

//As the start coro. Never suspend
struct detached_coro_task {
    struct promise_type {
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { std::rethrow_exception(std::current_exception()); }
        detached_coro_task get_return_object() noexcept { return detached_coro_task{}; }
    };
};


template<typename T>
class coro_task;

template<typename T>
struct coro_task_promise;

template<typename T>
struct coro_task_promise_base {
    struct final_awaiter {
        bool await_ready() noexcept { return false; }
        void await_resume() noexcept {}
        template <typename PromiseType>
        auto await_suspend(std::coroutine_handle<PromiseType> h) noexcept {
            return h.promise().caller_handle_;
        }
    };

    std::coroutine_handle<> caller_handle_ = nullptr;
    std::suspend_always initial_suspend() noexcept { return {}; }
    auto final_suspend() noexcept { return final_awaiter{}; }
    void unhandled_exception() noexcept { std::rethrow_exception(std::current_exception()); }
};

template<typename T>
struct coro_task_promise : coro_task_promise_base<T> {
    T value_{};
    coro_task<T> get_return_object() noexcept;
    void return_value(T val) noexcept { value_ = std::move(val); }

    std::suspend_always yield_value(T&& from) noexcept {
        value_ = std::move(from);
        return {};
    }
};

template<>
struct coro_task_promise<void> : coro_task_promise_base<void> {
    coro_task<void> get_return_object() noexcept;
    void return_void() noexcept {}    
};

template <typename T = void>
class coro_task {
public:
    using promise_type = coro_task_promise<T>;
private:
    std::coroutine_handle<promise_type> coro_handle_;

public:
    coro_task(const coro_task&) = delete;
    coro_task& operator=(const coro_task&) = delete;

    coro_task(std::coroutine_handle<promise_type> h) : coro_handle_(h) {}

    coro_task(coro_task&& other) noexcept
        : coro_handle_(std::move(other.coro_handle_)) {
        other.coro_handle_ = nullptr;
    }

    ~coro_task() {
        if (coro_handle_) {
            coro_handle_.destroy();
            coro_handle_ = nullptr;
        }
    }

    constexpr bool await_ready() const noexcept { return false; }

    auto await_suspend(std::coroutine_handle<> h) noexcept {
        coro_handle_.promise().caller_handle_ = h;
        return coro_handle_;
    }

    T await_resume() noexcept {
        if constexpr (!std::is_same_v<T, void>) {
            auto r = std::move(coro_handle_.promise().value_);
            coro_handle_.destroy();
            coro_handle_ = nullptr;
            return r;
        }
        else {
            coro_handle_.destroy();
            coro_handle_ = nullptr;
        } 
    }
};

template <typename T>
inline coro_task<T> coro_task_promise<T>::get_return_object() noexcept {
    return coro_task<T>{ std::coroutine_handle<coro_task_promise>::from_promise(*this) };
}

inline coro_task<void> coro_task_promise<void>::get_return_object() noexcept {
    return coro_task<void>{ std::coroutine_handle<coro_task_promise>::from_promise(*this) };
}

}
