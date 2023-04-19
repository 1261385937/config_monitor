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
	using create_cb = std::function<void(bool, std::string_view)>;

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
				printf("session expired, then reconnect");

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
								watch_path(r.first, type_cb.second) :
								watch_sub_path(r.first, type_cb.second);
						}
					}
				}
			});
		}
		ConfigType::initialize(std::forward<Args>(args)...);
	}

	void watch_path(std::string_view path, watch_cb cb) {
		if constexpr (std::is_same_v<ConfigType, zk::cppzk>) {
			std::unique_lock<std::mutex> lock(record_mtx_);
			record_[std::string(path)].emplace(watch_type::watch_path, cb);
			lock.unlock();
		}

		ConfigType::exists_path(path, [this, cb, p = std::string(path)](auto err, auto eve) {
			if (ConfigType::is_no_error(err) &&
				(ConfigType::is_dummy_event(eve) || ConfigType::is_create_event(eve))) {
				ConfigType::get_path_value(
					p, [cb, this](auto err, std::optional<std::string>&& value) {
					if (ConfigType::is_no_error(err) && value.has_value()) {
						cb(path_event::changed, std::move(value.value()));
					}
				});
				return;
			}
			if (ConfigType::is_no_node(err) && ConfigType::is_delete_event(eve)) {
				cb(path_event::del, {});
			}
		});
	}

	void watch_sub_path(std::string_view path, watch_cb cb) {
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

			auto monitor_new_path = [this, cb, main_path](const std::string& sub_path) {
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
				[this, cb, main_path](auto e, auto, std::vector<std::string>&& sub_paths) {
				if (!ConfigType::is_no_error(e)) {
					return;
				}

				std::unordered_set<std::string> sub_paths_set;
				auto it = last_sub_path_.find(main_path);
				// all is new paths, monitor all
				if (it == last_sub_path_.end()) {
					for (auto&& sub_path : sub_paths) {
						monitor_new_path(main_path + "/" + sub_path);
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
					monitor_new_path(main_path + "/" + sub_path);
				}

				for (auto&& sub_path : sub_paths) {
					sub_paths_set.emplace(std::move(sub_path));
				}
				last_sub_path_[main_path] = std::move(sub_paths_set);
			});
		});
	}

	void create_path(std::string_view path, create_cb cb, bool is_ephemeral = false,
					 bool is_sequential = false, std::string_view value = "") {
		auto r = ConfigType::exists_path_sync(path);
		if (ConfigType::is_no_error(r) && !is_sequential) {
			return cb(true, path);
		}
		auto create_mode = ConfigType::get_create_mode(is_ephemeral, is_sequential);

		auto sp_path = split_path(path);
		if (sp_path.size() == 1) {
			ConfigType::create_path(
				path, value, create_mode, [cb, this](auto e, std::string&& path) {
				if (cb) {
					cb(ConfigType::is_no_error(e) ? true : false, path);
				}
			});
			return;
		}

		std::string new_path;
		for (size_t i = 0; i < sp_path.size() - 1; ++i) {
			ConfigType::create_path_sync(sp_path[i], "",
										 ConfigType::get_persistent_mode(), new_path);
		}
		ConfigType::create_path(
			sp_path.back(), value, create_mode, [cb, this](auto e, std::string&& path) {
			if (cb) {
				cb(ConfigType::is_no_error(e) ? true : false, path);
			}
		});
	}

	void create_path_sync(std::string_view path, bool is_ephemeral = false,
						  bool is_sequential = false, std::string_view value = "") {
		auto r = ConfigType::exists_path_sync(path);
		if (ConfigType::is_no_error(r) && !is_sequential) {
			return;
		}
		auto create_mode = ConfigType::get_create_mode(is_ephemeral, is_sequential);

		std::string new_path;
		auto sp_path = split_path(path);
		if (sp_path.size() == 1) {
			ConfigType::create_path_sync(path, value, create_mode, new_path);
			return;
		}

		for (size_t i = 0; i < sp_path.size() - 1; ++i) {
			ConfigType::create_path_sync(sp_path[i], "",
										 ConfigType::get_persistent_mode(), new_path);
		}
		ConfigType::create_path_sync(sp_path.back(), value, create_mode, new_path);
	}

	auto set_path_value(std::string_view path, std::string_view value, operate_cb callback) {
		return ConfigType::set_path_value(path, value, [this, cb = std::move(callback)](auto e) {
			if (cb) {
				cb(ConfigType::is_no_error(e) ? true : false);
			}
		});
	}

	void del_path(std::string_view path, operate_cb callback) {
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

	template <typename F, typename Tuple, std::size_t... I>
	constexpr void callable(F&& f, Tuple&& tuple, std::index_sequence<I...>) {
		f(std::get<I>(std::forward<Tuple>(tuple))...);
	}
};
}  // namespace cm 