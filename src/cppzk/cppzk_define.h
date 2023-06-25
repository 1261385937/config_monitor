#pragma once
#include <functional>
#include <optional>
#include "cppzk_redeclare.h"
#include "zookeeper.h"

namespace zk {
inline std::unordered_map<zk_acl, ACL_vector> acl_mapping{
    {zk_acl::zk_open_acl_unsafe, ZOO_OPEN_ACL_UNSAFE},
    {zk_acl::zk_read_acl_unsafe, ZOO_READ_ACL_UNSAFE},
    {zk_acl::zk_creator_all_acl, ZOO_CREATOR_ALL_ACL}
};

using expired_callback = std::function<void()>;
using create_callback = std::function<void(const std::error_code&, std::string&&)>;
using set_callback = std::function<void(zk_error)>;
using delete_callback = std::function<void(zk_error)>;
using exists_callback = std::function<void(zk_error, zk_event)>;
using get_callback = std::function<void(zk_error, std::optional<std::string>&&)>;
using get_children_callback = std::function<void(zk_error, zk_event, std::vector<std::string>&&)>;
using recursive_get_children_callback = std::function<void(zk_error, std::deque<std::string>&&)>;

class cppzk;
struct user_data {};
struct exists_userdata : user_data {
    watcher_fn wfn;
    stat_completion_t completion;
    exists_callback cb;
    cppzk* self;
    std::string path;
    zk_event eve = zk_event::zk_dummy_event;

    exists_userdata(watcher_fn f, stat_completion_t c,
                    exists_callback callbback, cppzk* ptr, std::string_view p)
        : wfn(f), completion(c), cb(std::move(callbback)), self(ptr), path(p) {}
};
struct wget_userdata : user_data {
    watcher_fn wfn;
    data_completion_t completion;
    get_callback cb;
    cppzk* self;
    std::string path;

    wget_userdata(watcher_fn f, data_completion_t c,
                  get_callback callbback, cppzk* ptr, std::string_view p)
        : wfn(f), completion(c), cb(std::move(callbback)), self(ptr), path(p) {}
};
struct get_children_userdata : user_data {
    watcher_fn wfn;
    strings_stat_completion_t children_completion;
    get_children_callback cb;
    cppzk* self;
    zk_event eve = zk_event::zk_dummy_event;
    std::string path;

    get_children_userdata(watcher_fn f, strings_stat_completion_t c,
                          get_children_callback callbback, cppzk* ptr, std::string_view p)
        : wfn(f), children_completion(c), cb(std::move(callbback)), self(ptr), path(p) {}
};
}  // namespace zk