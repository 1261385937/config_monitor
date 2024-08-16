//#include <gtest/gtest.h>
//
//int main(int argc, char *argv[]) {
//   ::testing::InitGoogleTest(&argc, argv);
//   return RUN_ALL_TESTS();
//}

#include "cppzk/cppzk.hpp"

int main() {
	zk::cppzk cpp_zk;
	cpp_zk.initialize("192.168.152.137:2181", 40000);
	
	cpp_zk.create_path("/1", "1", zk::zk_create_mode::zk_persistent);
	cpp_zk.create_path("/1/2/3", "123", zk::zk_create_mode::zk_persistent);
	cpp_zk.create_path("/1/2/4", "124", zk::zk_create_mode::zk_persistent);
	cpp_zk.create_path("/1/3/3", "133", zk::zk_create_mode::zk_persistent);
	cpp_zk.create_path("/1/3/4", "134", zk::zk_create_mode::zk_persistent);
	
	cpp_zk.async_create_path("/2", "2", zk::zk_create_mode::zk_persistent,
		[](const std::error_code& ec, std::string&& new_path) {
		auto str = ec.message();
		int x = 4;
	});
	cpp_zk.async_create_path("/2/2/3", "223", zk::zk_create_mode::zk_persistent,
		[](const std::error_code& ec, std::string&& new_path) {
		auto str = ec.message();
		int x = 4;
	});
	cpp_zk.async_create_path("/2/2/4", "224", zk::zk_create_mode::zk_persistent,
		[](const std::error_code& ec, std::string&& new_path) {
		auto str = ec.message();
		int x = 4;
	});
	cpp_zk.async_create_path("/2/3/3", "233", zk::zk_create_mode::zk_persistent,
		[](const std::error_code& ec, std::string&& new_path) {
		auto str = ec.message();
		int x = 4;
	});
	cpp_zk.async_create_path("/2/3/4", "234", zk::zk_create_mode::zk_persistent,
		[](const std::error_code& ec, std::string&& new_path) {
		auto str = ec.message();
		int x = 4;
	});
	
	cpp_zk.async_recursive_get_sub_path("/1",
		[&](const std::error_code& ec, std::deque<std::string>&& subs) {
		auto str = ec.message();
		int x = 4;
		cpp_zk.async_delete_path("/1", [](const std::error_code& ec) {
			auto str = ec.message();
			int x = 4;
		});
	});

	Sleep(1000);
	cpp_zk.async_delete_path("/2", [](const std::error_code& ec) {
		auto str = ec.message();
		int x = 4;
	});

	Sleep(1000000000000);
}