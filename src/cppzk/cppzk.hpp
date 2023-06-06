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
#include "cppzk_define.h"

namespace zk {
class cppzk {
private:
    zhandle_t* zh_{};
    std::string hosts_;
    int unused_flags_ = 0;

    std::mutex mtx_;
    std::unordered_map<uint64_t, std::shared_ptr<user_data>> releaser_;

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

    template <bool Advanced = true>
    zk_error create_path(std::string_view path, std::string_view value, zk_create_mode mode,
                         create_callback ccb, int64_t ttl = -1,
                         zk_acl acl = zk_acl::zk_open_acl_unsafe) {
        auto data = new create_callback{ std::move(ccb) };
        bool enable_ttl = false;
        if (mode == zk_create_mode::zk_persistent_sequential_with_ttl ||
            mode == zk_create_mode::zk_persistent_with_ttl) {
            enable_ttl = true;
        }
        if (enable_ttl && ttl < 0) {
            throw std::runtime_error("enable_ttl, ttl must > 0");
        }

        if constexpr (Advanced) {
            auto sp_path = split_path(path);
            std::string new_path;
            for (size_t i = 0; i < sp_path.size() - 1; ++i) {
                zoo_create2_ttl(zh_, sp_path[i].data(), nullptr, 0,
                                &acl_mapping[acl], (int)mode, ttl, nullptr, 0, nullptr);
            }
        }

        auto value_ptr = value.empty() ? nullptr : value.data();
        auto value_len = value.empty() ? 0 : (int)value.length();
        auto r = zoo_acreate2_ttl(
            zh_, path.data(), value_ptr, value_len, &acl_mapping[acl], (int)mode, ttl,
            [](int rc, const char* string, const struct Stat*, const void* data) {
            auto cb = (create_callback*)data;
            if ((*cb)) {
                (*cb)((zk_error)rc, string == nullptr ? std::string{} : std::string(string));
            }
            delete cb;
        }, data);

        if (r != ZOK) {
            printf("create_path error: %s\n", zerror(r));
        }
        return (zk_error)r;
    }

    template <bool Advanced = true>
    zk_error delete_path(std::string_view path, delete_callback dcb) {
        if constexpr (Advanced) {
            std::deque<std::string> sub_paths;
            recursive_get_sub_path(path, sub_paths);
            for (auto& sub : sub_paths) {
                zoo_delete(zh_, sub.data(), -1);
            }
        }

        auto data = new delete_callback{ std::move(dcb) };
        auto r = zoo_adelete(zh_, path.data(), -1, [](int rc, const void* data) {
            auto cb = (delete_callback*)data;
            if ((*cb)) {
                (*cb)((zk_error)rc);
            }
            delete cb;
        }, data);

        if (r != ZOK) {
            printf("delete_path error: %s\n", zerror(r));
        }
        return (zk_error)r;
    }

    zk_error set_path_value(std::string_view path, std::string_view value, set_callback scb) {
        auto data = new set_callback{ std::move(scb) };
        auto r = zoo_aset(
            zh_, path.data(), value.data(), (int)value.length(), -1,
            [](int rc, const struct Stat*, const void* data) {
            auto cb = (set_callback*)data;
            if ((*cb)) {
                (*cb)((zk_error)rc);
            }
            delete cb;
        }, data);

        if (r != ZOK) {
            printf("set_path_value error: %s\n", zerror(r));
        }
        return (zk_error)r;
    }

    // [create/delete/changed] event just for current path
    template <bool Advanced = true>
    zk_error exists_path(std::string_view path, exists_callback ecb) {
        auto wfn = [](zhandle_t*, int eve, int, const char* path, void* watcherCtx) {
            auto eud = (exists_userdata*)watcherCtx;
            if (eve == ZOO_SESSION_EVENT) {
                return;  // deal in zookeeper_init watcher
            }
            if (eve == ZOO_NOTWATCHING_EVENT) {
                std::lock_guard<std::mutex> lock(eud->self->mtx_);
                eud->self->releaser_.erase((uint64_t)watcherCtx);
                return;
            }
            eud->eve = (zk_event)eve;
            eud->path = path;
            auto r = zoo_awexists(eud->self->zh_, path,
                                  eud->wfn, watcherCtx, eud->completion, watcherCtx);
            if (r != ZOK) {
                printf("exists_path error: %s\n", zerror(r));
            }
        };
        auto exists_completion = [](int rc, const struct Stat*, const void* data) {
            auto d = (exists_userdata*)data;
            if (d->cb) {
                d->cb((zk_error)rc, d->eve);
            }
            if constexpr (!Advanced) {
                delete d;
            }
        };

        int r = 0;
        if constexpr (Advanced) {
            auto data = std::make_shared<exists_userdata>(
                wfn, exists_completion, std::move(ecb), this, path);
            r = zoo_awexists(zh_, path.data(), wfn, data.get(), exists_completion, data.get());
            std::lock_guard<std::mutex> lock(mtx_);
            releaser_.emplace((uint64_t)data.get(), std::move(data));
        }
        else {
            auto data = new exists_userdata(wfn, exists_completion, std::move(ecb), this, path);
            r = zoo_awexists(zh_, path.data(), nullptr, data, exists_completion, data);
        }

        if (r != ZOK) {
            printf("exists_path error: %s\n", zerror(r));
        }
        return (zk_error)r;
    }

    // [changed] event just for current path, if the path exists all the time
    template <bool Advanced = true>
    zk_error get_path_value(std::string_view path, get_callback gcb) {
        auto wfn = [](zhandle_t*, int eve, int, const char* path, void* watcherCtx) {
            auto d = static_cast<wget_userdata*>(watcherCtx);
            if (eve == ZOO_SESSION_EVENT) {
                return;  // deal in zookeeper_init watcher
            }
            if (eve == ZOO_NOTWATCHING_EVENT || eve == ZOO_DELETED_EVENT) {
                if (eve == ZOO_DELETED_EVENT && d->cb) {
                    d->cb(zk_error::zk_no_node, std::optional<std::string>{});
                }
                std::lock_guard<std::mutex> lock(d->self->mtx_);
                d->self->releaser_.erase((uint64_t)watcherCtx);
                return;
            }
            d->path = path;
            auto r = zoo_awget(d->self->zh_, path, d->wfn, watcherCtx, d->completion, watcherCtx);
            if (r != ZOK) {
                printf("get_path_value error: %s\n", zerror(r));
            }
        };
        auto cb = [](int rc, const char* value, int value_len,
                     const struct Stat*, const void* data) {
            auto d = (wget_userdata*)data;
            if (d->cb) {
                std::optional<std::string> dummy;
                d->cb((zk_error)rc, value ? std::string(value, value_len) : std::move(dummy));
            }
            if constexpr (!Advanced) {
                delete d;
            }
        };

        int r = 0;
        if constexpr (Advanced) {
            auto data = std::make_shared<wget_userdata>(wfn, cb, std::move(gcb), this, path);
            r = zoo_awget(zh_, path.data(), wfn, data.get(), cb, data.get());
            std::lock_guard<std::mutex> lock(mtx_);
            releaser_.emplace((uint64_t)data.get(), std::move(data));
        }
        else {
            auto data = new wget_userdata(wfn, cb, std::move(gcb), this, path);
            r = zoo_awget(zh_, path.data(), nullptr, data, cb, data);
        }

        if (r != ZOK) {
            printf("get_path_value error: %s\n", zerror(r));
        }
        return (zk_error)r;
    }

    // [create/delete] sub path event just for current path, if the path exists all the time
    template <bool Advanced = true>
    zk_error get_sub_path(std::string_view path, get_children_callback gccb) {
        auto wfn = [](zhandle_t*, int eve, int, const char* path, void* watcherCtx) {
            auto d = static_cast<get_children_userdata*>(watcherCtx);
            if (eve == ZOO_SESSION_EVENT) {
                return;  // deal in zookeeper_init watcher
            }
            if (eve == ZOO_NOTWATCHING_EVENT || eve == ZOO_DELETED_EVENT) {
                if (eve == ZOO_DELETED_EVENT && d->cb) {
                    d->cb(zk_error::zk_no_node, zk_event::zk_dummy_event, {});
                }
                std::lock_guard<std::mutex> lock(d->self->mtx_);
                d->self->releaser_.erase((uint64_t)watcherCtx);
                return;
            }
            d->eve = (zk_event)eve;
            d->path = path;
            auto r = zoo_awget_children2(d->self->zh_, path, d->wfn,
                                         watcherCtx, d->children_completion, watcherCtx);
            if (r != ZOK) {
                printf("get_sub_path error: %s\n", zerror(r));
            }
            return;
        };
        auto children_completion = [](int rc, const struct String_vector* strings,
                                      const struct Stat*, const void* data) {
            auto d = (get_children_userdata*)data;
            if (d->cb) {
                std::vector<std::string> children_path;
                if (strings) {
                    size_t count = strings->count;
                    children_path.reserve(count);
                    for (size_t i = 0; i < count; ++i) {
                        children_path.emplace_back(std::string(strings->data[i]));
                    }
                }
                d->cb((zk_error)rc, d->eve, std::move(children_path));
            }
            if constexpr (!Advanced) {
                delete d;
            }
        };

        int r = 0;
        if constexpr (Advanced) {
            auto data = std::make_shared<get_children_userdata>(
                wfn, children_completion, std::move(gccb), this, path);
            r = zoo_awget_children2(zh_, path.data(), wfn, data.get(),
                                    children_completion, data.get());
            std::lock_guard<std::mutex> lock(mtx_);
            releaser_.emplace((uint64_t)data.get(), std::move(data));
        }
        else {
            auto data = new get_children_userdata(
                wfn, children_completion, std::move(gccb), this, path);
            r = zoo_awget_children2(zh_, path.data(), nullptr, data, children_completion, data);
        }

        if (r != ZOK) {
            printf("get_sub_path error: %s\n", zerror(r));
        }
        return (zk_error)r;
    }

    zk_error remove_watches(std::string_view path, int watch_type, delete_callback cb) {
        void_completion_t completion = [](int rc, const void* data) {
            auto cb = (delete_callback*)data;
            if ((*cb)) {
                (*cb)((zk_error)rc);
            }
            delete cb;
        };
        auto data = new delete_callback(std::move(cb));
        int r = 0;

        if (watch_type == 1) { //sub_path
            std::promise<std::vector<std::string>> pro;
            get_sub_path<false>(path, [&pro](zk_error, zk_event, std::vector<std::string>&& children) {
                pro.set_value(std::move(children));
            });
            auto sub_paths = pro.get_future().get();

            for (auto& sub : sub_paths) {
                auto full = std::string(path) + "/" + sub;
                zoo_remove_all_watches(zh_, full.data(), ZooWatcherType::ZWATCHTYPE_DATA, 0);
            }
            r = zoo_aremove_all_watches(zh_, path.data(), ZWATCHTYPE_ANY, 0,
                                        (void_completion_t*)completion, data);
        }
        else {
            r = zoo_aremove_all_watches(zh_, path.data(), ZWATCHTYPE_DATA, 0,
                                        (void_completion_t*)completion, data);
        }

        if (r != ZOK) {
            printf("remove_watches error: %s\n", zerror(r));
        }
        return (zk_error)r;
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
                    get_path_value<false>("/zookeeper", nullptr);
                }
                if (!is_conntected_) {
                    std::unique_lock<std::mutex> lock(mtx_);
                    releaser_.clear();
                    lock.unlock();
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

protected:
    std::error_code make_error_code(zk_error err) {
        return { static_cast<int>(err), zk::category() };
    }

    bool is_no_error(zk_error err) {
        return err == zk_error::zk_ok;
    }

    bool is_no_node(zk_error err) {
        return err == zk_error::zk_no_node;
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

    void recursive_get_sub_path(std::string_view path, std::deque<std::string>& sub_paths) {
        std::promise<std::vector<std::string>> pro;
        get_sub_path<false>(path, [&pro](zk_error, zk_event, std::vector<std::string>&& children) {
            pro.set_value(std::move(children));
        });
        auto children = pro.get_future().get();

        auto p = std::string(path);
        for (auto& child : children) {
            auto full = p + "/" + child;
            recursive_get_sub_path(full, sub_paths);
            sub_paths.emplace_back(std::move(full));
        }
    }


};
}  // namespace zk