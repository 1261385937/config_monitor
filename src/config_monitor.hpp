#pragma once
#include <algorithm>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <type_traits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <future>

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
	del = 2
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

template <typename ConfigType = zk::cppzk>
class config_monitor : public ConfigType {
public:
	using watch_sub_cb = std::function<void(path_event, std::string_view, std::optional<std::string>&&)>;
	using watch_cb = std::function<void(path_event, std::optional<std::string>&&)>;
	using operate_cb = std::function<void(const std::error_code&)>;
	using create_cb = std::function<void(const std::error_code&, std::string&&)>;
	using get_callback = std::function<void(const std::error_code&, std::optional<std::string>&&)>;

private:
	// key is main path
	std::unordered_map<std::string, std::unordered_set<std::string>> last_sub_path_;
	//std::unordered_map<std::string, std::unordered_map<std::string, std::string>> sub_path_value_;
	std::unordered_map<std::string, watch_cb> watch_record_;
	std::unordered_map<std::string, watch_sub_cb> watch_sub_record_;
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
			ConfigType::set_expired_cb([this, arg = std::make_tuple(args...)]() {
				ConfigType::clear_resource();
				last_sub_path_.clear();
				this->callable([this](auto&&... args) {
					ConfigType::initialize(std::forward<decltype(args)>(args)...);
				}, std::move(arg), std::make_index_sequence<std::tuple_size_v<decltype(arg)>>());

				// auto rewatch
				if constexpr (std::is_same_v<ConfigType, zk::cppzk>) {
					std::unique_lock<std::mutex> lock(record_mtx_);
					auto watch_record = std::move(watch_record_);
					auto watch_sub_record = std::move(watch_sub_record_);
					lock.unlock();
					for (auto&& [path, cb] : watch_record) {
						watch_path(path, std::move(cb));
					}
					for (auto&& [path, cb] : watch_sub_record) {
						watch_sub_path(path, std::move(cb));
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
		auto create_mode = ConfigType::get_create_mode(static_cast<int>(mode));
		return ConfigType::create_path(path, value, create_mode);
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
	void async_create_path(std::string path, create_cb cb,
		const std::optional<std::string>& value = std::nullopt,
		create_mode mode = create_mode::persistent) {
		auto create_mode = ConfigType::get_create_mode(static_cast<int>(mode));
		ConfigType::async_create_path(path, value, create_mode,
			[cb = std::move(cb)](const auto& ec, std::string&& new_path) {
			if (cb) {
				cb(ec, std::move(new_path));
			}
		});
	}

	/**
	 * @brief Sync delete the path (include their sub path).
	 *
	 * @param path The target path
	 * @return std::error_code
	 */
	auto del_path(std::string_view path) {
		return ConfigType::delete_path(path);
	}

	/**
	 * @brief Async delete the path (include their sub path).
	 *
	 * @param path The target path
	 * @param callback
	 */
	void async_del_path(std::string_view path, operate_cb callback) {
		ConfigType::async_delete_path(path, [this, cb = std::move(callback)](const auto& ec) {
			if (cb) {
				cb(ec);
			}
		});
	}

	/**
	 * @brief Sync change a path value
	 * @param path The target path
	 * @param value The changed value
	 * @return std::error_code
     */
	auto set_path_value(std::string_view path, std::string_view value) {
		return ConfigType::set_path_value(path, value);
	}

	/**
	 * @brief Async change a path value if path exist
	 * @param path The target path
	 * @param value The changed value
	 * @param callback
     */
	void async_set_path_value(std::string_view path, std::string_view value, operate_cb callback) {
		ConfigType::async_set_path_value(path, value, [cb = std::move(callback)](const auto& ec) {
			if (cb) {
				cb(ec);
			}
		});
	}

	/**
	 * @brief Sync get sub-path of a parent path
	 * @param path Parent path
	 * @return [std::error_code, std::vector<std::string>]
	 */
	auto get_sub_path(std::string_view path) {
		return ConfigType::get_sub_path(path);
	}

	/**
	 * @brief Sync get a path value
	 * @param path The target path
	 * @return [std::error_code, std::optional<std::string>]
	 */
	auto get_path_value(std::string_view path) {
		return ConfigType::get_path_value(path);
	}

	/**
	 * @brief Async get a path value
	 * @param path The target path
	 * @param callback
	 */
	void async_get_path_value(std::string_view path, get_callback callback) {
		ConfigType::async_get_path_value(path,
			[cb = std::move(callback)](const auto& ec, auto, auto, auto&& val) {
			if (cb) {
				cb(ec, std::move(val));
			}
		});
	}

	/**
	 * @brief Async monitor path changed. It will set the next watch point automatically.
	 * Also valid for a non existed path, monitor will start after the target path is created.
	 *
	 * @param path The target path
	 * @param cb Callback, 2th arg is changed value.
	 * If the event is del, then the 2th arg value will be empty.
     */
	void watch_path(std::string_view path, watch_cb callback) {
		if constexpr (std::is_same_v<ConfigType, zk::cppzk>) {
			std::unique_lock<std::mutex> lock(record_mtx_);
			watch_record_[std::string(path)] = callback;
			lock.unlock();
		}

		ConfigType::watch_path_event(path,
			[this, cb = std::move(callback), p = std::string(path)](const auto& ec, auto eve) {
			if (ec && ConfigType::is_delete_event(eve)) {
				cb(path_event::del, {});
				return;
			}
			auto changed = ConfigType::is_dummy_event(eve) ||
				ConfigType::is_create_event(eve) || ConfigType::is_changed_event(eve);
			if (changed) {
				ConfigType::async_get_path_value(p, [&cb](const auto& ec, auto, auto, auto&& val) {
					if (!ec) {
						cb(path_event::changed, std::move(val));
					}
				});
			}
		});
	}

	/**
	 * @brief Async monitor children path changed of the target path,
	 * do not include children path's children path.
	 * It will set the next watch point automatically.
	 * Also valid for a non existed path, monitor will start after the target path is created.
	 *
	 * @param path The target path
	 * @param cb Callback, 2th arg is associated sub path, 3th arg is changed value. 
	 * If the event is del, then the changed value must be empty.
	 */
	void watch_sub_path(std::string_view path, watch_sub_cb callback) {
		if constexpr (std::is_same_v<ConfigType, zk::cppzk>) {
			std::unique_lock<std::mutex> lock(record_mtx_);
			watch_sub_record_[std::string(path)] = callback;
			lock.unlock();
		}

		auto prefix = std::string(path);
		auto monitor = [this, cb = std::move(callback), prefix](const std::string& sub_path) {
			ConfigType::template async_get_path_value<true>(sub_path, 
				[this, cb = std::move(cb), prefix = std::move(prefix)](
					const auto& ec, auto eve, std::string_view path, auto&& val) {
				if (ec && ConfigType::is_delete_event(eve)) {
					cb(path_event::del, path, {});
					return;
				}
				if (!ec) {
					cb(path_event::changed, path, std::move(val));
				}	
			});
		};

		ConfigType::watch_path_event(path, 
		[this, prefix = std::move(prefix), monitor = std::move(monitor)](const auto& ec, auto eve) {
			if (ec) {
				return;
			}
			if (!ConfigType::is_dummy_event(eve) && !ConfigType::is_create_event(eve)) {
				return; //filter the target path other event
			}

			ConfigType::template async_get_sub_path<true>(prefix,
				[this, prefix, monitor = std::move(monitor)](const auto& ec, auto&& sub_paths) {
				if (ec) {
					return;
				}

				std::unordered_set<std::string> sub_paths_set;
				auto it = last_sub_path_.find(prefix);
				// all is new paths, monitor all
				if (it == last_sub_path_.end()) {
					for (auto&& sub_path : sub_paths) {
						monitor(prefix + "/" + sub_path);
						sub_paths_set.emplace(std::move(sub_path));
					}
					last_sub_path_.emplace(prefix, std::move(sub_paths_set));
					return;
				}
				// monitor new paths
				for (auto&& sub_path : sub_paths) {
					if (it->second.find(sub_path) != it->second.end()) { //ignore existed path
						continue;
					}
					monitor(prefix + "/" + sub_path);
				}
				//Replace the old sub_paths_set
				for (auto&& sub_path : sub_paths) {
					sub_paths_set.emplace(std::move(sub_path));
				}
				last_sub_path_[prefix] = std::move(sub_paths_set);
			});
		});
	}

	/**
     * @brief Sync remove the watch, the path event will not be triggered.
     * @param path The target path
     * @param type Watch type, path or sub-path
     */
	auto remove_watches(std::string_view path, watch_type type) {
		std::promise<std::error_code> pro;
		async_remove_watches(path, type, [&pro](const std::error_code& ec) {
			pro.set_value(ec);
		});
		return pro.get_future().get();
	}

	/**
	 * @brief Async remove the watch, the path event will not be triggered.
	 * @param path The target path
	 * @param type Watch type, path or sub-path
	 * @param callback
	 */
	void async_remove_watches(std::string_view path, watch_type type, operate_cb callback) {
		ConfigType::async_remove_watches(path, static_cast<int>(type),
			[this, type, p = std::string(path), cb = std::move(callback)](auto ec) {		
			if (type == watch_type::watch_path) {
				std::unique_lock<std::mutex> lock(record_mtx_);
				watch_record_.erase(p);
			}
			else { //sub-path
				last_sub_path_.erase(p);
				std::unique_lock<std::mutex> lock(record_mtx_);
				watch_sub_record_.erase(p);
			}
			if (cb) {
				cb(ec);
			}
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