#include <future>

#include "config_monitor.hpp"
#include "local_file/local_file.hpp"
#include "gtest/gtest.h"

using namespace std::chrono_literals;

class local_file_test : public ::testing::Test {
public:
    std::string test_path_ = "./test.log";
    std::string test_path_value = "5201314";

    std::string main_path_ = "./test_sub_path";
    std::string sub_path1_ = "1.log";
    std::string value1_ = "111";
    std::string sub_path2_ = "2.log";
    std::string value2_ = "222";
    std::string sub_path3_ = "3.log";
    std::string value3_ = "333";

    std::string async_test_path_ = "./async_test.log";
    std::string async_test_path_value_ = "5201314";

    std::string async_main_path_ = "./test_sub_path";
    std::string async_sub_path1_ = "1.log";
    std::string async_value1_ = "111";
    std::string async_sub_path2_ = "2.log";
    std::string async_value2_ = "222";
    std::string async_sub_path3_ = "3.log";
    std::string async_value3_ = "333";

public:
    static void SetUpTestSuite() {
        cm::config_monitor<loc::loc_file>::instance().init();
    }

    static void TearDownTestSuite() {
    }

    auto create_sub_path(std::string_view sub_path, std::string_view value) {
        std::string full_path = main_path_ + "/" + std::string(sub_path);
        return cm::config_monitor<loc::loc_file>::instance().create_path(full_path, value);
    }
};

TEST_F(local_file_test, create_path) {
    auto [_, path] = cm::config_monitor<loc::loc_file>::instance().create_path(test_path_);
    EXPECT_EQ(path, test_path_);
};

TEST_F(local_file_test, set_path_value) {
    auto ec = cm::config_monitor<loc::loc_file>::instance().set_path_value(test_path_, test_path_value);
    EXPECT_EQ(ec.value(), 0);
};

TEST_F(local_file_test, watch_path) {
    auto [_, path_value] = cm::config_monitor<loc::loc_file>::instance().watch_path(test_path_);
    EXPECT_EQ(path_value, test_path_value);
};

TEST_F(local_file_test, watch_sub_path) {
    cm::config_monitor<loc::loc_file>::instance().create_path(main_path_ + "/" + sub_path1_, value1_);
    cm::config_monitor<loc::loc_file>::instance().create_path(main_path_ + "/" + sub_path2_, value2_);
    cm::config_monitor<loc::loc_file>::instance().create_path(main_path_ + "/" + sub_path3_, value3_);

    auto [e, values] = cm::config_monitor<loc::loc_file>::instance().watch_sub_path<false>(main_path_);
    EXPECT_EQ(e.value(), 0);
    EXPECT_EQ(3, values.size());

    auto [ec, mapping_values] = cm::config_monitor<loc::loc_file>::instance().watch_sub_path(main_path_);
    EXPECT_EQ(value1_, mapping_values[sub_path1_]);
    EXPECT_EQ(value2_, mapping_values[sub_path2_]);
    EXPECT_EQ(value3_, mapping_values[sub_path3_]);
};

TEST_F(local_file_test, del_path) {
    auto ec = cm::config_monitor<loc::loc_file>::instance().del_path(test_path_);
    EXPECT_EQ(!ec, true);

    ec = cm::config_monitor<loc::loc_file>::instance().del_path(main_path_);
    EXPECT_EQ(!ec, true);
};



TEST_F(local_file_test, async_create_path) {
    std::promise<std::pair<std::error_code, std::string>> pro;
    cm::config_monitor<loc::loc_file>::instance().async_create_path(
        async_test_path_, [&pro](const std::error_code& ec, std::string&& path) {
        pro.set_value({ ec, std::move(path) });
    });
    auto [ec, new_path] = pro.get_future().get();
    EXPECT_EQ(ec.value(), 0);
    EXPECT_EQ(new_path, async_test_path_);
};

TEST_F(local_file_test, async_set_path_value) {
    std::promise<std::error_code> pro;
    cm::config_monitor<loc::loc_file>::instance().async_set_path_value(
        async_test_path_, async_test_path_value_, [&pro](const std::error_code& ec) {
        pro.set_value(ec);
    });
    EXPECT_EQ(pro.get_future().get().value(), 0);
};

TEST_F(local_file_test, async_watch_path) {
    using delay_type = std::promise<std::pair<cm::path_event, std::string>>;
    auto pro = std::make_shared<delay_type>();
    cm::config_monitor<loc::loc_file>::instance().async_watch_path(
        async_test_path_, [&pro](cm::path_event eve, std::string&& value) {
        pro->set_value({ eve , std::move(value) });
    });
    auto [eve0, value0] = pro->get_future().get();
    EXPECT_EQ(eve0, cm::path_event::changed);
    EXPECT_EQ(value0, async_test_path_value_);

    pro = std::make_shared<delay_type>();
    std::string new_value = "this is changed test";
    cm::config_monitor<loc::loc_file>::instance().set_path_value(async_test_path_, new_value);
    auto [eve1, value1] = pro->get_future().get();
    EXPECT_EQ(eve1, cm::path_event::changed);
    EXPECT_EQ(value1, new_value);

    pro = std::make_shared<delay_type>();
    cm::config_monitor<loc::loc_file>::instance().del_path(async_test_path_);
    auto [eve2, value2] = pro->get_future().get();
    EXPECT_EQ(eve2, cm::path_event::del);
    EXPECT_EQ(value2, "");
};

TEST_F(local_file_test, async_watch_sub_path) {
   /* std::promise<void> pro;
    cm::config_monitor<loc::loc_file>::instance().async_create_path(
        async_main_path_ + "/" + async_sub_path1_, [&pro](const std::error_code& ec, std::string&& path) {
        pro.set_value();
    }, async_value1_);
    pro.get_future().get();

    cm::config_monitor<loc::loc_file>::instance().async_create_path(
        async_main_path_ + "/" + async_sub_path2_, [&pro](const std::error_code& ec, std::string&& path) {
        pro.set_value();
    }, async_value2_);
    pro.get_future().get();

    cm::config_monitor<loc::loc_file>::instance().async_create_path(
        async_main_path_ + "/" + async_sub_path3_, [&pro](const std::error_code& ec, std::string&& path) {
        pro.set_value();
    }, async_value3_);
    pro.get_future().get();
    

    cm::config_monitor<loc::loc_file>::instance().async_watch_sub_path<false>(
        main_path_, [](cm::path_event eve, std::string&& v) {
    
    });
    EXPECT_EQ(e.value(), 0);
    EXPECT_EQ(3, values.size());

    auto [ec, mapping_values] = cm::config_monitor<loc::loc_file>::instance().watch_sub_path(main_path_);
    EXPECT_EQ(value1_, mapping_values[sub_path1_]);
    EXPECT_EQ(value2_, mapping_values[sub_path2_]);
    EXPECT_EQ(value3_, mapping_values[sub_path3_]);*/
};

TEST_F(local_file_test, async_del_path) {
    std::promise<std::error_code> pro;
    cm::config_monitor<loc::loc_file>::instance().async_del_path(
        async_test_path_, [&pro](const std::error_code& ec) {
        pro.set_value(ec);
    });
    EXPECT_EQ(pro.get_future().get().value(), 0);

    std::promise<std::error_code> pro1;
    cm::config_monitor<loc::loc_file>::instance().async_del_path(
        async_main_path_, [&pro1](const std::error_code& ec) {
        pro1.set_value(ec);
    });
    EXPECT_EQ(pro1.get_future().get().value(), 0);
};