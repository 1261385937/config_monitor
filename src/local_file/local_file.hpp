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
#include "local_file_declare.hpp"

namespace loc {

class loc_file {
public:
    using create_callback = std::function<void(file_error, std::string&&)>;
    using delete_callback = std::function<void(file_error)>;
    using set_callback = std::function<void(file_error)>;
    using exists_callback = std::function<void(file_error, file_event)>;
    using get_callback = std::function<void(file_error, std::optional<std::string>&&)>;
    using get_children_callback = std::function<void(file_error, file_event, std::deque<std::string>&&)>;

    using last_existed_status_type = std::unordered_map<std::string, bool>;
    using monitor_exist_path_type = std::unordered_map<std::string, exists_callback>;
    using last_changed_time_type = std::unordered_map<std::string, size_t>;
    using monitor_get_path_type = std::unordered_map<std::string, get_callback>;
    using last_path_children_type = std::unordered_map<std::string, std::deque<std::string>>;
    using monitor_sub_path_type = std::unordered_map<std::string, get_children_callback>;

private:
    last_existed_status_type last_existed_status_;
    monitor_exist_path_type monitor_exist_path_;

    last_changed_time_type last_changed_time_;
    monitor_get_path_type monitor_get_path_;

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
                auto task_queue = std::move(task_queue_);
                auto last_existed_status = last_existed_status_;
                auto monitor_exist_path = monitor_exist_path_;

                auto last_changed_time = last_changed_time_;
                auto monitor_get_path = monitor_get_path_;

                auto last_path_children = last_path_children_;
                auto monitor_sub_path = monitor_sub_path_;
                lock.unlock();

                // deal task
                for (auto& task : task_queue) {
                    task();
                }

                handle_monitor_exist(std::move(last_existed_status), std::move(monitor_exist_path));
                handle_monitor_get(std::move(last_changed_time), std::move(monitor_get_path));
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

    void create_path(std::string_view path, std::string_view value, file_create_mode mode,
                     create_callback ccb, int64_t ttl = -1) {
        bool enable_ttl = false;
        if (mode == file_create_mode::persistent_sequential_with_ttl ||
            mode == file_create_mode::persistent_with_ttl) {
            enable_ttl = true;
        }
        if (enable_ttl && ttl < 0) {
            throw std::runtime_error("enable_ttl, ttl must > 0");
        }
        add_task([this, p = std::string(path), v = std::string(value), cb = std::move(ccb)]() mutable {
            std::filesystem::path path(p);
            auto exist = std::filesystem::exists(path);
            if (exist) {
                cb(file_error::already_exist, {});
                return;
            }

            auto parent_path = path.parent_path();
            auto file_name = path.filename();
            std::filesystem::create_directories(parent_path);
            auto err = set_file_value(p, v);
            cb(err, std::move(p));
        });
    }

    void delete_path(std::string_view path, delete_callback dcb) {
        add_task([p = std::string(path), callback = std::move(dcb)]() {
            std::filesystem::remove_all(p);
            callback(file_error::ok);
        });
    }

    void set_path_value(std::string_view path, std::string_view value, set_callback scb) {
        add_task([this, p = std::string(path), v = std::string(value), callback = std::move(scb)]() {
            auto exist = std::filesystem::exists(p);
            if (!exist) {
                callback(file_error::not_exist);
                return;
            }

            auto err = set_file_value(p, v);
            callback(err);
        });
    }

    // [create/delete/changed] event just for current path
    template <bool Advanced = true>
    void exists_path(std::string_view path, exists_callback ecb) {
        auto existed = std::filesystem::exists(path);
        if (!existed) {
            add_task([ecb]() { ecb(file_error::not_exist, file_event::dummy_event); });
        }
        else {
            add_task([ecb]() { ecb(file_error::ok, file_event::dummy_event); });
        }

        if constexpr (Advanced) {
            auto p = std::string(path);
            std::unique_lock lock(task_mtx_);
            monitor_exist_path_.emplace(p, std::move(ecb));
            last_existed_status_.emplace(std::move(p), existed);
        }
    }

    // [changed] event just for current path, if the path exists all the time
    template <bool Advanced = true>
    void get_path_value(std::string_view path, get_callback gcb) {
        auto [err, value] = get_file_value(path);
        if (err != file_error::ok) {
            return add_task([gcb]() { gcb(file_error::not_exist, {}); });
        }
        add_task([gcb, v = std::move(value)]() mutable { gcb(file_error::ok, std::move(v)); });

        if constexpr (Advanced) {
            auto [_, timestamp] = file_modify_time(path);
            auto p = std::string(path);
            std::unique_lock lock(task_mtx_);
            monitor_get_path_.emplace(p, std::move(gcb));
            last_changed_time_.emplace(std::move(p), timestamp);
        }
    }

    // [create/delete] sub path event just for current path, if the path exists all the time
    template <bool Advanced = true>
    void get_sub_path(std::string_view path, get_children_callback gccb) {
        auto existed = std::filesystem::exists(path);
        if (!existed) {
            return add_task([gccb]() { gccb(file_error::not_exist, file_event::dummy_event, {}); });
        }

        auto children = get_path_children(path);
        if constexpr (Advanced) {
            add_task([gccb, children]() mutable{
                gccb(file_error::ok, file_event::dummy_event, std::move(children));
            });
            auto p = std::string(path);
            std::unique_lock lock(task_mtx_);
            monitor_sub_path_.emplace(p, std::move(gccb));
            last_path_children_.emplace(std::move(p), std::move(children));
        }
        else {
            add_task([gccb, ch = std::move(children)]() mutable {
                gccb(file_error::ok, file_event::dummy_event, std::move(ch));
            });
        }
    }

protected:
    bool is_no_error(file_error err) {
        return err == file_error::ok;
    }

    bool is_no_node(file_error err) {
        return err == file_error::not_exist;
    }

    bool is_dummy_event(file_event eve) {
        return eve == file_event::dummy_event;
    }

    bool is_create_event(file_event eve) {
        return eve == file_event::created_event;
    }

    bool is_delete_event(file_event eve) {
        return eve == file_event::deleted_event;
    }

    auto get_persistent_mode() {
        return file_create_mode::persistent;
    }

    auto get_create_mode(int mode) {
        return static_cast<file_create_mode>(mode);
    }
    
    std::error_code make_error_code(file_error err) {
        return { static_cast<int>(err), loc::category() };
    }

private:
    template <typename Task>
    void add_task(Task&& task) {
        std::unique_lock lock(task_mtx_);
        task_queue_.emplace_back(std::move(task));
        lock.unlock();
        task_cv_.notify_one();
    }

    std::pair<file_error, std::string> get_file_value(std::string_view path) {
        std::string value;
        auto file = fopen(path.data(), "rb");
        if (file == nullptr) {
            return { file_error::not_exist, value };
        }

        auto size = std::filesystem::file_size(path);
        value.resize(size, 0);
        fread(value.data(), value.length(), 1, file);
        fclose(file);
        return { file_error::ok, value };
    }

    file_error set_file_value(std::string_view path, std::string_view value) {
        auto file = fopen(path.data(), "wb+");
        if (file == nullptr) {
            return file_error::not_exist;
        }
        fwrite(value.data(), value.length(), 1, file);
        fclose(file);
        return file_error::ok;
    }

    std::pair<file_error, size_t> file_modify_time(std::string_view path) {
        namespace sc = std::chrono;
        std::error_code ec;
        auto time = std::filesystem::last_write_time(path, ec);
        if (ec) {
            return { file_error::not_exist, 0 };
        }
        auto timestamp = sc::duration_cast<sc::nanoseconds>(time.time_since_epoch()).count();
        return { file_error::ok, timestamp };
    }

    void remove_monitor_get_path(const std::string& path) {
        std::unique_lock lock(task_mtx_);
        last_changed_time_.erase(path);
        monitor_get_path_.erase(path);
    }

    void update_last_existed_status(const std::string& path, bool status) {
        std::unique_lock lock(task_mtx_);
        last_existed_status_[path] = status;
    }

    void update_last_changed_time(const std::string& path, size_t timestamp) {
        std::unique_lock lock(task_mtx_);
        last_changed_time_[path] = timestamp;
    }

    std::deque<std::string> get_path_children(std::string_view path) {
        std::deque<std::string> file;
        namespace sfs = std::filesystem;
        for (auto& dir_entry : sfs::directory_iterator{ sfs::path(path) }) {
            if (dir_entry.is_regular_file()) {
                file.emplace_back(dir_entry.path().filename().string());
                continue;
            }
        }
        return file;
    }

    void updata_path_children(const std::string& path, std::deque<std::string> children) {
        std::unique_lock lock(task_mtx_);
        last_path_children_[path] = std::move(children);
    }

    void handle_monitor_exist(last_existed_status_type&& last_existed_status,
                              monitor_exist_path_type&& monitor_exist_path) {
        for (auto& [path, last_status] : last_existed_status) {
            auto this_status = std::filesystem::exists(path);
            if (last_status == this_status) {
                //Todo: here can check changed or not.  changed_event or dummy_event
                continue;
            }
            update_last_existed_status(path, this_status);

            //last status is existed, this time not existed.
            if (last_status == true) {
                monitor_exist_path[path](file_error::not_exist, file_event::deleted_event);
                continue;
            }
            //last status is not existed, this time existed.
            if (last_status == false) {
                monitor_exist_path[path](file_error::ok, file_event::created_event);
            }
        }
    }

    void handle_monitor_get(last_changed_time_type&& last_changed_time,
                            monitor_get_path_type&& monitor_get_path) {
        for (auto& [path, last_timestamp] : last_changed_time) {
            auto [_, this_timestamp] = file_modify_time(path);
            //nothing changed
            if (last_timestamp == this_timestamp) {
                continue;
            }

            //value changed
            auto [err, value] = get_file_value(path);
            if (err != file_error::ok) {
                //file removed
                monitor_get_path[path](file_error::not_exist, {});
                remove_monitor_get_path(path);
                continue;
            }
            monitor_get_path[path](file_error::ok, std::move(value));
            update_last_changed_time(path, this_timestamp);
        }
    }

    void handle_monitor_sub(last_path_children_type last_sub_path,
                            monitor_sub_path_type monitor_sub_path) {
        for (auto& [path, last_sub_children] : last_sub_path) {
            auto this_sub_children = get_path_children(path);
            if (last_sub_children == this_sub_children) {
                continue;
            }
            updata_path_children(path, this_sub_children);
            monitor_sub_path[path](file_error::ok,
                                   file_event::child_event, std::move(this_sub_children));
        }
    }
};

}  // namespace loc