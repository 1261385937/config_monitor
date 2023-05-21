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

template <typename>
inline constexpr bool always_false_v = false;

template <typename ConfigType>
class config_monitor : public ConfigType {
public:
    using watch_cb = std::function<void(path_event, std::string&&)>;
    using operate_cb = std::function<void(bool)>;
    using create_cb = std::function<void(bool, std::string&&)>;

private:
    // key is main path
    std::unordered_map<std::string, std::unordered_set<std::string>> last_sub_path_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> sub_path_value_;
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
                    for (const auto& r : record) {
                        for (const auto& type_cb : r.second) {
                            type_cb.first == watch_type::watch_path ?
                                async_watch_path(r.first, type_cb.second) :
                                async_watch_sub_path(r.first, type_cb.second);
                        }
                    }
                }
            });
        }
        ConfigType::initialize(std::forward<Args>(args)...);
    }

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

    auto watch_sub_path(std::string_view path) {

    }

    void async_watch_sub_path(std::string_view path, watch_cb cb) {
        if constexpr (std::is_same_v<ConfigType, zk::cppzk>) {
            std::unique_lock<std::mutex> lock(record_mtx_);
            record_[std::string(path)].emplace(watch_type::watch_sub_path, cb);
            lock.unlock();
        }

        ConfigType::exists_path(
            path, [this, cb, main_path = std::string(path)](auto err, auto eve) {
            bool has_parent_path = false;
            if (ConfigType::is_no_error(err) &&
                (ConfigType::is_dummy_event(eve) || ConfigType::is_create_event(eve))) {
                has_parent_path = true;
            }
            if (!has_parent_path) {
                return;
            }

            auto monitor_path = [this, cb, main_path](const std::string& sub_path) {
                ConfigType::get_path_value(
                    sub_path,
                    [this, cb, main_path, sub_path](auto e, std::optional<std::string>&& value) {
                    if (ConfigType::is_no_error(e) && value.has_value()) {  // changed
                        sub_path_value_[main_path][sub_path] = value.value();
                        cb(path_event::changed, std::move(value.value()));
                        return;
                    }

                    if (ConfigType::is_no_node(e)) {  // del
                        auto it = sub_path_value_.find(main_path);
                        if (it == sub_path_value_.end()) {
                            return cb(path_event::del, {});
                        }
                        auto iter = it->second.find(sub_path);
                        if (iter == it->second.end()) {
                            return cb(path_event::del, {});
                        }
                        cb(path_event::del, std::move(iter->second));
                        it->second.erase(iter);
                    }
                });
            };

            ConfigType::get_sub_path(
                main_path,
                [this, cb, main_path, monitor_path](auto e, auto, auto&& sub_paths) {
                if (!ConfigType::is_no_error(e)) {
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
                // the new paths
                for (auto&& sub_path : sub_paths) {
                    if (it->second.find(sub_path) != it->second.end()) {  // exist
                        continue;
                    }
                    monitor_path(main_path + "/" + sub_path);
                }

                for (auto&& sub_path : sub_paths) {
                    sub_paths_set.emplace(std::move(sub_path));
                }
                last_sub_path_[main_path] = std::move(sub_paths_set);
            });
        });
    }

    void async_create_path(std::string_view path, create_cb cb, std::string_view value = "",
                     bool is_ephemeral = false, bool is_sequential = false) {
        auto create_mode = ConfigType::get_create_mode(is_ephemeral, is_sequential);
        ConfigType::create_path(path, value, create_mode, [this, cb](auto e, std::string&& path) {
            if (cb) {
                cb(ConfigType::is_no_error(e) ? true : false, std::move(path));
            }
        });
    }

    void async_set_path_value(std::string_view path, std::string_view value, operate_cb callback) {
        ConfigType::set_path_value(path, value, [this, cb = std::move(callback)](auto e) {
            if (cb) {
                cb(ConfigType::is_no_error(e) ? true : false);
            }
        });
    }

    void async_del_path(std::string_view path, operate_cb callback) {
        ConfigType::delete_path(path, [this, cb = std::move(callback)](auto e) {
            if (cb) {
                cb(ConfigType::is_no_error(e) ? true : false);
            }
        });
    }

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