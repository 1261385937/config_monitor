#pragma once
#define THREADED
#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define USE_STATIC_LIB
#pragma comment(lib, "ws2_32.lib")
#else
#include "arpa/inet.h"
#include "netinet/in.h"
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include "../awaitable_transform.hpp"
#include "cppzk_define.h"

namespace zk {
class cppzk {
private:
    zhandle_t* zh_{};
    std::string hosts_;
    int unused_flags_ = 0;

    std::thread detect_expired_thread_;
    std::atomic<int> session_timeout_ms_ = -1;
    std::atomic<bool> run_ = true;
    std::chrono::time_point<std::chrono::system_clock>
        session_begin_timepoint_{ std::chrono::system_clock::now() };
    std::atomic<bool> need_detect_ = false;
    expired_callback expired_cb_ = []() { exit(0); };
    std::once_flag of_;
    std::atomic<bool> is_conntected_ = false;

public:
    cppzk(const cppzk&) = delete;
    cppzk& operator=(const cppzk&) = delete;
    cppzk() = default;

    ~cppzk() {
        run_ = false;
        if (detect_expired_thread_.joinable()) {
            detect_expired_thread_.join();
        }
        zookeeper_close(zh_);
    }

    void set_expired_cb(expired_callback expired_watcher) {
        if (expired_watcher) {
            expired_cb_ = std::move(expired_watcher);
        }
    }

    // If enable ssl, param cert like this "server.crt,client.crt,client.pem,passwd"
    // Or defualt value disbale ssl
    void initialize(std::string_view hosts, int session_timeout_ms,
                    const char* cert = "", int unused_flags = 0) {
        hosts_ = hosts;
        session_timeout_ms_ = session_timeout_ms;
        unused_flags_ = unused_flags;
        connect_server(cert);
        std::call_once(of_, [this]() { detect_expired_session(); });
    }

    zk_error clear_resource() {
        return static_cast<zk_error>(zookeeper_close(zh_));
    }

    zk_error handle_state() {
        return static_cast<zk_error>(zoo_state(zh_));
    }

    std::string last_error(zk_error e) {
        return zerror((int)e);
    }

    void set_log_level(zk_loglevel level) {
        zoo_set_debug_level(static_cast<ZooLogLevel>(level));
    }

    void set_log_to_file(std::string_view file_path) {
        zoo_set_log_stream(std::fopen(file_path.data(), "ab+"));
    }

    std::string get_client_ip() {
        auto zsock = *(zsock_t**)(&(*zh_));
        sockaddr_in addr{};
        socklen_t addr_len = sizeof(addr);
        auto ret = getsockname(zsock->sock, (struct sockaddr*)&addr, &addr_len);
        if (ret == 0) {
            return inet_ntoa(addr.sin_addr);
        }
        return {};
    }

    coro::coro_task<std::tuple<std::error_code, std::string>>
        async_create_path(std::string_view path, const std::optional<std::string>& value,
        zk_create_mode mode, int64_t ttl = -1, zk_acl acl = zk_acl::zk_open_acl_unsafe) {
        bool enable_ttl = false;
        if (mode == zk_create_mode::zk_persistent_sequential_with_ttl ||
            mode == zk_create_mode::zk_persistent_with_ttl) {
            enable_ttl = true;
        }
        if (enable_ttl && ttl < 0) {
            throw std::runtime_error("enable_ttl, ttl must > 0");
        }

        auto value_ptr = !value.has_value() ? nullptr : value.value().data();
        auto value_len = !value.has_value() ? -1 : (int)value.value().length();
        using awaiter_type = coro::value_awaiter<std::tuple<std::error_code, std::string>>;
        awaiter_type awaiter;
        auto r = zoo_acreate2_ttl(zh_, path.data(), value_ptr, value_len, &acl_mapping[acl],
            (int)mode, ttl, [](int rc, const char* string, const struct Stat*, const void* data) {
            auto ec = make_ec(rc);
            auto val = (string == nullptr ? std::string{} : std::string(string));
            auto awaiter = (awaiter_type*)data;
            awaiter->set_value_then_resume(std::make_tuple(std::move(ec), std::move(val)));
        }, &awaiter);

        if (r != ZOK) {
            throw std::runtime_error(std::string("zoo_acreate2_ttl error: ") + zerror(r));
        }
        co_return co_await awaiter;
    }

    coro::coro_task<std::error_code>
        async_delete_path(std::string_view path) {
        std::string p(path);
        std::deque<std::string> sub_paths;
        co_await async_recursive_get_sub_path(p, sub_paths);
        sub_paths.emplace_back(std::move(p));

        using awaiter_type = coro::value_awaiter<std::error_code>;
        for (auto& sub : sub_paths) {
            awaiter_type awaiter;
            auto r = zoo_adelete(zh_, sub.data(), -1, [](int rc, const void* data) {
                auto awaiter = (awaiter_type*)data;
                awaiter->set_value_then_resume(make_ec(rc));
            }, &awaiter);

            if (r != ZOK) {
                throw std::runtime_error(std::string("zoo_adelete error: ") + zerror(r));
            }
            auto ec = co_await awaiter;
            if (ec) {
                co_return ec;
            }
        }
    }

    coro::coro_task<std::error_code>
        async_set_path_value(std::string_view path, std::string_view value) {
        using awaiter_type = coro::value_awaiter<std::error_code>;
        awaiter_type awaiter;
        auto r = zoo_aset(zh_, path.data(), value.data(), (int)value.length(), -1,
            [](int rc, const struct Stat*, const void* data) {
            auto awaiter = (awaiter_type*)data;
            awaiter->set_value_then_resume(make_ec(rc));
        }, &awaiter);

        if (r != ZOK) {
            throw std::runtime_error(std::string("zoo_aset error: ") + zerror(r));
        }
        co_return co_await awaiter;
    }

    // [create/change/delete] event
    coro::coro_task<zk_event>
        async_watch_exists_path(std::string_view path) {
        using awaiter_type = coro::value_awaiter<zk_event>;
        auto wfn = [](zhandle_t*, int eve, int state, const char*, void* watcherCtx) {
            auto awaiter = (awaiter_type*)watcherCtx;
            if (eve == ZOO_SESSION_EVENT) {
                if (state == ZOO_EXPIRED_SESSION_STATE) {
                    awaiter->set_value_then_resume((zk_event)eve);
                }
                return;
            }   
            awaiter->set_value_then_resume((zk_event)eve);
        };
        auto completion = [](int, const struct Stat*, const void*) {};

        awaiter_type awaiter;
        auto r = zoo_awexists(zh_, path.data(), wfn, &awaiter, completion, nullptr);

        if (r != ZOK) {
            throw std::runtime_error(std::string("zoo_awexists error: ") + zerror(r));
        }
        co_return co_await awaiter;
    }

    // [child] event
    coro::coro_task<zk_event>
        async_watch_sub_path(std::string_view path) {
        using awaiter_type = coro::value_awaiter<zk_event>;
        auto wfn = [](zhandle_t*, int eve, int state, const char*, void* watcherCtx) {
            auto awaiter = (awaiter_type*)watcherCtx;
            if (eve == ZOO_SESSION_EVENT) {
                if (state == ZOO_EXPIRED_SESSION_STATE) {
                    awaiter->set_value_then_resume((zk_event)eve);
                }
                return;
            }
            awaiter->set_value_then_resume((zk_event)eve);
        };
        auto completion = [](int, const struct String_vector*, const struct Stat*, const void*) {};

        awaiter_type awaiter;
        auto r = zoo_awget_children2(zh_, path.data(), wfn, &awaiter, completion, nullptr);

        if (r != ZOK) {
            throw std::runtime_error(std::string("zoo_awget_children2 error: ") + zerror(r));
        }
        co_return co_await awaiter;
    }

    coro::coro_task<std::tuple<std::error_code, std::optional<std::string>>>
        async_get_path_value(std::string_view path) {
        using awaiter_type = coro::value_awaiter<
            std::tuple<std::error_code, std::optional<std::string>>>;
        auto cb = [](int rc, const char* value, int value_len, const struct Stat*, const void* data) {
            std::optional<std::string> dummy =
                value ? std::string(value, value_len) : std::optional<std::string>();
            auto awaiter = (awaiter_type*)data;
            awaiter->set_value_then_resume(std::make_tuple(make_ec(rc), std::move(dummy)));
        };

        awaiter_type awaiter;
        auto r = zoo_awget(zh_, path.data(), nullptr, nullptr, cb, &awaiter);

        if (r != ZOK) {
            throw std::runtime_error(std::string("zoo_awget error: ") + zerror(r));
        }
        co_return co_await awaiter;
    }

    coro::coro_task<std::tuple<std::error_code, std::vector<std::string>>>
        async_get_sub_path(std::string_view path) {
        using awaiter_type = coro::value_awaiter<
            std::tuple<std::error_code, std::vector<std::string>>>;
        auto completion = [](int rc, const struct String_vector* strings,
            const struct Stat*, const void* data) {
            std::vector<std::string> children_path;
            if (strings) {
                size_t count = strings->count;
                children_path.reserve(count);
                for (size_t i = 0; i < count; ++i) {
                    children_path.emplace_back(std::string(strings->data[i]));
                }
            }
            auto awaiter = (awaiter_type*)data;
            awaiter->set_value_then_resume(std::make_tuple(make_ec(rc), std::move(children_path)));
        };

        awaiter_type awaiter;
        auto r = zoo_awget_children2(zh_, path.data(), nullptr, nullptr, completion, &awaiter);

        if (r != ZOK) {
            throw std::runtime_error(std::string("zoo_awget_children2 error: ") + zerror(r));
        }
        co_return co_await awaiter;
    }

    coro::coro_task<std::error_code>
        async_remove_watches(std::string_view path, int watch_type) {
        using awaiter_type = coro::value_awaiter<std::error_code>;
        void_completion_t completion = [](int rc, const void* data) {
            auto awaiter = (awaiter_type*)data;
            awaiter->set_value_then_resume(make_ec(rc));
        };

        int r = 0;
        std::string p(path);
        if ((watch_type & 0x01) != 0) { //path
            awaiter_type awaiter;
            r = zoo_aremove_all_watches(zh_, p.data(),
                ZWATCHTYPE_DATA, 0, (void_completion_t*)completion, &awaiter);

            if (r != ZOK) {
                throw std::runtime_error(std::string("zoo_aremove_all_watches error: ") + zerror(r));
            }
            co_return co_await awaiter;
        }

        if ((watch_type & 0x02) != 0) { //sub_path
            awaiter_type awaiter;
            r = zoo_aremove_all_watches(zh_, p.data(),
                ZWATCHTYPE_CHILD, 0, (void_completion_t*)completion, &awaiter);

            if (r != ZOK) {
                throw std::runtime_error(std::string("zoo_aremove_all_watches error: ") + zerror(r));
            }
            auto ec = co_await awaiter;
            if (ec) {
                co_return ec;
            }

            auto [_, sub_paths] = co_await async_get_sub_path(p);
            for (auto& sub : sub_paths) {
                awaiter_type aw;
                auto full = std::string(p) + "/" + sub;
                r = zoo_aremove_all_watches(zh_, full.data(),
                    ZWATCHTYPE_DATA, 0, (void_completion_t*)completion, &aw);

                if (r != ZOK) {
                    throw std::runtime_error(std::string("zoo_aremove_all_watches error: ") + zerror(r));
                }
                auto e = co_await aw;
                if (e) {
                    co_return e;
                }
            }
        }
    }

private:
    void detect_expired_session() {
        detect_expired_thread_ = std::thread([this]() {
            while (run_) {
                auto interval = session_timeout_ms_ / 5;
                std::this_thread::sleep_for(std::chrono::milliseconds(
                    interval < 3000 ? interval : 3000));
                if (need_detect_) {
                    // make network interaction
                  zoo_get(zh_, "/zookeeper", 0, nullptr, 0, nullptr);
                }
                if (!is_conntected_) {
                    expired_cb_();
                }
            }
        });
    }

    void connect_server(const char* cert = "") {
        auto watcher = [](zhandle_t*, int type, int state, const char*, void* watcherCtx) {
            auto self = (cppzk*)(watcherCtx);
            if (state == ZOO_CONNECTED_STATE && type == ZOO_SESSION_EVENT) {
                self->is_conntected_ = true;
                return;
            }
            if (state == ZOO_EXPIRED_SESSION_STATE && type == ZOO_SESSION_EVENT) {
                self->need_detect_ = false;
                self->is_conntected_ = false;
            }
        };
#ifdef HAVE_OPENSSL_H
        zh_ = zookeeper_init_ssl(hosts_.c_str(), cert,
                                 watcher, session_timeout_ms_, nullptr, this, 0);
#else
        zh_ = zookeeper_init(hosts_.c_str(), watcher, session_timeout_ms_, nullptr, this, 0);
        (void)cert;
#endif
        if (!zh_) {
            throw std::runtime_error("zookeeper_init error");
        }

        while (!is_conntected_ && run_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        need_detect_ = true;
        session_timeout_ms_ = zoo_recv_timeout(zh_);  // get the actual value
    }

    static std::error_code make_ec(int err) {
        return { err, zk::category() };
    }

protected:
    bool is_no_error(zk_error err) {
        return err == zk_error::zk_ok;
    }

    bool is_no_node(const std::error_code& err) {
        return err.value() == ZOO_ERRORS::ZNONODE;
    }

    bool is_dummy_event(zk_event eve) {
        return eve == zk_event::zk_dummy_event;
    }

    bool is_create_event(zk_event eve) {
        return eve == zk_event::zk_created_event;
    }

    bool is_delete_event(zk_event eve) {
        return eve == zk_event::zk_deleted_event;
    }

    bool is_changed_event(zk_event eve) {
        return eve == zk_event::zk_changed_event;
    }

    bool is_session_event(zk_event eve) {
        return eve == zk_event::zk_session_event;
    }

    bool is_child_event(zk_event eve) {
        return eve == zk_event::zk_child_event;
    }

    bool is_notwatching_event(zk_event eve) {
        return eve == zk_event::zk_notwatching_event;
    }

    auto get_persistent_mode() {
        return zk_create_mode::zk_persistent;
    }

    auto get_create_mode(int mode) {
        return static_cast<zk_create_mode>(mode);
    }

    std::deque<std::string> split_path(std::string_view path) {
        auto c = std::count(path.begin(), path.end(), '/');
        std::deque<std::string> split_path;
        if (c == 0) {
            throw std::invalid_argument("no / found in path");
        }
        if (c == 1) {
            split_path.emplace_front(path);
            return split_path;
        }

        auto src_path = path;
        auto pos = path.find_last_of('/');
        while (pos != std::string_view::npos) {
            path = path.substr(0, pos);
            split_path.emplace_front(path);
            pos = path.find_last_of('/');
        }
        split_path.pop_front();
        split_path.emplace_back(src_path);
        return split_path;
    }

    template <size_t PathDepth>
    constexpr decltype(auto) split_path(std::string_view path) {
        if constexpr (PathDepth == 1) {
            std::array<std::string_view, PathDepth> self_path;
            self_path[0] = path;
            return self_path;
        }
        else {
            std::array<std::string_view, PathDepth> p;
            auto end = path.find_last_of('/');
            size_t i = 0;
            while (end != std::string_view::npos) {
                p[i] = path.substr(0, end);
                path = path.substr(0, end);
                end = path.find_last_of('/');
                i++;
            }

            std::array<std::string_view, PathDepth - 1> sp_path;
            for (size_t j = 0; j < PathDepth - 1; ++j) {
                sp_path[j] = p[j];
            }
            return sp_path;
        }
    }

    coro::coro_task<>
        async_recursive_get_sub_path(std::string_view path, std::deque<std::string>& sub_paths) {
        auto p = std::string(path);
        auto [ec, children] = co_await async_get_sub_path(p);
        if (ec) {
            co_return;
        }
        for (auto& child : children) {
            auto full = p + "/" + child;
            co_await async_recursive_get_sub_path(full, sub_paths);
            sub_paths.emplace_back(std::move(full));
        }
    }

};
}  // namespace zk