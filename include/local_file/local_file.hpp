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
    std::unordered_map<std::string, bool> last_path_existed_;
    std::unordered_map<std::string, exists_callback> monitor_exist_path_;

    std::unordered_map<std::string, std::string> last_path_value_;
    std::unordered_map<std::string, get_callback> monitor_get_path_;

    std::unordered_map<std::string, get_children_callback> monitor_sub_path_;

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
                    return !run_ || !task_queue_.empty() || !monitor_exist_path_.empty() ||
                        monitor_get_path_.empty() || monitor_sub_path_.empty();
                });
                auto task_queue = std::move(task_queue_);
                auto last_path_existed = last_path_existed_;
                auto monitor_exist_path = monitor_exist_path_;
                auto monitor_get_path = monitor_get_path_;
                auto monitor_sub_path = monitor_sub_path_;
                lock.unlock();

                // deal task
                for (auto&& task : task_queue) {
                    task();
                }

                //monitor exist
                for (auto& [path, exist_status] : last_path_existed) {
                    auto status = std::filesystem::exists(path);
                    if (exist_status == status) {
                        continue;
                    }

                    //last status is existed, this time not existed.
                    if (exist_status == true) {
                        monitor_exist_path[path](file_error::file_ok, file_event::file_deleted_event);
                        continue;
                    }
                    //last status is not existed, this time existed.
                    if (exist_status == false) {
                        monitor_exist_path[path](file_error::file_ok, file_event::file_created_event);
                    }
                }

                //monitor get

                //monitor sub
            }
        });
    }

    ~local_file() {
        run_ = false;
        task_cv_.notify_one();
        if (task_thread_.joinable()) {
            task_thread_.join();
        }
    }

    void create_path(std::string_view path, std::string_view value, file_create_mode mode,
                     create_callback ccb, int64_t ttl = -1) {
        bool enable_ttl = false;
        if (mode == file_create_mode::file_persistent_sequential_with_ttl ||
            mode == file_create_mode::file_persistent_with_ttl) {
            enable_ttl = true;
        }
        if (enable_ttl && ttl < 0) {
            throw std::runtime_error("enable_ttl, ttl must > 0");
        }
        add_task(
            [this, p = std::string(path), v = std::string(value), callback = std::move(ccb)]() mutable {
            std::filesystem::path path(p);
            auto exist = std::filesystem::exists(path);
            if (exist) {
                callback(file_error::file_exist, {});
                return;
            }

            auto parent_path = path.parent_path();
            auto file_name = path.filename();
            std::filesystem::create_directories(parent_path);
            auto err = set_file_value(p, v);
            callback(err, std::move(p));
        });
    }

    void delete_path(std::string_view path, delete_callback dcb) {
        add_task([p = std::string(path), callback = std::move(dcb)]() {
            std::filesystem::remove_all(p);
            callback(file_error::file_ok);
        });
    }

    void set_path_value(std::string_view path, std::string_view value, set_callback scb) {
        add_task([this, p = std::string(path), v = std::string(value), callback = std::move(scb)]() {
            auto exist = std::filesystem::exists(p);
            if (!exist) {
                callback(file_error::file_not_exist);
                return;
            }

            auto err = set_file_value(p, v);
            callback(err);
        });
    }

    // [create/delete/changed] event just for current path
    void exists_path(std::string_view path, exists_callback ecb) {
        auto existed = std::filesystem::exists(path);
        auto p = std::string(path);

        std::unique_lock lock(task_mtx_);
        monitor_exist_path_.emplace(p, std::move(ecb));
        last_path_existed_.emplace(std::move(p), existed);
    }

    // [changed] event just for current path, if the path exists all the time
    void get_path_value(std::string_view path, get_callback gcb) {
        std::string value;
        last_path_value_;
        get_file_value(path, value);

        std::unique_lock lock(task_mtx_);
        monitor_get_path_.emplace(std::string(path), std::move(gcb));
    }

    // [create/delete] sub path event just for current path, if the path exists all the time
    void get_sub_path(std::string_view path, get_children_callback gccb) {
        std::unique_lock lock(task_mtx_);
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
            create_mode = is_sequential ?
                file_create_mode::file_ephemeral_sequential : file_create_mode::file_ephemeral;
        }
        else {
            create_mode = is_sequential ?
                file_create_mode::file_persistent_sequential : file_create_mode::file_persistent;
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

    file_error get_file_value(std::string_view path, std::string& value) {
        auto file = fopen(path.data(), "wb+");
        if (file == nullptr) {
            return file_error::file_not_exist;
        }
        auto size = std::filesystem::file_size(path);
        std::string v;
        v.resize(size, 0);
        fread(v.data(), v.length(), 1, file);
        fclose(file);
        value = std::move(v);
        return file_error::file_ok;
    }

    file_error set_file_value(std::string_view path, std::string_view value) {
        auto file = fopen(path.data(), "wb+");
        if (file == nullptr) {
            return file_error::file_not_exist;
        }
        fwrite(value.data(), value.length(), 1, file);
        fclose(file);
        return file_error::file_ok;
    }
};

}  // namespace loc