#pragma once
#include <system_error>

namespace loc {

enum class file_err {
    ok,
    not_exist,
    already_exist,
    already_used
};

enum class file_event {
    dummy_event = 0,
    created_event = 1,
    deleted_event = 2,
    changed_event = 3,
    child_event = 4,
    notwatch_event = 4,
};

enum class file_create_mode {
    persistent = 0,
    ephemeral = 1,
    persistent_sequential = 2,
    ephemeral_sequential = 3,
    persistent_with_ttl = 5,
    persistent_sequential_with_ttl = 6
};

class file_error_category : public std::error_category {
public:
    virtual const char* name() const noexcept override {
        return "file_error::category";
    }

    virtual std::string message(int err_val) const override {
        switch (static_cast<file_err>(err_val)) {
        case file_err::ok:
            return "ok";
        case file_err::not_exist:
            return "file not exist";
        case file_err::already_exist:
            return "file already_exist";
        default:
            return "unknown error";
        }
    }
};

inline const std::error_category& category() {
    static file_error_category instance;
    return instance;
}

}  // namespace loc