#include <future>

#include "config_monitor.hpp"
#include "local_file/local_file.hpp"
#include "gtest/gtest.h"

using namespace std::chrono_literals;

class local_file_test : public ::testing::Test {
public:
    std::string test_path = "./test.log";
    std::string async_test_path = "./async_test.log";
    std::string async_main_path = "./async_test_sub_path";

public:
    static void SetUpTestSuite() {
        cm::config_monitor<loc::loc_file>::instance().init();
    }

    void SetUp() override {
    }

    static void TearDownTestSuite() {
    }

    void TearDown() override {
        std::promise<std::error_code> pro1;
        cm::config_monitor<loc::loc_file>::instance().async_remove_watches(
            async_test_path, cm::watch_type::watch_path, [&pro1](const std::error_code& ec) {
            pro1.set_value(ec);
        });
        pro1.get_future().get();
        cm::config_monitor<loc::loc_file>::instance().del_path(test_path);
        cm::config_monitor<loc::loc_file>::instance().del_path(async_test_path);


        std::promise<std::error_code> pro2;
        cm::config_monitor<loc::loc_file>::instance().async_remove_watches(
            async_main_path, cm::watch_type::watch_sub_path, [&pro2](const std::error_code& ec) {
            pro2.set_value(ec);
        });
        pro2.get_future().get();
        cm::config_monitor<loc::loc_file>::instance().del_path(async_main_path);
    }
};

TEST_F(local_file_test, create_path) {
    auto [_, path] = cm::config_monitor<loc::loc_file>::instance().create_path(test_path);
    EXPECT_EQ(path, test_path);
};

TEST_F(local_file_test, set_path_value) {
    cm::config_monitor<loc::loc_file>::instance().create_path(test_path);

    auto ec = cm::config_monitor<loc::loc_file>::instance().set_path_value(test_path, "5201314");
    EXPECT_EQ(ec.value(), 0);
};

TEST_F(local_file_test, watch_path) {
    std::string test_path_value = "5201314";
    cm::config_monitor<loc::loc_file>::instance().create_path(test_path);
    cm::config_monitor<loc::loc_file>::instance().set_path_value(test_path, test_path_value);

    auto [ec, path_value] = cm::config_monitor<loc::loc_file>::instance().watch_path(test_path);
    EXPECT_EQ(ec.value(), 0);
    EXPECT_EQ(path_value, test_path_value);
};

TEST_F(local_file_test, watch_sub_path) {
    std::string main_path = "./test_sub_path";
    std::string sub_path1_ = "1.log";
    std::string value1_ = "111";
    std::string sub_path2_ = "2.log";
    std::string value2_ = "222";
    std::string sub_path3_ = "3.log";
    std::string value3_ = "333";

    cm::config_monitor<loc::loc_file>::instance().create_path(main_path + "/" + sub_path1_, value1_);
    cm::config_monitor<loc::loc_file>::instance().create_path(main_path + "/" + sub_path2_, value2_);
    cm::config_monitor<loc::loc_file>::instance().create_path(main_path + "/" + sub_path3_, value3_);

    auto [e, values] = cm::config_monitor<loc::loc_file>::instance().watch_sub_path<false>(main_path);
    EXPECT_EQ(e.value(), 0);
    EXPECT_EQ((values[0] == value1_) || (values[0] == value2_) || (values[0] == value3_), true);
    EXPECT_EQ((values[1] == value1_) || (values[1] == value2_) || (values[1] == value3_), true);
    EXPECT_EQ((values[2] == value1_) || (values[2] == value2_) || (values[2] == value3_), true);

    auto [ec, mapping_values] = cm::config_monitor<loc::loc_file>::instance().watch_sub_path(main_path);
    EXPECT_EQ(value1_, mapping_values[sub_path1_]);
    EXPECT_EQ(value2_, mapping_values[sub_path2_]);
    EXPECT_EQ(value3_, mapping_values[sub_path3_]);

    cm::config_monitor<loc::loc_file>::instance().del_path(main_path);
};

TEST_F(local_file_test, del_path) {
    std::string delete_test_path = "./delete_test.log";
    cm::config_monitor<loc::loc_file>::instance().create_path(delete_test_path);

    auto ec = cm::config_monitor<loc::loc_file>::instance().del_path(delete_test_path);
    EXPECT_EQ(!ec, true);
};



TEST_F(local_file_test, async_create_path) {
    std::promise<std::pair<std::error_code, std::string>> pro;
    cm::config_monitor<loc::loc_file>::instance().async_create_path(
        async_test_path, [&pro](const std::error_code& ec, std::string&& path) {
        pro.set_value({ ec, std::move(path) });
    });
    auto [ec, new_path] = pro.get_future().get();
    EXPECT_EQ(ec.value(), 0);
    EXPECT_EQ(new_path, async_test_path);
};

TEST_F(local_file_test, async_set_path_value) {
    std::string async_test_path_value = "5201314";
    cm::config_monitor<loc::loc_file>::instance().create_path(async_test_path);

    std::promise<std::error_code> pro;
    cm::config_monitor<loc::loc_file>::instance().async_set_path_value(
        async_test_path, async_test_path_value, [&pro](const std::error_code& ec) {
        pro.set_value(ec);
    });
    EXPECT_EQ(pro.get_future().get().value(), 0);
};

TEST_F(local_file_test, async_watch_path_original) {
    std::string async_test_path_value = "5201314";
    cm::config_monitor<loc::loc_file>::instance().create_path(async_test_path, async_test_path_value);

    using delay_type = std::promise<std::pair<cm::path_event, std::string>>;
    auto pro = std::make_shared<delay_type>();
    cm::config_monitor<loc::loc_file>::instance().async_watch_path(
        async_test_path, [&pro](cm::path_event eve, std::string&& value) {
        pro->set_value({ eve , std::move(value) });
    });
    auto [eve0, value0] = pro->get_future().get();
    EXPECT_EQ(eve0, cm::path_event::changed);
    EXPECT_EQ(value0, async_test_path_value);
};

TEST_F(local_file_test, async_watch_path_not_exist_then_create) {
    std::string async_test_path_value = "5201314";
    std::thread([this, async_test_path_value]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        cm::config_monitor<loc::loc_file>::instance().create_path(
            async_test_path, async_test_path_value);
    }).detach();

    using delay_type = std::promise<std::pair<cm::path_event, std::string>>;
    auto pro = std::make_shared<delay_type>();
    cm::config_monitor<loc::loc_file>::instance().async_watch_path(
        async_test_path, [&pro](cm::path_event eve, std::string&& value) {
        pro->set_value({ eve , std::move(value) });
    });
    auto [eve0, value0] = pro->get_future().get();
    EXPECT_EQ(eve0, cm::path_event::changed);
    EXPECT_EQ(value0, async_test_path_value);
};

TEST_F(local_file_test, async_watch_path_change_value) {
    std::string async_test_path_value = "5201314";
    cm::config_monitor<loc::loc_file>::instance().create_path(async_test_path, async_test_path_value);

    using delay_type = std::promise<std::pair<cm::path_event, std::string>>;
    std::shared_ptr<delay_type> pro;
    cm::config_monitor<loc::loc_file>::instance().async_watch_path(
        async_test_path, [&pro](cm::path_event eve, std::string&& value) {
        if (pro) {
            pro->set_value({ eve , std::move(value) });
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    pro = std::make_shared<delay_type>();
    std::string new_value = "this is changed test";
    cm::config_monitor<loc::loc_file>::instance().set_path_value(async_test_path, new_value);
    auto [eve1, value1] = pro->get_future().get();
    EXPECT_EQ(eve1, cm::path_event::changed);
    EXPECT_EQ(value1, new_value);
};

TEST_F(local_file_test, async_watch_path_delete_path) {
    std::string async_test_path_value = "5201314";
    cm::config_monitor<loc::loc_file>::instance().create_path(async_test_path, async_test_path_value);

    using delay_type = std::promise<std::pair<cm::path_event, std::string>>;
    std::shared_ptr<delay_type> pro;
    cm::config_monitor<loc::loc_file>::instance().async_watch_path(
        async_test_path, [&pro](cm::path_event eve, std::string&& value) {
        if (pro) {
            pro->set_value({ eve , std::move(value) });
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    pro = std::make_shared<delay_type>();
    cm::config_monitor<loc::loc_file>::instance().del_path(async_test_path);
    auto [eve2, value2] = pro->get_future().get();
    EXPECT_EQ(eve2, cm::path_event::del);
    EXPECT_EQ(value2, "");
};

TEST_F(local_file_test, async_watch_sub_path_original_no_mapping) {
    std::string sub_path1 = "1.log";
    std::string value1 = "111";
    std::string sub_path2 = "2.log";
    std::string value2 = "222";
    std::string sub_path3 = "3.log";
    std::string value3 = "333";
    cm::config_monitor<loc::loc_file>::instance().create_path(async_main_path + "/" + sub_path1, value1);
    cm::config_monitor<loc::loc_file>::instance().create_path(async_main_path + "/" + sub_path2, value2);
    cm::config_monitor<loc::loc_file>::instance().create_path(async_main_path + "/" + sub_path3, value3);

    auto pro = std::make_shared<std::promise<cm::path_event>>();
    std::set<std::string> values;
    cm::config_monitor<loc::loc_file>::instance().async_watch_sub_path<false>(
        async_main_path, [&pro, &values](cm::path_event eve, std::string&& v) {
        if (pro) {
            values.emplace(std::move(v));
            if (values.size() == 3) {
                pro->set_value(eve);
            }
        }
    });

    EXPECT_EQ(pro->get_future().get(), cm::path_event::changed);
    EXPECT_EQ(values.find(value1) != values.end(), true);
    EXPECT_EQ(values.find(value2) != values.end(), true);
    EXPECT_EQ(values.find(value3) != values.end(), true);
};

TEST_F(local_file_test, async_watch_sub_path_original_mapping) {
    std::string path1 = async_main_path + "/" + "11.log";
    std::string value1 = "xxx";
    std::string path2 = async_main_path + "/" + "12.log";
    std::string value2 = "yyy";
    std::string path3 = async_main_path + "/" + "13.log";
    std::string value3 = "zzz";
    cm::config_monitor<loc::loc_file>::instance().create_path(path1, value1);
    cm::config_monitor<loc::loc_file>::instance().create_path(path2, value2);
    cm::config_monitor<loc::loc_file>::instance().create_path(path3, value3);

    std::promise<cm::path_event> pro1;
    std::unordered_map<std::string, std::string> mapping_values;
    cm::config_monitor<loc::loc_file>::instance().async_watch_sub_path(
        async_main_path, [&pro1, &mapping_values](cm::path_event eve, std::string&& v, const std::string& path) {
        mapping_values.emplace(path, std::move(v));
        if (mapping_values.size() == 3) {
            pro1.set_value(eve);
        }
    });
    EXPECT_EQ(pro1.get_future().get(), cm::path_event::changed);
    EXPECT_EQ(value1, mapping_values[path1]);
    EXPECT_EQ(value2, mapping_values[path2]);
    EXPECT_EQ(value3, mapping_values[path3]);
};

TEST_F(local_file_test, async_del_path) {
    std::string delete_test_path = "./async_test_sub_path";
    cm::config_monitor<loc::loc_file>::instance().create_path(delete_test_path + "/test.log");
    std::promise<std::error_code> pro1;
    cm::config_monitor<loc::loc_file>::instance().async_del_path(
        delete_test_path, [&pro1](const std::error_code& ec) {
        pro1.set_value(ec);
    });
    EXPECT_EQ(pro1.get_future().get().value(), 0);
};