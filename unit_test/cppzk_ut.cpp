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
protected:
    std::string test_path = "/test";
    inline static bool is_init_ = false;

    std::string async_test_path = "/async_test";

public:
    static void SetUpTestSuite() {
        //GetParam();
    }

    static void TearDownTestSuite() {

    }

    void SetUp() override {
        if (!is_init_) {
            is_init_ = true;
            cm::config_monitor<zk::cppzk>::instance().init(GetParam().ips, GetParam().tiemout);
        }
    }

    void TearDown() override {
        std::promise<std::error_code> pro1;
        cm::config_monitor<zk::cppzk>::instance().async_remove_watches(
            async_test_path, cm::watch_type::watch_path, [&pro1](const std::error_code& ec) {
            pro1.set_value(ec);
        });
        pro1.get_future().get();

        cm::config_monitor<zk::cppzk>::instance().del_path(test_path);
        cm::config_monitor<zk::cppzk>::instance().del_path(async_test_path);
    }
};

TEST_P(cppzk_test, create_path) {
    auto [ec, path] = cm::config_monitor<zk::cppzk>::instance().create_path(test_path, "333");
    EXPECT_EQ(ec.value(), 0);
    EXPECT_EQ(path, test_path);
};

TEST_P(cppzk_test, create_exist_path) {
    cm::config_monitor<zk::cppzk>::instance().create_path(test_path);
    auto [ec, path] = cm::config_monitor<zk::cppzk>::instance().create_path(test_path);
    EXPECT_FALSE(ec.value() == 0);
    EXPECT_EQ(path, "");
};

TEST_P(cppzk_test, set_path_value) {
    cm::config_monitor<zk::cppzk>::instance().create_path(test_path);

    auto ec = cm::config_monitor<zk::cppzk>::instance().set_path_value(test_path, "5201314");
    EXPECT_EQ(ec.value(), 0);
};

TEST_P(cppzk_test, set_not_exist_path_value) {
    auto ec = cm::config_monitor<zk::cppzk>::instance().set_path_value(test_path, "5201314");
    EXPECT_FALSE(ec.value() == 0);
};

TEST_P(cppzk_test, watch_path) {
    std::string test_path_value = "5201314";
    cm::config_monitor<zk::cppzk>::instance().create_path(test_path);
    cm::config_monitor<zk::cppzk>::instance().set_path_value(test_path, test_path_value);

    auto [ec, path_value] = cm::config_monitor<zk::cppzk>::instance().watch_path(test_path);
    EXPECT_EQ(ec.value(), 0);
    EXPECT_EQ(path_value, test_path_value);
};

TEST_P(cppzk_test, watch_not_exist_path) {
    auto [ec, path_value] = cm::config_monitor<zk::cppzk>::instance().watch_path(test_path);
    EXPECT_FALSE(ec.value() == 0);
    EXPECT_EQ(path_value, "");
};

TEST_P(cppzk_test, watch_sub_path) {
    std::string main_path = "/test_sub_path";
    std::string sub_path1_ = "1";
    std::string value1_ = "111";
    std::string sub_path2_ = "2";
    std::string value2_ = "222";
    std::string sub_path3_ = "3";
    std::string value3_ = "333";

    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path1_, value1_);
    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path2_, value2_);
    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path3_, value3_);

    auto [e, values] = cm::config_monitor<zk::cppzk>::instance().watch_sub_path<false>(main_path);
    EXPECT_EQ(e.value(), 0);
    EXPECT_EQ((values[0] == value1_) || (values[0] == value2_) || (values[0] == value3_), true);
    EXPECT_EQ((values[1] == value1_) || (values[1] == value2_) || (values[1] == value3_), true);
    EXPECT_EQ((values[2] == value1_) || (values[2] == value2_) || (values[2] == value3_), true);

    auto [ec, mapping_values] = cm::config_monitor<zk::cppzk>::instance().watch_sub_path(main_path);
    EXPECT_EQ(value1_, mapping_values[sub_path1_]);
    EXPECT_EQ(value2_, mapping_values[sub_path2_]);
    EXPECT_EQ(value3_, mapping_values[sub_path3_]);

    cm::config_monitor<zk::cppzk>::instance().del_path(main_path);
};

TEST_P(cppzk_test, watch_sub_path_not_exist_main_path) {
    std::string main_path = "/test_sub_path";
    auto [ec, values] = cm::config_monitor<zk::cppzk>::instance().watch_sub_path<false>(main_path);
    EXPECT_FALSE(ec.value() == 0);
    EXPECT_EQ(values.size(), 0);

    auto [ec1, mapping_values] = cm::config_monitor<zk::cppzk>::instance().watch_sub_path(main_path);
    EXPECT_FALSE(ec1.value() == 0);
    EXPECT_EQ(mapping_values.size(), 0);
};

TEST_P(cppzk_test, del_path) {
    std::string main_path = "/delete_test";
    std::string sub_path1_ = "1";
    std::string sub_path2_ = "2";
    std::string sub_path3_ = "3";

    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path1_ + "/" + "11");
    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path1_ + "/" + "12");
    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path1_ + "/" + "13");
    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path2_ + "/" + "21");
    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path2_ + "/" + "22");
    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path2_ + "/" + "23");
    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path3_ + "/" + "31");
    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path3_ + "/" + "32");
    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path3_ + "/" + "33");

    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path1_ + "/" + "11/1");
    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path2_ + "/" + "21/2");
    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path3_ + "/" + "31/3");

    auto ec = cm::config_monitor<zk::cppzk>::instance().del_path(main_path);
    EXPECT_EQ(!ec, true);
};

TEST_P(cppzk_test, del_not_exist_path) {
    std::string delete_test_path = "/delete_not_exist_test";
    auto ec = cm::config_monitor<zk::cppzk>::instance().del_path(delete_test_path);
    EXPECT_EQ(!ec, false);
};


TEST_P(cppzk_test, async_create_path) {
    std::promise<std::pair<std::error_code, std::string>> pro;
    cm::config_monitor<zk::cppzk>::instance().async_create_path(
        async_test_path, [&pro](const std::error_code& ec, std::string&& path) {
        pro.set_value({ ec, std::move(path) });
    });
    auto [ec, new_path] = pro.get_future().get();
    EXPECT_EQ(ec.value(), 0);
    EXPECT_EQ(new_path, async_test_path);
};

TEST_P(cppzk_test, async_create_exist_path) {
    cm::config_monitor<zk::cppzk>::instance().create_path(async_test_path);

    std::promise<std::pair<std::error_code, std::string>> pro;
    cm::config_monitor<zk::cppzk>::instance().async_create_path(
        async_test_path, [&pro](const std::error_code& ec, std::string&& path) {
        pro.set_value({ ec, std::move(path) });
    });
    auto [ec, new_path] = pro.get_future().get();
    EXPECT_FALSE(ec.value() == 0);
    EXPECT_EQ(new_path, "");
};

TEST_P(cppzk_test, async_set_path_value) {
    std::string async_test_path_value = "5201314";
    cm::config_monitor<zk::cppzk>::instance().create_path(async_test_path);

    std::promise<std::error_code> pro;
    cm::config_monitor<zk::cppzk>::instance().async_set_path_value(
        async_test_path, async_test_path_value, [&pro](const std::error_code& ec) {
        pro.set_value(ec);
    });
    EXPECT_EQ(pro.get_future().get().value(), 0);
};

TEST_P(cppzk_test, async_set_not_exist_path_value) {
    std::string async_test_path_value = "5201314";
    std::promise<std::error_code> pro;
    cm::config_monitor<zk::cppzk>::instance().async_set_path_value(
        async_test_path, async_test_path_value, [&pro](const std::error_code& ec) {
        pro.set_value(ec);
    });
    EXPECT_FALSE(pro.get_future().get().value() == 0);
};

TEST_P(cppzk_test, async_watch_path) {
    std::string async_test_path_value = "5201314";
    cm::config_monitor<zk::cppzk>::instance().create_path(async_test_path, async_test_path_value);

    using delay_type = std::promise<std::pair<cm::path_event, std::string>>;
    auto pro = std::make_shared<delay_type>();
    cm::config_monitor<zk::cppzk>::instance().async_watch_path(
        async_test_path, [&pro](cm::path_event eve, std::string&& value) {
        pro->set_value({ eve , std::move(value) });
    });
    auto [eve0, value0] = pro->get_future().get();
    EXPECT_EQ(eve0, cm::path_event::changed);
    EXPECT_EQ(value0, async_test_path_value);

  
};


INSTANTIATE_TEST_SUITE_P(cppzk_test_set, cppzk_test,
                         ::testing::Values(zk_info{ "192.168.3.163:2181", 40000 }));
