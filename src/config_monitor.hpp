#pragma once
#include <algorithm>
#include <deque>
#include <functional>
#include <mutex>
#include <string_view>
#include <type_traits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <semaphore>
#include "awaitable_transform.hpp"

namespace zk {
class cppzk;
}
namespace loc {
class config_file;
}
namespace etcd {
class etcd_v3;
}

namespace cm {

#define HAS_MEMBER(FUN)																		\
template <typename T, class U = void>														\
struct has_##FUN : std::false_type {};														\
template <typename T>																		\
struct has_##FUN<T, std::enable_if_t<std::is_member_function_pointer_v<decltype(&T::FUN)>>> \
: std::true_type {};																		\
template <class T>																			\
constexpr bool has_##FUN##_v = has_##FUN<T>::value; 

HAS_MEMBER(set_expired_cb);
HAS_MEMBER(get_client_ip);

enum class path_event {
    changed = 1,  // create, update
    del
};

enum class watch_type {
    watch_path = 0x01,
    watch_sub_path = 0x02,
    watch_all = 0x04
};

enum class create_mode {
    persistent = 0,
    ephemeral = 1,
    persistent_sequential = 2,
    ephemeral_sequential = 3,
    persistent_with_ttl = 5,
    persistent_sequential_with_ttl = 6
};

template <typename>
inline constexpr bool always_false_v = false;

template <typename ConfigType>
class config_monitor : public ConfigType {
public:
    using watch_cb = std::function<void(path_event, std::optional<std::string>&&, const std::string&)>;
    using operate_cb = std::function<void(const std::error_code&)>;
    using create_cb = std::function<void(const std::error_code&, std::string&&)>;

private:
    // key is main path
    std::unordered_map<std::string, std::unordered_set<std::string>> last_sub_path_;
    std::unordered_map<std::string, std::unordered_map<watch_type, watch_cb>> record_;
    std::mutex record_mtx_;

public:
    config_monitor(const config_monitor&) = delete;
    config_monitor& operator=(const config_monitor&) = delete;
    config_monitor() = default;

    static auto& instance() {
        static config_monitor cm;
        return cm;
    }

    /**
     * @brief Initialize the ConfigType and set session expire callback if has
     * @tparam ...Args
     * @param ...args According to the ConfigType
    */
    template <typename... Args>
    void init(Args&&... args) {
        if constexpr (has_set_expired_cb_v<ConfigType>) {
            ConfigType::set_expired_cb([this, arg = std::make_tuple(args...)]() mutable {
                ConfigType::clear_resource();
                last_sub_path_.clear();
                this->callable([this](auto&&... args) {
                    ConfigType::initialize(std::forward<decltype(args)>(args)...);
                }, std::move(arg), std::make_index_sequence<std::tuple_size_v<decltype(arg)>>());

                // auto rewatch
                if constexpr (std::is_same_v<ConfigType, zk::cppzk>) {
                    std::unique_lock<std::mutex> lock(record_mtx_);
                    auto record = std::move(record_);           
                    lock.unlock();
                    for (auto& [path, pair] : record) {
                        for (auto& [watch_type, cb] : pair) {
                            watch_type == watch_type::watch_path ?
                                async_watch_path(path, std::move(cb)) :
                                async_watch_sub_path(path, std::move(cb));
                        }
                    }
                }
            });
        }
        ConfigType::initialize(std::forward<Args>(args)...);
    }

    /**
     * @brief Sync create full path.
     * If the path depth more than 1, the prefix path will be created automatically.
     *
     * @param path The full path need to be created
     * @param value The path initial value when created
     * @param mode Default is persistent path
     * @return [std::error_code, path_name], a new path name if mode is sequential
    */
    auto create_path(std::string_view path, const std::optional<std::string>& value = std::nullopt,
                     create_mode mode = create_mode::persistent) {
        std::binary_semaphore cond{0};
        std::tuple<std::error_code, std::string> ret;
        [this, &cond, &ret, path, &value, mode]() ->coro::coro_task<void> {
            auto create_mode = ConfigType::get_create_mode(static_cast<int>(mode));
            if constexpr (std::is_same_v<ConfigType, zk::cppzk>) {
                auto sp_path = ConfigType::split_path(path);
                auto sp_mode = ConfigType::get_create_mode((int)create_mode::persistent);
                for (size_t i = 0; i < sp_path.size() - 1; ++i) {
                    co_await ConfigType::async_create_path(sp_path[i].data(), std::nullopt, sp_mode);
                }
            }
            ret = co_await ConfigType::async_create_path(path, value, create_mode);
            cond.release();
        }();
        cond.acquire();
        return ret;
    }

    /**
     * @brief Async create full path.
     * If the path depth more than 1, the prefix path will be created automatically.
     *
     * @param path The full path need to be created
     * @param cb Callback, 2th arg is a new path name if mode is sequential
     * @param value Set the path initial value when created
     * @param mode Default is persistent path
    */
    coro::coro_task<> async_create_path(std::string_view path, create_cb cb,
        const std::optional<std::string>& value = std::nullopt,
        create_mode mode = create_mode::persistent) {
        std::string p(path);
        auto v = value;
        auto create_mode = ConfigType::get_create_mode(static_cast<int>(mode));
        if constexpr (std::is_same_v<ConfigType, zk::cppzk>) {
            auto sp_path = ConfigType::split_path(p);
            auto sp_mode = ConfigType::get_create_mode((int)create_mode::persistent);
            for (size_t i = 0; i < sp_path.size() - 1; ++i) {
                co_await ConfigType::async_create_path(sp_path[i].data(), std::nullopt, sp_mode);
            }
        }

        auto [ec, new_path] = co_await ConfigType::async_create_path(p, v, create_mode);
        if (cb) {
            cb(ec, std::move(new_path));
        }
    }

    /**
     * @brief Sync change a path value
     * @param path The target path
     * @param value The changed value
     * @return std::error_code
    */
    auto set_path_value(std::string_view path, std::string_view value) {
        std::binary_semaphore cond{0};
        std::error_code ret;
        [this, &cond, &ret, path, value]() ->coro::coro_task<void> {
            ret = co_await ConfigType::async_set_path_value(path, value);
            cond.release();
        }();
        cond.acquire();
        return ret;
    }

    /**
    * @brief Async change a path value if path exist
    * @param path The target path
    * @param value The changed value
    * @param callback
   */
    coro::coro_task<> async_set_path_value(std::string_view path,
        std::string_view value, operate_cb callback) {
        auto ec = co_await ConfigType::async_set_path_value(path, value);
        if (callback) {
            callback(ec);
        }
    }

    /**
     * @brief Sync delete the path (include their sub path).
     *
     * @param path The target path
     * @return std::error_code
    */
    auto del_path(std::string_view path) {
        std::binary_semaphore cond{0};
        std::error_code ret;
        [this, &cond, &ret, path]() ->coro::coro_task<void> {
            ret = co_await ConfigType::async_delete_path(path);
            cond.release();
        }();
        cond.acquire();
        return ret;
    }

    /**
    * @brief Async delete the path (include their sub path).
    *
    * @param path The target path
    * @param callback
   */
    coro::coro_task<> async_del_path(std::string_view path, operate_cb callback) {
        auto ec = co_await ConfigType::async_delete_path(path);
        if (callback) {
            callback(ec);
        }
    }

    /**
     * @brief Sync get the path value just once. Path must be existed.
     * @param path The target path
     * @return [std::error_code, value]
    */
    auto watch_path(std::string_view path) {
        std::binary_semaphore cond{0};
        std::tuple<std::error_code, std::optional<std::string>> ret;
        [this, &cond, &ret, path]() ->coro::coro_task<void> {
            ret = co_await ConfigType::async_get_path_value(path);
            cond.release();
        }();
        cond.acquire();
        return ret;
    }

    /**
     * @brief Async get the path value.
     * Also valid for a non existed path. The monitor will start after the target path is created.
     *
     * @param path The target path
     * @param cb Callback, 2th arg is changed value.
     * If the event is del, then the changed value must be empty.
    */
    coro::coro_task<> async_watch_path(std::string_view path, watch_cb cb) {
        auto p = std::string(path);
        if constexpr (std::is_same_v<ConfigType, zk::cppzk>) {
            std::unique_lock<std::mutex> lock(record_mtx_);
            record_[p].emplace(watch_type::watch_path, cb);
            lock.unlock();
        }
        //for first time
        auto [ec, value] = co_await ConfigType::async_get_path_value(p);
        if (!ec) {
            cb(path_event::changed, value.has_value() ? std::move(value) : std::nullopt, p);
        }

        for (;;) {
            auto eve = co_await ConfigType::async_watch_exists_path(p);
            if (ConfigType::is_session_event(eve) || ConfigType::is_notwatching_event(eve)) {
                co_return;
            }
            if (ConfigType::is_create_event(eve) || ConfigType::is_changed_event(eve)) {
                auto [e, val] = co_await ConfigType::async_get_path_value(p);
                if (!e) {
                    cb(path_event::changed, val.has_value() ? std::move(val) : std::nullopt, p);
                }
            }
            if (ConfigType::is_delete_event(eve)) {
                cb(path_event::del, {}, p);
            }
        }
    }

    /**
     * @brief Sync get children path value of the target path just once. Path must be existed.
     * @param path The target path
     * @return [std::error_code, std::unordered_map<std::string, std::optional<std::string>>]
    */
    auto watch_sub_path(std::string_view path) {
        std::binary_semaphore cond{0};
        std::tuple<std::error_code, std::unordered_map<std::string, std::optional<std::string>>> ret;
        [this, &cond, &ret, path]() ->coro::coro_task<void> {
            auto [ec, sub_paths] = co_await ConfigType::async_get_sub_path(path);
            std::unordered_map<std::string, std::optional<std::string>> mapping_values;
            if (ec) {
                ret = std::make_tuple(ec, mapping_values);
                cond.release();
                co_return;
            }

            for (auto it = sub_paths.begin(); it != sub_paths.end(); ++it) {
                auto full_path = std::string(path) + "/" + *it;
                auto [wec, value] = co_await ConfigType::async_get_path_value(full_path);
                if (!wec) {
                    mapping_values.emplace(std::move(full_path), std::move(value));
                }
            }
            ret = std::make_tuple(ec, mapping_values);
            cond.release();
        }();
        cond.acquire();
        return ret;
    }

    /**
     * @brief Async get children path value of the target path.
     * Also valid for a non existed path. The monitor will start after the target path is created.
     *
     * @param path The target path
     * @param cb Callback, 2th arg is changed value.
     * If the event is del, then the changed value must be empty.
    */
    coro::coro_task<> async_watch_sub_path(std::string_view path, watch_cb cb) {
        auto main_path = std::string(path);
        if constexpr (std::is_same_v<ConfigType, zk::cppzk>) {
            std::unique_lock<std::mutex> lock(record_mtx_);
            record_[main_path].emplace(watch_type::watch_sub_path, cb);
            lock.unlock();
        }   
        //for first time
        auto [ec, subs] = co_await ConfigType::async_get_sub_path(main_path);
        if (!ec) {
            for (const auto& sub : subs) {
                std::string p = main_path + "/" + sub;
                async_watch_path(p, cb);
            }
        }
        else {
            if (ConfigType::is_no_node(ec)) {
                for (;;) {
                    auto eve = co_await ConfigType::async_watch_exists_path(main_path);
                    if (ConfigType::is_session_event(eve) || ConfigType::is_notwatching_event(eve)) {
                        co_return;
                    }
                    if (ConfigType::is_create_event(eve)) {
                        break;
                    }
                }
            }
        }
       
        for (;;) {
            auto eve = co_await ConfigType::async_watch_sub_path(main_path);
            if (ConfigType::is_session_event(eve) || ConfigType::is_notwatching_event(eve)) {
                co_return;
            }
            auto [ec2, sub_paths] = co_await ConfigType::async_get_sub_path(main_path);
            if (ec2) {
                continue;
            }
            std::unordered_set<std::string> sub_paths_set;
            auto it = last_sub_path_.find(main_path);
            // all sub_paths are new path, get each value
            if (it == last_sub_path_.end()) {
                for (auto&& sub_path : sub_paths) {
                    std::string p = main_path + "/" + sub_path;
                    async_watch_path(p, cb);
                    sub_paths_set.emplace(std::move(sub_path));
                }
                last_sub_path_.emplace(main_path, std::move(sub_paths_set));
                continue;
            }
            // check new paths
            for (auto&& sub_path : sub_paths) {
                if (it->second.find(sub_path) != it->second.end()) { // exist
                    continue;
                }
                std::string p = main_path + "/" + sub_path;
                async_watch_path(p, cb);
               }
            //Replace the old sub_paths_set
            for (auto&& sub_path : sub_paths) {
                sub_paths_set.emplace(std::move(sub_path));
            }
            last_sub_path_[main_path] = std::move(sub_paths_set);
        }
    }

    /**
    * @brief Sync remove the watch, the path event will not be triggered.
    * @param path The target path
    * @param type Watch type, path or sub-path
   */
    auto remove_watches(std::string_view path, watch_type type) {
        std::binary_semaphore cond{0};
        std::error_code ret;
        [this, &cond, &ret, path, type]() ->coro::coro_task<void> {
            auto p = std::string(path);
            ret = co_await ConfigType::async_remove_watches(path, (int)type);
            if (ret) {
                cond.release();
                co_return;
            }

            last_sub_path_.erase(p);
            if (type == watch_type::watch_path) {
                std::unique_lock<std::mutex> lock(record_mtx_);
                record_.erase(p);        
            }
            else { //sub-path
                std::unique_lock<std::mutex> lock(record_mtx_);
                if (auto it = record_.find(p); it != record_.end()) {
                    it->second.erase(type);
                    if (it->second.empty()) {
                        record_.erase(it);
                    }
                }
            }          
            cond.release();
        }();
        cond.acquire();
        return ret;
    }

    /**
     * @brief Async remove the watch, the path event will not be triggered.
     * @param path The target path
     * @param type Watch type, path or sub-path
     * @param callback
    */
    coro::coro_task<>
        async_remove_watches(std::string_view path, watch_type type, operate_cb callback) {
        auto p = std::string(path);
        auto ret = co_await ConfigType::async_remove_watches(path, (int)type);
        if (ret) {
            if (callback) {
                callback(ret);
            }
            co_return;
        }

        last_sub_path_.erase(p);
        if (type == watch_type::watch_path) {
            std::unique_lock<std::mutex> lock(record_mtx_);
            record_.erase(p);
        }
        else { //sub-path
            std::unique_lock<std::mutex> lock(record_mtx_);
            if (auto it = record_.find(p); it != record_.end()) {
                it->second.erase(type);
                if (it->second.empty()) {
                    record_.erase(it);
                }
            }
        }

        if (callback) {
            callback(ret);
        }
    }

    /**
     * @brief Get self ip with the session
     * @return Self ip
    */
    auto client_ip() {
        if constexpr (has_get_client_ip_v<ConfigType>) {
            return ConfigType::get_client_ip();
        }
    }

private:
    template <typename F, typename Tuple, std::size_t... I>
    constexpr void callable(F&& f, Tuple&& tuple, std::index_sequence<I...>) {
        f(std::get<I>(std::forward<Tuple>(tuple))...);
    }
};
}  // namespace cm 