#pragma once

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

/*enum class zk_state {
		zk_dummy_state = 0,
		zk_expired_session_state = -112,
		zk_auth_failed_state = -113,
		zk_connecting_state = 1,
		zk_associating_state = 2,
		zk_connected_state = 3,
		zk_readonly_state = 5,
		zk_notconnected_state = 999
};*/

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

/** zookeeper return constants **/
enum class zk_error {
	zk_ok = 0, /*!< Everything is OK */

	/** System and server-side errors.
	 * This is never thrown by the server, it shouldn't be used other than
	 * to indicate a range. Specifically error codes greater than this
	 * value, but lesser than {@link #ZAPIERROR}, are system errors. */
	zk_system_error = -1,
	zk_runtime_inconsistency = -2, /*!< A runtime inconsistency was found */
	zk_data_inconsistency = -3,    /*!< A data inconsistency was found */
	zk_connection_loss = -4,       /*!< Connection to the server has been lost */
	zk_marshalling_error = -5,     /*!< Error while marshalling or unmarshalling data */
	zk_unimplemented = -6,         /*!< Operation is unimplemented */
	zk_operation_timeout = -7,     /*!< Operation timeout */
	zk_bad_arguments = -8,         /*!< Invalid arguments */
	zk_invalid_state = -9,         /*!< Invliad zhandle state */

	/*!< No quorum of new config is connected and up-to-date with the leader of last commmitted
	config
	- try invoking reconfiguration after new servers are connected and synced */
	zk_new_config_no_quorum = -13,
	/*!< Reconfiguration requested while another reconfiguration is currently in progress.
	This is currently not supported. Please retry. */
	zk_reconfig_in_progress = -14,
	zk_ssl_connection_error = -15, /*!< The SSL connection Error */

	/** API errors.
	 * This is never thrown by the server, it shouldn't be used other than
	 * to indicate a range. Specifically error codes greater than this
	 * value are API errors (while values less than this indicate a
	 * {@link #ZSYSTEMERROR}).
	 */
	zk_api_error = -100,
	zk_no_node = -101,                    /*!< Node does not exist */
	zk_no_auth = -102,                    /*!< Not authenticated */
	zk_bad_version = -103,                /*!< Version conflict */
	zk_no_children_for_ephemerals = -108, /*!< Ephemeral nodes may not have children */
	zk_node_exists = -110,                /*!< The node already exists */
	zk_not_empty = -111,                  /*!< The node has children */
	zk_session_expired = -112,            /*!< The session has been expired by the server */
	zk_invalid_callback = -113,           /*!< Invalid callback specified */
	zk_invalid_acl = -114,                /*!< Invalid ACL specified */
	zk_auth_failed = -115,                /*!< Client authentication failed */
	zk_closing = -116,                    /*!< ZooKeeper is closing */
	zk_nothing = -117,                    /*!< (not error) no server responses to process */
	zk_session_moved = -118,              /*!<session moved to another server, so operation is ignored */
	zk_not_read_only = -119,              /*!< state-changing request is passed to read-only server */
	zk_ephemeral_on_local_session = -120, /*!< Attempt to create ephemeral node on a local session */
	zk_no_watcher = -121,                 /*!< The watcher couldn't be found */
	zk_reconfig_disabled = -123,          /*!< Attempts to perform a reconfiguration operation when
											 reconfiguration feature is disabled */
	zk_session_closed_require_sasl_auth = -124 /*!< The session has been closed by server because server requires client to do SASL
			authentication, but client is not configured with SASL authentication or configuted
			with SASL but failed (i.e. wrong credential used.). */
};
}  // namespace zk