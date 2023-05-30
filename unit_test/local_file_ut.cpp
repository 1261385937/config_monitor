#include <future>

#include "config_monitor.hpp"
#include "local_file/local_file.hpp"
#include "gtest/gtest.h"

using namespace std::chrono_literals;

class local_file_test : public ::testing::Test{
public:
    static void SetUpTestSuite() {
        cm::config_monitor<loc::loc_file>::instance().init();
    }

    static void TearDownTestSuite() {
    }
};

TEST_F(local_file_test, create_path) {
    auto path = cm::config_monitor<loc::loc_file>::instance().create_path("./test.log", "444");
    EXPECT_EQ(path, "./test.log");
};

TEST_F(local_file_test, watch_path) {
    auto path_value = cm::config_monitor<loc::loc_file>::instance().watch_path("./test.log");
    EXPECT_EQ(path_value, "444");
};

TEST_F(local_file_test, del_path) {
    auto ok = cm::config_monitor<loc::loc_file>::instance().del_path("./test.log");
    EXPECT_EQ(ok, true);
};