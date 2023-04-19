#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include "local_file_declare.hpp"

namespace loc {

class local_file {
public:
    using create_callback = std::function<void(file_error, std::string&&)>;
    using delete_callback = std::function<void(file_error)>;
    using set_callback = std::function<void(file_error)>;
    using exists_callback = std::function<void(file_error, file_event)>;
    using get_callback = std::function<void(file_error, std::optional<std::string>&&)>;
    using get_children_callback = std::function<void(file_error, file_event, std::vector<std::string>&&)>;

private:
    int monitor_frequency_ms_;
    std::thread monitor_thread_;

    std::mutex monitor_mtx_;
    std::unordered_map<std::string, exists_callback> monitor_exist_path_;
    std::unordered_map<std::string, get_callback> monitor_get_path_;
    std::unordered_map<std::string, get_children_callback> monitor_sub_path_;

    std::thread task_thread_;
    std::mutex task_mtx_;
    std::condition_variable task_cv_;
    std::deque<std::function<void()>> task_queue_;
    std::atomic<bool> run_ = true;

public:
    void initialize(int monitor_frequency_ms) {
        monitor_frequency_ms_ = monitor_frequency_ms;
        task_thread_ = std::thread([this]() {
            while (run_) {
                std::unique_lock lock(task_mtx_);
                task_cv_.wait(lock, [this]() { return !task_queue_.empty() || !run_; });
                auto task_queue = std::move(task_queue_);
                lock.unlock();
                // deal task
                for (auto&& task : task_queue) {
                    task();
                }
            }
        });

        monitor_thread_ = std::thread([this]() {
            while (run_) {
                std::unique_lock lock(monitor_mtx_);
                auto monitor_exist_path = monitor_exist_path_;
                lock.unlock();

                task_cv_.notify_one();

                std::unique_lock lock1(monitor_mtx_);
                auto monitor_get_path = monitor_get_path_;
                lock.unlock();
                task_cv_.notify_one();

                std::unique_lock lock2(monitor_mtx_);
                auto monitor_sub_path = monitor_sub_path_;
                lock.unlock();
                task_cv_.notify_one();

                std::this_thread::sleep_for(std::chrono::milliseconds(monitor_frequency_ms_));
            }
        });
    }

    ~local_file() {
        run_ = false;
        task_cv_.notify_one();
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
        if (task_thread_.joinable()) {
            task_thread_.join();
        }
    }

    void create_path(std::string_view path, std::string_view value, file_create_mode mode, create_callback ccb,
                     int64_t ttl = -1) {
        bool enable_ttl = false;
        if (mode == file_create_mode::file_persistent_sequential_with_ttl ||
            mode == file_create_mode::file_persistent_with_ttl) {
            enable_ttl = true;
        }
        if (enable_ttl && ttl < 0) {
            throw std::runtime_error("enable_ttl, ttl must > 0");
        }
        add_task([p = std::string(path), v = std::string(value), callback = std::move(ccb)]() mutable {
            std::filesystem::path path(p);
            auto exist = std::filesystem::exists(path);
            if (exist) {
                callback(file_error::file_exist, {});
                return;
            }

            auto parent_path = path.parent_path();
            auto file_name = path.filename();
            std::filesystem::create_directories(parent_path);
            auto file = fopen(file_name.string().data(), "wb+");
            if (!v.empty()) {
                fwrite(v.data(), v.length(), 1, file);
            }
            fclose(file);
            callback(file_error::file_ok, std::move(p));
        });
    }

    void delete_path(std::string_view path, delete_callback dcb) {
        add_task([p = std::string(path), callback = std::move(dcb)]() {
            std::filesystem::remove_all(p);
            callback(file_error::file_ok);
        });
    }

    void set_path_value(std::string_view path, std::string_view value, set_callback scb) {
        add_task([p = std::string(path), v = std::string(value), callback = std::move(scb)]() {
            auto exist = std::filesystem::exists(p);
            if (!exist) {
                callback(file_error::file_not_exist);
                return;
            }
            auto file = fopen(p.data(), "wb+");
            fwrite(v.data(), v.length(), 1, file);
            fclose(file);
            callback(file_error::file_ok);
        });
    }

    // [create/delete/changed] event just for current path
    void exists_path(std::string_view path, exists_callback ecb) {
        std::unique_lock lock(monitor_mtx_);
        monitor_exist_path_.emplace(std::string(path), std::move(ecb));
    }

    // [changed] event just for current path, if the path exists all the time
    void get_path_value(std::string_view path, get_callback gcb) {
        std::unique_lock lock(monitor_mtx_);
        monitor_get_path_.emplace(std::string(path), std::move(gcb));
    }

    // [create/delete] sub path event just for current path, if the path exists all the time
    void get_sub_path(std::string_view path, get_children_callback gccb) {
        std::unique_lock lock(monitor_mtx_);
        monitor_sub_path_.emplace(std::string(path), std::move(gccb));
    }

protected:
    bool is_no_error(file_error err) {
        return err == file_error::file_ok;
    }

    bool is_not_exist(file_error err) {
        return err == file_error::file_not_exist;
    }

    bool is_dummy_event(file_event eve) {
        return eve == file_event::file_dummy_event;
    }

    bool is_create_event(file_event eve) {
        return eve == file_event::file_created_event;
    }

    bool is_delete_event(file_event eve) {
        return eve == file_event::file_deleted_event;
    }

    auto get_persistent_mode() {
        return file_create_mode::file_persistent;
    }

    auto get_create_mode(bool is_ephemeral, bool is_sequential) {
        file_create_mode create_mode;
        if (is_ephemeral) {
            create_mode =
                is_sequential ? file_create_mode::file_ephemeral_sequential : file_create_mode::file_ephemeral;
        }
        else {
            create_mode =
                is_sequential ? file_create_mode::file_persistent_sequential : file_create_mode::file_persistent;
        }
        return create_mode;
    }

private:
    template <typename Task>
    void add_task(Task&& task) {
        std::unique_lock lock(task_mtx_);
        task_queue_.emplace_back(std::move(task));
        lock.unlock();
        task_cv_.notify_one();
    }
};

}  // namespace loc