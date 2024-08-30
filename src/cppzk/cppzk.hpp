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
#include "cppzk_redeclare.h"

namespace zk {
class cppzk {
private:
	zhandle_t* zh_{};
	std::string hosts_;
	int unused_flags_ = 0;
	std::string schema_;
	std::string credential_;
	std::string cert_;

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
	void initialize(std::string_view hosts, int session_timeout_ms, std::string_view schema = "",
		std::string_view credential = "", const char* cert = "", int unused_flags = 0) {
		hosts_ = hosts;
		session_timeout_ms_ = session_timeout_ms;
		schema_ = schema;
		credential_ = credential;
		cert_ = cert;
		unused_flags_ = unused_flags;
		connect_server();
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

		auto sp_path = split_path(path);
		for (size_t i = 0; i < sp_path.size() - 1; ++i) {
			auto rc = zoo_create2_ttl(zh_, sp_path[i].data(), nullptr, -1,
				&acl_mapping[acl], ZOO_PERSISTENT, -1, nullptr, 0, nullptr);
			if ((rc != ZOO_ERRORS::ZOK) && (rc != ZOO_ERRORS::ZNODEEXISTS)) { //create error
				return std::make_tuple(make_ec(rc), std::string{});
			}
		}
		auto value_ptr = !value.has_value() ? nullptr : value.value().data();
		auto value_len = !value.has_value() ? -1 : (int)value.value().length();
		auto new_path_len = path.length() + 16; //16 for sequence number, maybe
		auto ptr = std::make_unique<char[]>(new_path_len);
		auto ret = zoo_create2_ttl(zh_, path.data(), value_ptr, value_len,
			&acl_mapping[acl], (int)mode, ttl, ptr.get(), (int)new_path_len, nullptr);
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
			auto val_len = !value.has_value() ? -1 : (int)value.value().length();
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

		struct create_userdata {
			std::deque<std::string> split_paths;
			std::optional<std::string> value;
			zk_create_mode mode;
			create_callback callback;
			int64_t ttl;
			zk_acl acl;
			cppzk* self;
			string_stat_completion_t completion;
		};
		auto completion = [](int rc, const char* str, const struct Stat*, const void* data) {
			auto cud = (create_userdata*)data;
			auto& cb = cud->callback;
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
			if (sp_path.size() == 1) { //create the last layer (whole path)
				auto val_ptr = !cud->value.has_value() ? nullptr : cud->value.value().data();
				auto val_len = !cud->value.has_value() ? -1 : (int)cud->value.value().length();
				zoo_acreate2_ttl(cud->self->zh_, sp_path[0].data(), val_ptr, val_len,
					&acl_mapping[cud->acl], (int)cud->mode, cud->ttl, cud->completion, cud);
			}
			else {
				zoo_acreate2_ttl(cud->self->zh_, sp_path[0].data(), nullptr, -1,
					&acl_mapping[cud->acl], ZOO_PERSISTENT, -1, cud->completion, cud);
			}
		};

		std::string first_layer = sp_path[0];
		auto cud = new create_userdata{ split_path(path),
			std::move(value), mode, std::move(ccb), ttl, acl, this, completion };
		zoo_acreate2_ttl(zh_, first_layer.data(), nullptr, -1, &acl_mapping[acl], ZOO_PERSISTENT, -1,
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
				const std::error_code& ec, std::deque<std::string>&& subs) {
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

	auto set_path_value(std::string_view path, std::string_view value) {
		auto rc = zoo_set2(zh_, path.data(), value.data(), (int)value.length(), -1, nullptr);
		return make_ec(rc);
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

	auto get_path_value(std::string_view path) {
		constexpr int size = 1024;
		char buf[size]{};
		int len = size;
		Stat stat{};
		auto rc = zoo_get(zh_, path.data(), 0, buf, &len, &stat);
		if ((rc != ZOO_ERRORS::ZOK) || (len == -1)) {
			return std::make_tuple(make_ec(rc), std::optional<std::string>{});
		}

		auto real_len = stat.dataLength;
		if (real_len <= size) {
			auto val = std::string{ buf, (size_t)real_len };
			return std::make_tuple(make_ec(rc), std::optional<std::string>(std::move(val)));
		}

		//buf is not enough, re-get the data
		auto ptr = std::make_unique<char[]>(static_cast<size_t>(real_len));
		rc = zoo_get(zh_, path.data(), 0, ptr.get(), &real_len, nullptr);
		if (rc != ZOO_ERRORS::ZOK) {
			return std::make_tuple(make_ec(rc), std::optional<std::string>{});
		}
		auto value = std::string{ buf, (size_t)real_len };
		return std::make_tuple(make_ec(rc), std::optional<std::string>(std::move(value)));
	}

	template<bool Advanced = false>
	void async_get_path_value(std::string_view path, get_callback cb) {
		auto wfn = [](zhandle_t*, int eve, int, const char* path, void* watcherCtx) {
			auto d = static_cast<wget_userdata*>(watcherCtx);
			if (eve == ZOO_SESSION_EVENT) {
				return;  // deal in zookeeper_init watcher
			}
			if (eve == ZOO_DELETED_EVENT) {
				d->cb(make_ec(ZOO_ERRORS::ZNONODE),
					(zk_event)ZOO_DELETED_EVENT, path, std::optional<std::string>{});
				std::lock_guard<std::mutex> lock(d->self->mtx_);
				d->self->releaser_.erase((uint64_t)watcherCtx);
				return;
			}
			d->path = path;
			d->eve = (zk_event)eve;
			zoo_awget(d->self->zh_, path, d->wfn, watcherCtx, d->completion, watcherCtx);
		};
		auto gcb = [](int rc, const char* val, int len, const struct Stat*, const void* data) {
			auto d = (wget_userdata*)data;
			d->cb(make_ec(rc), d->eve, d->path,
				val ? std::string(val, len) : std::optional<std::string>{});
			if constexpr (!Advanced) {
				delete d;
			}
		};

		if constexpr (Advanced) {
			auto data = std::make_shared<wget_userdata>(wfn, gcb, std::move(cb), this, path);
			zoo_awget(zh_, path.data(), wfn, data.get(), gcb, data.get());
			std::lock_guard<std::mutex> lock(mtx_);
			releaser_.emplace((uint64_t)data.get(), std::move(data));
		}
		else {
			auto data = new wget_userdata(wfn, gcb, std::move(cb), this, path);
			zoo_awget(zh_, path.data(), nullptr, nullptr, gcb, data);
		}
	}

	// [create/delete/changed] event just for current path
	void watch_path_event(std::string_view path, exists_callback cb) {
		auto wfn = [](zhandle_t*, int eve, int, const char* path, void* watcherCtx) {
			auto eud = (exists_userdata*)watcherCtx;
			if (eve == ZOO_SESSION_EVENT) {
				return;  // deal in zookeeper_init watcher
			}
			eud->eve = (zk_event)eve;
			zoo_awexists(eud->self->zh_, path, eud->wfn, watcherCtx, eud->completion, watcherCtx);
		};
		auto exists_completion = [](int rc, const struct Stat*, const void* data) {
			auto d = (exists_userdata*)data;
			d->cb(make_ec(rc), d->eve);
		};

		auto data = std::make_shared<exists_userdata>(wfn, exists_completion, std::move(cb), this);
		zoo_awexists(zh_, path.data(), wfn, data.get(), exists_completion, data.get());
		std::lock_guard lock(mtx_);
		releaser_.emplace((uint64_t)data.get(), std::move(data));
	}

	auto get_sub_path(std::string_view path) {
		struct String_vector strings {};
		struct Stat stat {};
		auto rc = zoo_wget_children2(zh_, path.data(), nullptr, nullptr, &strings, &stat);
		if (rc != ZOO_ERRORS::ZOK) {
			return std::make_tuple(make_ec(rc), std::vector<std::string>{});
		}

		size_t count = strings.count;
		std::vector<std::string> sub_paths;
		sub_paths.reserve(count);
		for (size_t i = 0; i < count; ++i) {
			sub_paths.emplace_back(std::string(path) + "/" + std::string(strings.data[i]));
		}
		return std::make_tuple(make_ec(rc), std::move(sub_paths));
	}

	template<bool Advanced = false>
	void async_get_sub_path(std::string_view path, get_children_callback cb) {
		auto wfn = [](zhandle_t*, int eve, int, const char* path, void* watcherCtx) {
			auto d = static_cast<get_children_userdata*>(watcherCtx);
			if (eve == ZOO_SESSION_EVENT) {
				return;  // deal in zookeeper_init watcher
			}
			if (eve == ZOO_DELETED_EVENT) {
				std::lock_guard<std::mutex> lock(d->self->mtx_);
				d->self->releaser_.erase((uint64_t)watcherCtx);
				return;
			}
			zoo_awget_children2(d->self->zh_, path, d->wfn, watcherCtx, d->completion, watcherCtx);
		};
		auto completion = [](int rc, const String_vector* strings, const Stat*, const void* data) {
			auto d = (get_children_userdata*)data;
			std::vector<std::string> children_path;
			if (strings) {
				size_t count = strings->count;
				children_path.reserve(count);
				for (size_t i = 0; i < count; ++i) {
					children_path.emplace_back(std::string(strings->data[i]));
				}
			}
			d->cb(make_ec(rc), std::move(children_path));
			if constexpr (!Advanced) {
				delete d;
			}
		};

		if constexpr (Advanced) {
			auto data = std::make_shared<get_children_userdata>(
				wfn, completion, std::move(cb), this, path);
			zoo_awget_children2(zh_, path.data(), wfn, data.get(), completion, data.get());
			std::lock_guard<std::mutex> lock(mtx_);
			releaser_.emplace((uint64_t)data.get(), std::move(data));
		}
		else {
			auto data = new get_children_userdata(wfn, completion, std::move(cb), this, path);
			zoo_awget_children2(zh_, path.data(), nullptr, nullptr, completion, data);
		}
	}

	std::error_code recursive_get_sub_path(std::string_view path, std::deque<std::string>& subs) {
		auto [ec, sub_paths] = get_sub_path(path);
		if (ec) {
			return ec;
		}

		auto p = std::string(path);
		for (auto&& sub : sub_paths) {
			auto rec = recursive_get_sub_path(sub, subs);
			if (rec) {
				return rec;
			}
			subs.emplace_back(std::move(sub));
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

	//TODO remove watch, should delete releaser_
	auto remove_watches(std::string_view path, int watch_type) {
		if (watch_type == 0) { // path
			auto rc = zoo_remove_all_watches(zh_, path.data(), ZWATCHTYPE_DATA, 0);
			return make_ec(rc);
		}

		//sub path
		auto [ec, sub_paths] = get_sub_path(path);
		if (ec) {
			return ec;
		}
		for (auto& sub : sub_paths) {
			auto full = std::string(path) + "/" + sub;
			auto rc = zoo_remove_all_watches(zh_, full.data(), ZooWatcherType::ZWATCHTYPE_DATA, 0);
			if (rc) {
				return make_ec(rc);
			}
		}
		auto rc = zoo_remove_all_watches(zh_, path.data(), ZWATCHTYPE_CHILD, 0);
		return make_ec(rc);
	}

	void async_remove_watches(std::string_view path, int watch_type, operate_cb cb) {
		void_completion_t callback = [](int rc, const void* data) {
			auto cb = (operate_cb*)data;
			if ((*cb)) {
				(*cb)(make_ec(rc));
			}
			delete cb;
		};
		if (watch_type == 0) { //path
			auto data = new operate_cb(std::move(cb));
			zoo_aremove_all_watches(zh_, path.data(), ZWATCHTYPE_DATA, 0,
				(void_completion_t*)callback, data);
			return;
		}

		//sub path
		struct userdata {
			operate_cb callback;
			std::deque<std::string> subs;
			cppzk* self;
			void_completion_t completion;
		};
		void_completion_t completion = [](int rc, const void* data) {
			auto ud = (userdata*)data;
			auto& subs = ud->subs;
			auto& cb = ud->callback;
			subs.pop_front();
			if ((subs.empty())) {
				if (cb) {
					cb(make_ec(rc));
				}
				delete ud;
				return;
			}
			bool is_last_path = (subs.size() == 1llu);
			if (is_last_path) {// deal prefix
				zoo_aremove_all_watches(ud->self->zh_, subs[0].data(), ZWATCHTYPE_ANY, 0,
					(void_completion_t*)ud->completion, data);
			}
			else {
				zoo_aremove_all_watches(ud->self->zh_, subs[0].data(), ZWATCHTYPE_DATA, 0,
					(void_completion_t*)ud->completion, data);
			}
		};

		async_get_sub_path(path,
			[cb = std::move(cb), p = std::string(path), completion = std::move(completion), this](
				const std::error_code& ec, std::vector<std::string>&& subs) {
			if (ec) {
				cb(ec);
				return;
			}
			std::deque<std::string> sub_paths;
			for (auto& sub : subs) {
				sub_paths.emplace_back(p + "/" + std::move(sub));
			}
			sub_paths.emplace_back(std::move(p));

			auto data = new userdata{ std::move(cb), std::move(sub_paths), this, completion };
			if (data->subs.size() == 1llu) { //just deal prefix
				zoo_aremove_all_watches(zh_, p.data(), ZWATCHTYPE_ANY, 0,
					(void_completion_t*)completion, data);
				return;
			}
			zoo_aremove_all_watches(zh_, data->subs[0].data(), ZWATCHTYPE_DATA, 0,
				(void_completion_t*)completion, data);
		});
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
					get_path_value("/zookeeper");
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
		zh_ = zookeeper_init_ssl(hosts_.c_str(), cert_.data(),
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
		if (!schema_.empty() && !credential_.empty()) {
			auto r = zoo_add_auth(zh_, schema_.data(), credential_.data(), (int)credential_.size(),
				nullptr, nullptr);
			if (r != ZOK) {
				throw std::runtime_error(std::string("zoo_add_auth error: ") + zerror(r));
			}
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

	bool is_dummy_event(zk_event eve) {
		return eve == zk_event::zk_dummy_event;
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