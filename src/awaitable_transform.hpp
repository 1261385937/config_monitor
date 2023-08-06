#pragma once
#include <coroutine>
#include <string>

namespace coro {

template <typename T>
struct value_awaiter {
    T data_;
    std::coroutine_handle<> coro_;

    bool await_ready() noexcept {
        return false;
    }

    T await_resume() noexcept {
        return data_;
    }

    void await_suspend(std::coroutine_handle<> p) noexcept {
       // printf("await_suspend, coro address:%p\n", p.address());
        coro_ = p;
    }

    void resume() {
        //printf("\nresume, coro address:%p\n", coro_.address());
        coro_.resume();
    }

    template<typename Ret>
    void set_resume_value(Ret&& data) {
        data_ = std::forward<Ret>(data);
    }

    template<typename Ret>
    void set_value_then_resume(Ret&& data) {
        this->set_resume_value(std::forward<Ret>(data));
        this->resume();
    }
};

template<typename T>
class coro_task;

template<typename T>
struct coro_task_promise;

template<typename T>
struct coro_task_promise_base {
    std::coroutine_handle<> caller_handle_ = nullptr;

    std::suspend_never initial_suspend() noexcept {
        /*printf("initial_suspend, coro address:%p\n",
            static_cast<coro_task_promise<T>*>(this)->handle_.address());*/
        return {};
    }

    std::suspend_never final_suspend() noexcept {
       /* printf("final_suspend, coro address:%p\n",
            static_cast<coro_task_promise<T>*>(this)->handle_.address());*/
        if (caller_handle_) {
           // printf("resume other coro address:%p\n", caller_handle_.address());
            caller_handle_.resume();
        }
        return {};
    }

    void unhandled_exception() noexcept {
        std::rethrow_exception(std::current_exception());
    }
};

template<typename T>
struct coro_task_promise : coro_task_promise_base<T> {
    std::coroutine_handle<coro_task_promise> handle_;
    T value_;

    void return_value(T val) noexcept {
        //printf("coro address:%p\n", handle_.address());
        value_ = std::move(val);
    }

    std::suspend_always yield_value(T&& from) noexcept {
        value_ = std::move(from);
        return {};
    }

    coro_task<T> get_return_object() noexcept;
};


template<>
struct coro_task_promise<void> : coro_task_promise_base<void> {
    std::coroutine_handle<coro_task_promise> handle_;

    void return_void() noexcept {
        //printf("return_void, coro address:%p\n", handle_.address());
    }

    coro_task<void> get_return_object() noexcept;
};

template <typename T = void>
class coro_task {
public:
    using promise_type = coro_task_promise<T>;
private:
    std::coroutine_handle<promise_type> coro_;

public:
    coro_task(const coro_task&) = delete;
    coro_task& operator=(const coro_task&) = delete;

    coro_task(std::coroutine_handle<promise_type> h) : coro_(h) {
       // printf("begin, coro address:%p\n", coro_.address());
    }

    coro_task(coro_task&& other) noexcept
        : coro_(std::move(other.coro_)) {
       // printf("move, coro address:%p\n", coro_.address());
        other.coro_ = nullptr;
    }

    ~coro_task() {
      //  printf("end, coro address:%p\n\n", coro_.address());
    }

    constexpr bool await_ready() const noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<> h) noexcept {
       // printf("await_suspend, coro address:%p\n", h.address());
        coro_.promise().caller_handle_ = h;
    }

    T await_resume() noexcept {
        if constexpr (!std::is_same_v<T, void>) {
            return coro_.promise().value_;
        }
    }

    T get_value() {
        if constexpr (!std::is_same_v<T, void>) {
            return coro_.promise().value_;
        }
    }
};

template <typename T>
inline coro_task<T> coro_task_promise<T>::get_return_object() noexcept {
    handle_ = std::coroutine_handle<coro_task_promise>::from_promise(*this);
   // printf("get_return_object, coro address:%p\n", handle_.address());
    auto tk = coro_task<T>{ handle_ };
    return tk;
}

inline coro_task<void> coro_task_promise<void>::get_return_object() noexcept {
    handle_ = std::coroutine_handle<coro_task_promise>::from_promise(*this);
   // printf("get_return_object, coro address:%p\n", handle_.address());
    auto tk = coro_task<void>{ handle_ };
    return tk;
}

}
