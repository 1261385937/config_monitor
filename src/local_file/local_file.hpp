#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <optional>
#include <chrono>
#include "../awaitable_transform.hpp"
#include "local_file_declare.hpp"

namespace loc {

class loc_file {
public:
    using watch_exists_callback = std::function<void(file_event)>;
    using watch_hildren_callback = std::function<void(file_event)>;

    struct existed_status {
        bool existed;
        size_t modify_time;
    };
    using last_existed_status_type = std::unordered_map<std::string, existed_status>;
    using monitor_exist_path_type = std::unordered_map<std::string, watch_exists_callback>;
    using last_path_children_type = std::unordered_map<std::string, std::deque<std::string>>;
    using monitor_sub_path_type = std::unordered_map<std::string, watch_hildren_callback>;

private:
    last_existed_status_type last_existed_status_;
    monitor_exist_path_type monitor_exist_path_;

    last_path_children_type last_path_children_;
    monitor_sub_path_type monitor_sub_path_;

    std::thread task_thread_;
    std::mutex task_mtx_;
    std::condition_variable task_cv_;
    std::deque<std::function<void()>> task_queue_;
    std::atomic<bool> run_ = true;

public:
    void initialize(int frequency_ms = 1000) {
        task_thread_ = std::thread([this, frequency_ms]() {
            while (run_) {
                std::unique_lock lock(task_mtx_);
                task_cv_.wait_for(lock, std::chrono::milliseconds(frequency_ms), [this]() {
                    return !run_ || !task_queue_.empty();
                });
                auto last_existed_status = last_existed_status_;
                auto monitor_exist_path = monitor_exist_path_;

                auto last_path_children = last_path_children_;
                auto monitor_sub_path = monitor_sub_path_;

                auto task_queue = std::move(task_queue_);
                lock.unlock();

                // deal task
                for (auto& task : task_queue) {
                    task();
                }

                handle_monitor_exist(std::move(last_existed_status), std::move(monitor_exist_path));
                handle_monitor_sub(std::move(last_path_children), std::move(monitor_sub_path));
            }
        });
    }

    ~loc_file() {
        run_ = false;
        task_cv_.notify_one();
        if (task_thread_.joinable()) {
            task_thread_.join();
        }
    }

    coro::coro_task<std::tuple<std::error_code, std::string>>
        async_create_path(std::string_view path, std::optional<std::string> value,
        file_create_mode mode, int64_t ttl = -1) {
        bool enable_ttl = false;
        if (mode == file_create_mode::persistent_sequential_with_ttl ||
            mode == file_create_mode::persistent_with_ttl) {
            enable_ttl = true;
        }
        if (enable_ttl && ttl < 0) {
            throw std::runtime_error("enable_ttl, ttl must > 0");
        }

        using awaiter_type = coro::value_awaiter<std::tuple<std::error_code, std::string>>;
        awaiter_type awaiter;
        add_task([this, p = std::string(path), val = value, &awaiter]() mutable {
            std::filesystem::path path(p);
            std::error_code ec;
            auto exist = std::filesystem::exists(path, ec);
            if (ec) {
                awaiter.set_value_then_resume(std::make_tuple(ec, std::string{}));
                return;
            }
            if (exist) {
                awaiter.set_value_then_resume(
                    std::make_tuple(make_ec(file_err::already_exist), std::string{}));
                return;
            }

            auto parent_path = path.parent_path();
            auto file_name = path.filename();
            std::filesystem::create_directories(parent_path, ec);
            if (ec) {
                awaiter.set_value_then_resume(std::make_tuple(ec, std::string{}));
                return;
            }
            auto err = set_file_value(p, val.has_value() ? val.value() : "", false);
            awaiter.set_value_then_resume(std::make_tuple(err, std::move(p)));
        });
        co_return co_await awaiter;
    }

    coro::coro_task<std::error_code>
        async_delete_path(std::string_view path) {
        using awaiter_type = coro::value_awaiter<std::error_code>;
        awaiter_type awaiter;
        add_task([p = std::string(path), &awaiter]() {
            std::error_code ec;
            auto exist = std::filesystem::exists(p);
            if (!exist) {
                awaiter.set_value_then_resume(make_ec(file_err::not_exist));
                return;
            }

            std::filesystem::remove_all(p, ec);
            if (ec) {
                awaiter.set_value_then_resume(ec);
                return;
            }
            awaiter.set_value_then_resume(ec);
        });
        co_return co_await awaiter;
    }

    coro::coro_task<std::error_code>
        async_set_path_value(std::string_view path, std::string_view value) {
        using awaiter_type = coro::value_awaiter<std::error_code>;
        awaiter_type awaiter;
        add_task([this, p = std::string(path), v = std::string(value), &awaiter]() {
            auto err = set_file_value(p, v);
            awaiter.set_value_then_resume(err);
        });
        co_return co_await awaiter;
    }

    // [create/change/delete] event
    coro::coro_task<file_event>
        async_watch_exists_path(std::string_view path) {
        auto p = std::string(path);
        std::error_code ig;
        auto [_, time] = file_modify_time(p);
        auto exist = std::filesystem::exists(p, ig);
        using awaiter_type = coro::value_awaiter<file_event>;
        awaiter_type awaiter;

        {
            std::unique_lock lock(task_mtx_);
            monitor_exist_path_.emplace(p, [&awaiter](file_event eve) {
                awaiter.set_value_then_resume(eve);
            });
            last_existed_status_.emplace(std::move(p), existed_status{ exist, time });
        }
        co_return co_await awaiter;
    }

    // [child] event
    coro::coro_task<file_event>
        async_watch_sub_path(std::string_view path) {
        using awaiter_type = coro::value_awaiter<file_event>;
        awaiter_type awaiter;
        auto p = std::string(path);
        auto [_, sub_children] = get_path_children(path);

        {
            std::unique_lock lock(task_mtx_);
            monitor_sub_path_.emplace(p, [&awaiter](file_event eve) {
                awaiter.set_value_then_resume(eve);
            });
            last_path_children_.emplace(std::move(p), std::move(sub_children));
        }
        co_return co_await awaiter;
    }

    coro::coro_task<std::tuple<std::error_code, std::optional<std::string>>>
        async_get_path_value(std::string_view path) {
        using awaiter_type = coro::value_awaiter<
            std::tuple<std::error_code, std::optional<std::string>>>;
        awaiter_type awaiter;
        auto [_, timestamp] = file_modify_time(path);
        auto p = std::string(path);
        add_task([this, p = std::string(path), &awaiter]() {         
            auto [ec, val] = get_file_value(p);
            awaiter.set_value_then_resume(std::make_tuple(ec, std::move(val)));
        });
        co_return co_await awaiter;
    }

    coro::coro_task<std::tuple<std::error_code, std::deque<std::string>>>
        async_get_sub_path(std::string_view path) {
        auto p = std::string(path);
        using awaiter_type = coro::value_awaiter<
            std::tuple<std::error_code, std::deque<std::string>>>;
        awaiter_type awaiter;
        add_task([this, p = std::string(path), &awaiter]() {
            auto [ec, children] = get_path_children(p);
            awaiter.set_value_then_resume(std::make_tuple(ec, std::move(children)));
        });
        co_return co_await awaiter;
    }

    coro::coro_task<std::error_code>
        async_remove_watches(std::string_view path, int watch_type) {
        using awaiter_type = coro::value_awaiter<std::error_code>;
        awaiter_type awaiter;
        add_task([watch_type, this, p = std::string(path), &awaiter]() {
            if (watch_type == 0) { //path
                remove_monitor_exist_path(p);
            }

            if (watch_type == 1) { //sub-path
                remove_monitor_sub_path(p);
                auto [_, children] = get_path_children(p);
                for (auto& sub : children) {
                    remove_monitor_exist_path(std::string(p) + "/" + sub);
                }                  
            }
            awaiter.set_value_then_resume(make_ec(file_err::ok));
        });
        co_return co_await awaiter;
    }

protected:
    static std::error_code make_ec(file_err err) {
        return { static_cast<int>(err), loc::category() };
    }

    bool is_no_node(std::error_code ec) {
        return ec.value() == (int)file_err::not_exist;
    }

    bool is_create_event(file_event eve) {
        return eve == file_event::created_event;
    }

    bool is_delete_event(file_event eve) {
        return eve == file_event::deleted_event;
    }

    bool is_changed_event(file_event eve) {
        return eve == file_event::changed_event;
    }

    bool is_session_event(file_event) {
        return false;
    }

    bool is_notwatching_event(file_event) {
        return false;
    }

    auto get_persistent_mode() {
        return file_create_mode::persistent;
    }

    auto get_create_mode(int mode) {
        return static_cast<file_create_mode>(mode);
    }

private:
    template <typename Task>
    void add_task(Task&& task) {
        std::unique_lock lock(task_mtx_);
        task_queue_.emplace_back(std::move(task));
        lock.unlock();
        task_cv_.notify_one();
    }

    std::pair<std::error_code, std::string> get_file_value(std::string_view path) {
        std::string value;
        auto file = fopen(path.data(), "rb");
        if (file == nullptr) {
            return { make_ec(file_err::not_exist), value };
        }

        auto size = std::filesystem::file_size(path);
        value.resize(size, 0);
        fread(value.data(), value.length(), 1, file);
        fclose(file);
        return { make_ec(file_err::ok), value };
    }

    std::error_code set_file_value(
        std::string_view path, std::string_view value, bool need_existed = true) {
        if (need_existed) {
            std::error_code ec;
            auto existed = std::filesystem::exists(path, ec);
            if (ec) {
                return ec;
            }
            if (!existed) {
                return make_ec(file_err::not_exist);
            }
        }
        
        auto file = fopen(path.data(), "wb");
        if (file == nullptr) {
            return make_ec(file_err::not_exist);
        }
        fwrite(value.data(), value.length(), 1, file);
        fclose(file);
        return make_ec(file_err::ok);
    }

    std::pair<std::error_code, size_t> file_modify_time(std::string_view path) {
        namespace sc = std::chrono;
        std::error_code ec;
        auto time = std::filesystem::last_write_time(path, ec);
        if (ec) {
            return { ec, 0 };
        }
        auto timestamp = sc::duration_cast<sc::nanoseconds>(time.time_since_epoch()).count();
        return { ec, timestamp };
    }

    void remove_monitor_exist_path(const std::string& path) {     
        std::unique_lock lock(task_mtx_);      
        last_existed_status_.erase(path);
        monitor_exist_path_.erase(path);
    }

    void remove_monitor_sub_path(const std::string& path) {   
        std::unique_lock lock(task_mtx_);       
        last_path_children_.erase(path);
        monitor_sub_path_.erase(path);
    }

    std::pair<std::error_code, std::deque<std::string>> get_path_children(std::string_view path) {
        std::deque<std::string> file;
        namespace fs = std::filesystem;
        std::error_code ec;
        auto dirs = fs::directory_iterator{ fs::path(path), ec };
        if (ec) {
            return { ec, file };
        }

        for (auto& dir_entry : dirs) {
            if (dir_entry.is_regular_file()) {
                file.emplace_back(dir_entry.path().filename().string());
                continue;
            }
        }
        return { ec, file };
    }

    void handle_monitor_exist(last_existed_status_type&& last_existed_status,
                              monitor_exist_path_type&& monitor_exist_path) {
        for (auto& [path, last_status] : last_existed_status) {
            std::error_code ec;
            auto this_existed = std::filesystem::exists(path, ec);
            if (ec) {
                continue;
            }

            //last existed status as same as this time.
            if (last_status.existed == this_existed) {
                auto [_, this_modify_time] = file_modify_time(path);
                if (!_ && (this_modify_time != last_status.modify_time)) {
                    remove_monitor_exist_path(path);
                    monitor_exist_path[path](file_event::changed_event);              
                }
                continue;
            }

            //last status is existed, this time not existed.
            if (last_status.existed == true) {
                remove_monitor_exist_path(path);
                monitor_exist_path[path](file_event::deleted_event); 
                continue;
            }

            //last status is not existed, this time existed.
            if (last_status.existed == false) {
                remove_monitor_exist_path(path);
                monitor_exist_path[path](file_event::created_event);               
            }
        }
    }

    void handle_monitor_sub(last_path_children_type last_sub_path,
        monitor_sub_path_type monitor_sub_path) {
        for (auto& [path, last_sub_children] : last_sub_path) {
            auto [ec, this_sub_children] = get_path_children(path);
            if (ec || (last_sub_children == this_sub_children)) {
                continue;
            }
            remove_monitor_sub_path(path);
            monitor_sub_path[path](file_event::child_event);
        }
    }
};

}  // namespace loc+