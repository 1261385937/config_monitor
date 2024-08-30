#pragma once
#include <system_error>
#include <optional>
#include "zookeeper.h"

// redefine according to zookeeper origin define
namespace zk {
enum class zk_event {
    zk_dummy_event = 0,
    zk_created_event = 1,
    zk_deleted_event = 2,
    zk_changed_event = 3,
    zk_child_event = 4,
    zk_session_event = -1,
    zk_notwatching_event = -2
};

enum class zk_loglevel {
    zk_log_level_error = 1,
    zk_log_level_warn = 2,
    zk_log_level_info = 3,
    zk_log_level_debug = 4
};

enum class zk_acl {  // Access Control List
    zk_open_acl_unsafe,
    zk_read_acl_unsafe,
    zk_creator_all_acl
};

enum class zk_create_mode {
    zk_persistent = 0,
    zk_ephemeral = 1,
    zk_persistent_sequential = 2,
    zk_ephemeral_sequential = 3,
    zk_container = 4,
    zk_persistent_with_ttl = 5,
    zk_persistent_sequential_with_ttl = 6
};

class zk_error_category : public std::error_category {
public:
    virtual const char* name() const noexcept override {
        return "zk_error::category";
    }

    virtual std::string message(int err_val) const override {
        return zerror(err_val);
    }
};

inline const std::error_category& category() {
    static zk_error_category instance;
    return instance;
}

inline std::unordered_map<zk_acl, ACL_vector> acl_mapping{
    {zk_acl::zk_open_acl_unsafe, ZOO_OPEN_ACL_UNSAFE},
    {zk_acl::zk_read_acl_unsafe, ZOO_READ_ACL_UNSAFE},
    {zk_acl::zk_creator_all_acl, ZOO_CREATOR_ALL_ACL}
};

using expired_callback = std::function<void()>;
using create_callback = std::function<void(const std::error_code&, std::string&&)>;
using operate_cb = std::function<void(const std::error_code&)>;
using exists_callback = std::function<void(const std::error_code&, zk_event)>;
using get_callback = std::function<void(
    const std::error_code&, zk_event, std::string_view, std::optional<std::string>&&)>;
using get_children_callback = std::function<void(
    const std::error_code&, std::vector<std::string>&&)>;
using recursive_get_children_callback = std::function<void(
    const std::error_code&, std::deque<std::string>&&)>;

class cppzk;
struct user_data {};
struct exists_userdata : user_data {
    watcher_fn wfn;
    stat_completion_t completion;
    exists_callback cb;
    cppzk* self;
    zk_event eve = zk_event::zk_dummy_event;

    exists_userdata(watcher_fn f, stat_completion_t c, exists_callback callbback, cppzk* ptr)
        : wfn(f), completion(c), cb(std::move(callbback)), self(ptr) {}
};
struct wget_userdata : user_data {
    watcher_fn wfn;
    data_completion_t completion;
    get_callback cb;
    cppzk* self;
    zk_event eve = zk_event::zk_dummy_event;
    std::string path;

    wget_userdata(watcher_fn f, data_completion_t c,
                  get_callback callbback, cppzk* ptr, std::string_view p)
        : wfn(f), completion(c), cb(std::move(callbback)), self(ptr), path(p) {}
};
struct get_children_userdata : user_data {
    watcher_fn wfn;
    strings_stat_completion_t completion;
    get_children_callback cb;
    cppzk* self;
    zk_event eve = zk_event::zk_dummy_event;
    std::string path;

    get_children_userdata(watcher_fn f, strings_stat_completion_t c,
                          get_children_callback callback, cppzk* ptr, std::string_view p)
        : wfn(f), completion(c), cb(std::move(callback)), self(ptr), path(p) {}
};
}  // namespace zk