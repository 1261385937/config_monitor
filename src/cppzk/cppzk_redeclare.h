#pragma once
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
}  // namespace zk