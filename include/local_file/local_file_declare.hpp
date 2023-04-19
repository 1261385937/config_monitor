#pragma once

namespace loc {

enum class file_error {
    file_ok,
    file_not_exist,
    file_exist
};

enum class file_event {
    file_dummy_event = 0,
    file_created_event = 1,
    file_deleted_event = 2,
    file_changed_event = 3,
    file_child_event = 4,
};

enum class file_create_mode {
    file_persistent = 0,
    file_ephemeral = 1,
    file_persistent_sequential = 2,
    file_ephemeral_sequential = 3,
    file_persistent_with_ttl = 5,
    file_persistent_sequential_with_ttl = 6
};

}  // namespace loc