#include <future>

#include "config_monitor.hpp"
#include "local_file/local_file.hpp"
#include "gtest/gtest.h"

using namespace std::chrono_literals;

class local_file_test : public ::testing::Test {
public:
    std::string main_path_ = "./test_sub_path";
    std::string test_path_ = "./test.log";

public:
    static void SetUpTestSuite() {
        cm::config_monitor<loc::loc_file>::instance().init();
    }

    static void TearDownTestSuite() {
    }
};

TEST_F(local_file_test, create_path) {
    auto path = cm::config_monitor<loc::loc_file>::instance().create_path(test_path_, "444");
    EXPECT_EQ(path, test_path_);
};

TEST_F(local_file_test, watch_path) {
    auto path_value = cm::config_monitor<loc::loc_file>::instance().watch_path(test_path_);
    EXPECT_EQ(path_value, "444");
};

TEST_F(local_file_test, set_path_value) {
    std::string new_value = "5201314";
    auto ok = cm::config_monitor<loc::loc_file>::instance().set_path_value(test_path_, new_value);
    EXPECT_EQ(ok, true);

    auto path_value = cm::config_monitor<loc::loc_file>::instance().watch_path(test_path_);
    EXPECT_EQ(path_value, new_value);
};

TEST_F(local_file_test, watch_sub_path) {
    std::string path1 = "1.log";
    std::string full_path1 = main_path_ + "/" + path1;
    std::string value1 = "111";
    auto p1 = cm::config_monitor<loc::loc_file>::instance().create_path(full_path1, value1);
    EXPECT_EQ(p1, full_path1);

    std::string path2 = "2.log";
    std::string full_path2 = main_path_ + "/" + path2;
    std::string value2 = "222";
    auto p2 = cm::config_monitor<loc::loc_file>::instance().create_path(full_path2, value2);
    EXPECT_EQ(p2, full_path2);

    std::string path3 = "3.log";
    std::string full_path3 = main_path_ + "/" + path3;
    std::string value3 = "333";
    auto p3 = cm::config_monitor<loc::loc_file>::instance().create_path(full_path3, value3);
    EXPECT_EQ(p3, full_path3);

    auto values = cm::config_monitor<loc::loc_file>::instance().watch_sub_path<false>(main_path_);
    EXPECT_EQ(3, values.size());

    auto mapping_values = cm::config_monitor<loc::loc_file>::instance().watch_sub_path(main_path_);
    EXPECT_EQ(value1, mapping_values[path1]);
    EXPECT_EQ(value2, mapping_values[path2]);
    EXPECT_EQ(value3, mapping_values[path3]);
};

TEST_F(local_file_test, del_path) {
    auto ok = cm::config_monitor<loc::loc_file>::instance().del_path(test_path_);
    EXPECT_EQ(ok, true);

    ok = cm::config_monitor<loc::loc_file>::instance().del_path(main_path_);
    EXPECT_EQ(ok, true);
};