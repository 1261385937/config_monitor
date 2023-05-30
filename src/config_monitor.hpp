#pragma once
#include <algorithm>
#include <deque>
#include <functional>
#include <mutex>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

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
    watch_path,
    watch_sub_path
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
    using watch_cb = std::function<void(path_event, std::string&&)>;
    using mapping_watch_cb = std::function<void(path_event, std::string&&, std::string_view)>;
    using operate_cb = std::function<void(bool)>;
    using create_cb = std::function<void(bool, std::string&&)>;

private:
    // key is main path
    std::unordered_map<std::string, std::unordered_set<std::string>> last_sub_path_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> sub_path_value_;
    std::unordered_map<std::string, std::unordered_map<watch_type, watch_cb>> record_;
    std::unordered_map<std::string, std::pair<watch_type, mapping_watch_cb>> mapping_record_;
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
                    auto mapping_record = std::move(mapping_record_);
                    lock.unlock();
                    for (auto& [path, pair] : record) {
                        for (auto& [watch_type, cb] : pair) {
                            watch_type == watch_type::watch_path ?
                                async_watch_path(path, std::move(cb)) :
                                async_watch_sub_path<false>(path, std::move(cb));
                        }
                    }
                    //Here just watch_sub_path
                    for (auto& [path, pair] : mapping_record) {
                        async_watch_sub_path(path, std::move(pair.second));
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
    auto create_path(std::string_view path, std::string_view value = "",
                     create_mode mode = create_mode::persistent) {
        auto create_mode = ConfigType::get_create_mode(static_cast<int>(mode));
        std::promise<std::string> pro;
        ConfigType::create_path(path, value, create_mode, [this, &pro](auto, std::string&& path) {
            pro.set_value(std::move(path));
        });
        return pro.get_future().get();
    }

    /**
     * @brief Sync change a path value
     * @param path The target path
     * @param value The changed value
     * @return std::error_code
    */
    auto set_path_value(std::string_view path, std::string_view value) {
        std::promise<bool> pro;
        ConfigType::set_path_value(path, value, [this, &pro](auto e) {
            pro.set_value(ConfigType::is_no_error(e) ? true : false);
        });
        return pro.get_future().get();
    }

    /**
     * @brief Sync delete the path.
     * If the path depth more than 1, the prefix path (include their sub path) will aslo be deleted.
     *
     * @param path The target path
     * @return std::error_code
    */
    auto del_path(std::string_view path) {
        std::promise<bool> pro;
        ConfigType::delete_path(path, [this, &pro](auto e) {
            pro.set_value(ConfigType::is_no_error(e) ? true : false);
        });
        return pro.get_future().get();
    }

    /**
     * @brief Sync get the path value just once. Path must be existed.
     * @param path The target path
     * @return [std::error_code, value]
    */
    auto watch_path(std::string_view path) {
        std::promise<std::string> pro;
        ConfigType::get_path_value<false>(path, [&pro](auto, std::optional<std::string>&& value) {
            if (!value.has_value()) {
                return pro.set_value({});
            }
            pro.set_value(std::move(value.value()));
        });
        return pro.get_future().get();
    }

    /**
     * @brief Sync get children path value of the target path just once. Path must be existed.
     * @tparam Mapping Enable mapping or not. If enable, [subpath, value] mapping will be return.
     * @param path The target path
     * @return
     * [std::error_code, std::vector<std::string> or std::unordered_map<std::string, std::string>]
    */
    template<bool Mapping = true>
    auto watch_sub_path(std::string_view path) {
        using sub_paths_type = std::conditional_t<std::is_same_v<ConfigType, zk::cppzk>,
            std::vector<std::string>, std::deque<std::string>>;
        std::promise<sub_paths_type> pro;
        ConfigType::get_sub_path<false>(path, [&pro](auto, auto, auto&& sub_paths) {
            if (sub_paths.empty()) {
                return pro.set_value({});
            }
            pro.set_value(std::move(sub_paths));
        });

        auto sub_paths = pro.get_future().get();
        std::vector<std::string> values;
        values.reserve(sub_paths.size());
        for (auto& sub_path : sub_paths) {
            values.emplace_back(watch_path(std::string(path) + "/" + sub_path));
        }

        if constexpr (Mapping) {
            std::unordered_map<std::string, std::string> path_values;
            size_t index = 0;
            for (auto& sub_path : sub_paths) {
                path_values.emplace(std::move(sub_path), std::move(values[index]));
                index++;
            }
            return path_values;
        }
        else {
            return values;
        }
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
    void async_create_path(std::string_view path, create_cb cb, std::string_view value = "",
                           create_mode mode = create_mode::persistent) {
        auto create_mode = ConfigType::get_create_mode(static_cast<int>(mode));
        ConfigType::create_path(path, value, create_mode, [this, cb](auto e, std::string&& path) {
            if (cb) {
                cb(ConfigType::is_no_error(e) ? true : false, std::move(path));
            }
        });
    }

    /**
     * @brief Async change a path value if path exist
     * @param path The target path
     * @param value The changed value
     * @param callback
    */
    void async_set_path_value(std::string_view path, std::string_view value, operate_cb callback) {
        ConfigType::set_path_value(path, value, [this, cb = std::move(callback)](auto e) {
            if (cb) {
                cb(ConfigType::is_no_error(e) ? true : false);
            }
        });
    }

    /**
     * @brief Async delete the path.
     * If the path depth more than 1, the prefix path (include their sub path) will aslo be deleted.
     *
     * @param path The target path
     * @param callback
    */
    void async_del_path(std::string_view path, operate_cb callback) {
        ConfigType::delete_path(path, [this, cb = std::move(callback)](auto e) {
            if (cb) {
                cb(ConfigType::is_no_error(e) ? true : false);
            }
        });
    }

    /**
     * @brief Async get the path value.
     * Also valid for a non existed path. The monitor will start after the target path is created.
     *
     * @param path The target path
     * @param cb Callback, 2th arg is changed value.
     * If the event is del, then the changed value must be empty.
    */
    void async_watch_path(std::string_view path, watch_cb cb) {
        if constexpr (std::is_same_v<ConfigType, zk::cppzk>) {
            std::unique_lock<std::mutex> lock(record_mtx_);
            record_[std::string(path)].emplace(watch_type::watch_path, cb);
            lock.unlock();
        }

        ConfigType::exists_path(path, [this, cb, p = std::string(path)](auto err, auto eve) {
            if (!ConfigType::is_no_error(err)) {
                return;
            }

            if (ConfigType::is_dummy_event(eve) || ConfigType::is_create_event(eve)) {
                ConfigType::get_path_value(p, [cb, this](auto err, auto&& value) {
                    if (ConfigType::is_no_node(err)) {
                        //watch_path do not need content when del event, the mapping is explicit
                        return cb(path_event::del, {});
                    }
                    if (ConfigType::is_no_error(err) && value.has_value()) {
                        cb(path_event::changed, std::move(value.value()));
                    }
                });
            }
        });
    }

    /**
     * @brief Async get children path value of the target path.
     * Also valid for a non existed path. The monitor will start after the target path is created.
     *
     * @tparam WatchCb
     * @tparam Mapping Enable mapping or not.
     * @param path The target path
     * @param cb Callback, 2th arg is changed value.
     * If disable mapping, for del event, changed value is last time value.
     * If enable mapping, the extra 3th arg is children path. For del event, 2th arg is awalys empty
    */
    template<
        bool Mapping = true,
        typename WatchCb = std::conditional_t<Mapping, mapping_watch_cb, watch_cb>
    >
    void async_watch_sub_path(std::string_view path, WatchCb&& cb) {
        if constexpr (std::is_same_v<ConfigType, zk::cppzk>) {
            std::unique_lock<std::mutex> lock(record_mtx_);
            if constexpr (Mapping) {
                mapping_record_[std::string(path)] = { watch_type::watch_sub_path, cb };
            }
            else {
                record_[std::string(path)].emplace(watch_type::watch_sub_path, cb);
            }
            lock.unlock();
        }

        auto main_path = std::string(path);
        auto monitor_path = [this, cb, main_path](const std::string& sub_path) {
            ConfigType::get_path_value(
                sub_path, [this, cb, main_path, sub_path](auto e, std::optional<std::string>&& value) {
                if (ConfigType::is_no_error(e) && value.has_value()) {  // changed
                    if constexpr (Mapping) {
                        cb(path_event::changed, std::move(value.value()), sub_path);
                    }
                    else {
                        sub_path_value_[main_path][sub_path] = value.value();
                        cb(path_event::changed, std::move(value.value()));
                    }
                    return;
                }

                if (ConfigType::is_no_node(e)) {  // del
                    if constexpr (Mapping) {
                        cb(path_event::del, {}, sub_path);
                    }
                    else {
                        cb(path_event::del, std::move(sub_path_value_[main_path][sub_path]));
                        sub_path_value_[main_path].erase(sub_path);
                    }
                }
            });
        };

        ConfigType::exists_path(path, [this, main_path, monitor_path](auto err, auto eve) {
            if (!ConfigType::is_no_error(err)) {
                return;
            }
            if (!ConfigType::is_dummy_event(eve) && !ConfigType::is_create_event(eve)) {
                return;
            }

            ConfigType::get_sub_path(
                main_path, [this, main_path, monitor_path](auto err, auto, auto&& sub_paths) {
                if (!ConfigType::is_no_error(err)) {
                    return;
                }

                std::unordered_set<std::string> sub_paths_set;
                auto it = last_sub_path_.find(main_path);
                // all is new paths, monitor all
                if (it == last_sub_path_.end()) {
                    for (auto&& sub_path : sub_paths) {
                        monitor_path(main_path + "/" + sub_path);
                        sub_paths_set.emplace(std::move(sub_path));
                    }
                    last_sub_path_.emplace(main_path, std::move(sub_paths_set));
                    return;
                }
                // che new paths
                for (auto&& sub_path : sub_paths) {
                    if (it->second.find(sub_path) != it->second.end()) {  // exist
                        continue;
                    }
                    monitor_path(main_path + "/" + sub_path);
                }
                //Replace the old sub_paths_set
                for (auto&& sub_path : sub_paths) {
                    sub_paths_set.emplace(std::move(sub_path));
                }
                last_sub_path_[main_path] = std::move(sub_paths_set);
            });
        });
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