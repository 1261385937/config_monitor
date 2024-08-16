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

	std::error_code clear_resource() {
		return make_ec(zookeeper_close(zh_));
	}

	std::error_code handle_state() {
		return make_ec(zoo_state(zh_));
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

	auto create_path(std::string_view path, const std::optional<std::string>& value,
		zk_create_mode mode, int64_t ttl = -1, zk_acl acl = zk_acl::zk_open_acl_unsafe) {
		bool enable_ttl = false;
		if (mode == zk_create_mode::zk_persistent_sequential_with_ttl ||
			mode == zk_create_mode::zk_persistent_with_ttl) {
			enable_ttl = true;
		}
		if (enable_ttl && ttl < 0) {
			throw std::runtime_error("enable_ttl, ttl must > 0");
		}

		auto value_ptr = !value.has_value() ? nullptr : value.value().data();
		auto value_len = !value.has_value() ? -1 : (int)value.value().length();
		auto sp_path = split_path(path);
		for (size_t i = 0; i < sp_path.size() - 1; ++i) {
			auto rc = zoo_create2_ttl(zh_, sp_path[i].data(), nullptr, 0,
				&acl_mapping[acl], (int)mode, ttl, nullptr, 0, nullptr);
			if ((rc != ZOO_ERRORS::ZOK) && (rc != ZOO_ERRORS::ZNODEEXISTS)) { //create error
				return std::make_tuple(make_ec(rc), std::string{});
			}
		}
		auto new_path_len = path.length() + 16; //16 for sequence number, maybe
		auto ptr = std::make_unique<char[]>(new_path_len);
		auto ret = zoo_create2_ttl(zh_, path.data(), value_ptr, value_len,
			&acl_mapping[acl], (int)mode, ttl, ptr.get(), new_path_len, nullptr);
		return std::make_tuple(make_ec(ret), std::string(ptr.get()));
	}

	void async_create_path(std::string_view path, std::optional<std::string> value,
		zk_create_mode mode, create_callback ccb, int64_t ttl = -1,
		zk_acl acl = zk_acl::zk_open_acl_unsafe) {
		bool enable_ttl = false;
		if (mode == zk_create_mode::zk_persistent_sequential_with_ttl ||
			mode == zk_create_mode::zk_persistent_with_ttl) {
			enable_ttl = true;
		}
		if (enable_ttl && ttl < 0) {
			throw std::runtime_error("enable_ttl, ttl must > 0");
		}

		auto sp_path = split_path(path);
		if (sp_path.size() == 1) { //just 1 depth
			auto val_ptr = !value.has_value() ? nullptr : value.value().data();
			auto val_len = !value.has_value() ? 0 : (int)value.value().length();
			auto ud = new create_callback{ std::move(ccb) };
			zoo_acreate2_ttl(zh_, path.data(), val_ptr, val_len, &acl_mapping[acl], (int)mode, ttl,
				[](int rc, const char* str, const struct Stat*, const void* data) {
				auto cb = (create_callback*)data;
				if ((*cb)) {
					(*cb)(make_ec(rc), str == nullptr ? std::string{} : std::string(str));
				}
				delete cb;
			}, ud);
			return;
		}

		auto completion = [](int rc, const char* str, const struct Stat*, const void* data) {
			auto cud = (create_userdata*)data;
			auto& cb = cud->callbback;
			auto& sp_path = cud->split_paths;
			sp_path.pop_front();
			auto create_error = (rc != ZOO_ERRORS::ZOK) && (rc != ZOO_ERRORS::ZNODEEXISTS);
			if (create_error || sp_path.empty()) {
				if (cb) {
					cb(make_ec(rc), str == nullptr ? std::string{} : std::string(str));
				}
				delete cud;
				return;
			}
			if (sp_path.size() == 1) { //create the layer (whole path)
				auto val_ptr = !cud->value.has_value() ? nullptr : cud->value.value().data();
				auto val_len = !cud->value.has_value() ? 0 : (int)cud->value.value().length();
				zoo_acreate2_ttl(cud->self->zh_, sp_path[0].data(), val_ptr, val_len,
					&acl_mapping[cud->acl], (int)cud->mode, cud->ttl, cud->completion, cud);
			}
			else {
				zoo_acreate2_ttl(cud->self->zh_, sp_path[0].data(), nullptr, 0,
					&acl_mapping[cud->acl], ZOO_PERSISTENT, -1, cud->completion, cud);
			}
		};

		std::string first_layer = sp_path[0];
		auto cud = new create_userdata{ split_path(path),
				std::move(value), mode, std::move(ccb), ttl, acl, this, completion };
		zoo_acreate2_ttl(zh_, first_layer.data(), nullptr, 0, &acl_mapping[acl], ZOO_PERSISTENT, -1,
			completion, cud);
	}

	auto delete_path(std::string_view path) {
		std::deque<std::string> sub_paths;
		auto ec = recursive_get_sub_path(path, sub_paths);
		if (ec) {
			return ec;
		}
		sub_paths.emplace_back(path);
		for (auto& sub : sub_paths) {
			auto rc = zoo_delete(zh_, sub.data(), -1);
			if (rc != ZOO_ERRORS::ZOK) {
				return make_ec(rc);
			}
		}
		return std::error_code{};
	}

	void async_delete_path(std::string_view path, operate_cb cb) {
		struct usrdata {
			operate_cb callback;
			std::deque<std::string> subs;
			cppzk* self;
			void_completion_t completion;
		};
		auto completion = [](int rc, const void* data) {
			auto ud = (usrdata*)data;
			ud->subs.pop_front();
			if (rc || ud->subs.empty()) {
				ud->callback(make_ec(rc));
				delete ud;
				return;
			}
			zoo_adelete(ud->self->zh_, ud->subs[0].data(), -1, ud->completion, ud);
		};

		async_recursive_get_sub_path(path,
			[this, cb = std::move(cb), prefix = std::string(path), completion](
				const std::error_code& ec, std::deque<std::string>&& subs) mutable {
			if (ec) {
				cb(ec);
				return;
			}

			subs.emplace_back(std::move(prefix));
			std::string first_path = subs[0];
			auto data = new usrdata{ std::move(cb), std::move(subs), this, completion };
			zoo_adelete(zh_, first_path.data(), -1, completion, data);
		});
	}

	void async_set_path_value(std::string_view path, std::string_view value, operate_cb cb) {
		auto data = new operate_cb{ std::move(cb) };
		zoo_aset(zh_, path.data(), value.data(), (int)value.length(), -1,
			[](int rc, const struct Stat*, const void* data) {
			auto cb = (operate_cb*)data;
			if ((*cb)) {
				(*cb)(make_ec(rc));
			}
			delete cb;
		}, data);
	}

	// [create/delete/changed] event just for current path
	void async_exists_path(std::string_view path, exists_callback ecb) {
		auto wfn = [](zhandle_t*, int eve, int state, const char* path, void* watcherCtx) {
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
			zoo_awexists(eud->self->zh_, path, eud->wfn, watcherCtx, eud->completion, watcherCtx);
		};
		auto exists_completion = [](int rc, const struct Stat*, const void* data) {
			auto d = (exists_userdata*)data;
			if (d->cb) {
				d->cb(make_ec(rc), d->eve);
			}
		};

		auto data = std::make_shared<exists_userdata>(
			wfn, exists_completion, std::move(ecb), this);
		auto r = zoo_awexists(zh_, path.data(), wfn, data.get(), exists_completion, data.get());
		std::lock_guard lock(mtx_);
		releaser_.emplace((uint64_t)data.get(), std::move(data));
	}

	// [changed] event just for current path, if the path exists all the time
	void get_path_value(std::string_view path, get_callback gcb) {
		auto wfn = [](zhandle_t*, int eve, int, const char* path, void* watcherCtx) {
			auto d = static_cast<wget_userdata*>(watcherCtx);
			if (eve == ZOO_SESSION_EVENT) {
				return;  // deal in zookeeper_init watcher
			}
			if (eve == ZOO_NOTWATCHING_EVENT || eve == ZOO_DELETED_EVENT) {
				if (eve == ZOO_DELETED_EVENT && d->cb) {
					//d->cb(zk_error::zk_no_node, std::optional<std::string>{});
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
				//d->cb((zk_error)rc, value ? std::string(value, value_len) : std::move(dummy));
			}
			/*if constexpr (!Advanced) {
				delete d;
			}*/
		};

		int r = 0;
		/*if constexpr (Advanced) {
			auto data = std::make_shared<wget_userdata>(wfn, cb, std::move(gcb), this, path);
			r = zoo_awget(zh_, path.data(), wfn, data.get(), cb, data.get());
			std::lock_guard<std::mutex> lock(mtx_);
			releaser_.emplace((uint64_t)data.get(), std::move(data));
		}
		else {
			auto data = new wget_userdata(wfn, cb, std::move(gcb), this, path);
			r = zoo_awget(zh_, path.data(), nullptr, data, cb, data);
		}*/

		if (r != ZOK) {
			printf("get_path_value error: %s\n", zerror(r));
		}
	}

	// [create/delete] sub path event just for current path, if the path exists all the time
	void async_watch_sub_path(std::string_view path, get_children_callback gccb) {
		auto wfn = [](zhandle_t*, int eve, int, const char* path, void* watcherCtx) {
			auto d = static_cast<get_children_userdata*>(watcherCtx);
			if (eve == ZOO_SESSION_EVENT) {
				return;  // deal in zookeeper_init watcher
			}
			if (eve == ZOO_NOTWATCHING_EVENT || eve == ZOO_DELETED_EVENT) {
				if (eve == ZOO_DELETED_EVENT && d->cb) {
					d->cb(make_ec(ZOO_ERRORS::ZNONODE), zk_event::zk_dummy_event, {});
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
				d->cb(make_ec(rc), d->eve, std::move(children_path));
			}
		};

		auto data = std::make_shared<get_children_userdata>(
			wfn, children_completion, std::move(gccb), this, path);
		auto r = zoo_awget_children2(zh_, path.data(), wfn, data.get(),
			children_completion, data.get());
		std::lock_guard<std::mutex> lock(mtx_);
		releaser_.emplace((uint64_t)data.get(), std::move(data));
	}

	auto get_sub_path(std::string_view path) {
		struct String_vector strings{};
		struct Stat stat{};
		auto rc = zoo_wget_children2(zh_, path.data(), nullptr, nullptr, &strings, &stat);
		if (rc != ZOO_ERRORS::ZOK) {
			return std::make_tuple(make_ec(rc), std::vector<std::string>{});
		}

		size_t count = strings.count;
		std::vector<std::string> sub_paths;
		sub_paths.reserve(count);
		for (size_t i = 0; i < count; ++i) {
			sub_paths.emplace_back(std::string(strings.data[i]));
		}
		return std::make_tuple(make_ec(rc), std::move(sub_paths));
	}

	std::error_code recursive_get_sub_path(std::string_view path, std::deque<std::string>& subs) {
		auto [ec, sub_paths] = get_sub_path(path);
		if (ec) {
			return ec;
		}

		auto p = std::string(path);
		for (auto& sub : sub_paths) {
			auto full = p + "/" + sub;
			auto ec = recursive_get_sub_path(full, subs);
			if (ec) {
				return ec;
			}
			subs.emplace_back(std::move(full));
		}
		return {};
	}

	// Be careful, maybe block the completion thread.
	// Not the real async recursive, it is too difficult
	void async_recursive_get_sub_path(std::string_view path, recursive_get_children_callback cb) {
		struct usrdata {
			recursive_get_children_callback callback;
			std::string prefix_path;
			cppzk* self;
		};
		auto data = new usrdata{ std::move(cb), std::string(path), this };
		zoo_awget_children2(zh_, path.data(), nullptr, nullptr,
			[](int rc, const struct String_vector* strings, const struct Stat*, const void* data) {
			auto ud = (usrdata*)data;
			std::shared_ptr<void> guard(nullptr, [ud](auto) {delete ud; });
			if (rc != ZOO_ERRORS::ZOK) {
				ud->callback(make_ec(rc), {});
				return;
			}

			std::vector<std::string> sub_paths;
			if (strings) {
				size_t count = strings->count;
				sub_paths.reserve(count);
				for (size_t i = 0; i < count; ++i) {
					sub_paths.emplace_back(std::string(strings->data[i]));
				}
			}
			std::deque<std::string> subs;
			for (auto& sub : sub_paths) {
				auto full = ud->prefix_path + "/" + sub;
				//sync, not good. But async is too difficult.
				auto ec = ud->self->recursive_get_sub_path(full, subs); 
				if (ec) {
					ud->callback(ec, {});
					return;
				}
				subs.emplace_back(std::move(full));
			}
			ud->callback(make_ec(rc), std::move(subs));
		}, data);
	}

	auto remove_watches(std::string_view path, int watch_type, operate_cb cb) {
		void_completion_t completion = [](int rc, const void* data) {
			auto cb = (operate_cb*)data;
			if ((*cb)) {
				(*cb)(make_ec(rc));
			}
			delete cb;
		};
		auto data = new operate_cb(std::move(cb));
		int r = 0;

		if (watch_type == 1) { //sub_path
			std::promise<std::vector<std::string>> pro;
			/*get_sub_path<false>(path, [&pro](zk_error, zk_event, std::vector<std::string>&& children) {
				pro.set_value(std::move(children));
			});*/
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
		return make_ec(r);
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
					//get_path_value("/zookeeper", nullptr);
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
	static std::error_code make_ec(int err) {
		return { err, zk::category() };
	}

	bool is_no_node(const std::error_code& err) {
		return err.value() == ZOO_ERRORS::ZNONODE;
	}

	bool is_node_exist(const std::error_code& err) {
		return err.value() == ZOO_ERRORS::ZNODEEXISTS;
	}

	bool is_create_event(zk_event eve) {
		return eve == zk_event::zk_created_event;
	}

	bool is_delete_event(zk_event eve) {
		return eve == zk_event::zk_deleted_event;
	}

	bool is_changed_event(zk_event eve) {
		return eve == zk_event::zk_changed_event;
	}

	bool is_session_event(zk_event eve) {
		return eve == zk_event::zk_session_event;
	}

	bool is_notwatching_event(zk_event eve) {
		return eve == zk_event::zk_notwatching_event;
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


};
}  // namespace zk