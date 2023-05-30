#include <future>

#include "config_monitor.hpp"
#include "cppzk/cppzk.hpp"
#include "gtest/gtest.h"

using namespace std::chrono_literals;

struct zk_info {
	std::string ips;
	int tiemout;
};

class cppzk_test : public testing::TestWithParam<zk_info> {
private:	
	std::atomic<bool> run_ = true;

protected:
	void SetUp() override {	
		cm::config_monitor<zk::cppzk>::instance().init(GetParam().ips, GetParam().tiemout);
	}

	void TearDown() override {
	}
};

//TEST_P(cppzk_test, create_path) {
//	auto path_value = cm::config_monitor<zk::cppzk>::instance().create_path("/test", "333");
//	EXPECT_EQ(path_value, "/test");
//};

//INSTANTIATE_TEST_SUITE_P(cppzk_test_set, cppzk_test,
//						 ::testing::Values(zk_info{ "192.168.3.163:2181", 40000}));
