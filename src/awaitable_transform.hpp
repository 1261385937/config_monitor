#pragma once
#include <coroutine>
#include <string>

namespace coro {

template <typename T>
class coroutine_task {
public:
    struct promise_type {
        std::suspend_never initial_suspend() noexcept { return {}; }

        std::suspend_never final_suspend() noexcept { return {}; }

        void unhandled_exception() noexcept {}

        coroutine_task get_return_object() noexcept {
            auto tk = coroutine_task{ std::coroutine_handle<promise_type>::from_promise(*this) };
            return tk;
        }

        void return_value(T) noexcept {}

        std::suspend_always yield_value(std::string&& from) noexcept {
            value_ = std::move(from);
            return {};
        }
        std::string value_;
    };

private:
    std::coroutine_handle<promise_type> coro_;

public:
    coroutine_task(std::coroutine_handle<promise_type> h) : coro_(h) {}

    std::string value() { return coro_.promise().value_; }
};

template <>
class coroutine_task<void> {
public:
    struct promise_type {
        std::suspend_never initial_suspend() noexcept { return {}; }

        std::suspend_never final_suspend() noexcept { return {}; }

        void unhandled_exception() noexcept {}

        coroutine_task get_return_object() noexcept {
            auto tk = coroutine_task{ std::coroutine_handle<promise_type>::from_promise(*this) };
            return tk;
        }

        void return_void() noexcept {}

        std::suspend_always yield_value(std::string&& from) noexcept {
            value_ = std::move(from);
            return {};
        }
        std::string value_;
    };

private:
    std::coroutine_handle<promise_type> coro_;

public:
    coroutine_task(std::coroutine_handle<promise_type> h) : coro_(h) {}
};


template <typename T>
struct awaitable {
private:
    T data_;
    std::coroutine_handle<> coro_handle_;

public:
    awaitable() = default;

    bool await_ready() noexcept {
        return false;
    }

    T await_resume() noexcept {
        return data_;
    }

    void await_suspend(std::coroutine_handle<> p) noexcept {
        coro_handle_ = p;
    }

    void resume() {
        coro_handle_.resume();
    }

    template<typename Ret>
    void set_resume_value(Ret&& data) {
        data_ = std::forward<Ret>(data);
    }
};

}
