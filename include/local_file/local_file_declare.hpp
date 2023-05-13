#pragma once

namespace loc {

enum class file_error {
    ok,
    not_exist,
    already_exist
};

enum class file_event {
    dummy_event = 0,
    created_event = 1,
    deleted_event = 2,
    changed_event = 3,
    child_event = 4,
};

enum class file_create_mode {
    persistent = 0,
    ephemeral = 1,
    persistent_sequential = 2,
    ephemeral_sequential = 3,
    persistent_with_ttl = 5,
    persistent_sequential_with_ttl = 6
};

}  // namespace loc