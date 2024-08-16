//#include <future>
//
//#include "config_monitor.hpp"
//#include "cppzk/cppzk.hpp"
//#include "gtest/gtest.h"
//
//using namespace std::chrono_literals;
//
//struct zk_info {
//    std::string ips;
//    int tiemout;
//};
//
//class cppzk_test : public testing::TestWithParam<zk_info> {
//protected:
//    std::string test_path = "/test";
//    inline static bool is_init_ = false;
//
//    std::string async_test_path = "/async_test";
//    std::string async_main_path = "/async_test_sub_path";
//
//public:
//    static void SetUpTestSuite() {
//        //GetParam();
//    }
//
//    static void TearDownTestSuite() {
//
//    }
//
//    void SetUp() override {
//        if (!is_init_) {
//            is_init_ = true;
//            cm::config_monitor<zk::cppzk>::instance().init(GetParam().ips, GetParam().tiemout);
//        }
//    }
//
//    void TearDown() override {
//        std::promise<std::error_code> pro;
//        cm::config_monitor<zk::cppzk>::instance().async_remove_watches(
//            async_test_path, cm::watch_type::watch_path, [&pro](const std::error_code& ec) {
//            pro.set_value(ec);
//        });
//        pro.get_future().get();
//
//        cm::config_monitor<zk::cppzk>::instance().del_path(test_path);
//        cm::config_monitor<zk::cppzk>::instance().del_path(async_test_path);
//
//        std::promise<std::error_code> pro1;
//        cm::config_monitor<zk::cppzk>::instance().async_remove_watches(
//            async_main_path, cm::watch_type::watch_sub_path, [&pro1](const std::error_code& ec) {
//            pro1.set_value(ec);
//        });
//        pro1.get_future().get();
//
//        cm::config_monitor<zk::cppzk>::instance().del_path(async_main_path);
//    }
//};
//
//TEST_P(cppzk_test, create_path) {
//    auto [ec, path] = cm::config_monitor<zk::cppzk>::instance().create_path(test_path, "333");
//    EXPECT_EQ(ec.value(), 0);
//    EXPECT_EQ(path, test_path);
//};
//
//TEST_P(cppzk_test, create_exist_path) {
//    cm::config_monitor<zk::cppzk>::instance().create_path(test_path);
//    auto [ec, path] = cm::config_monitor<zk::cppzk>::instance().create_path(test_path);
//    EXPECT_FALSE(ec.value() == 0);
//    EXPECT_EQ(path, "");
//};
//
//TEST_P(cppzk_test, set_path_value) {
//    cm::config_monitor<zk::cppzk>::instance().create_path(test_path);
//
//    auto ec = cm::config_monitor<zk::cppzk>::instance().set_path_value(test_path, "5201314");
//    EXPECT_EQ(ec.value(), 0);
//};
//
//TEST_P(cppzk_test, set_not_exist_path_value) {
//    auto ec = cm::config_monitor<zk::cppzk>::instance().set_path_value(test_path, "5201314");
//    EXPECT_FALSE(ec.value() == 0);
//};
//
//TEST_P(cppzk_test, watch_path) {
//    std::string test_path_value = "5201314";
//    cm::config_monitor<zk::cppzk>::instance().create_path(test_path);
//    cm::config_monitor<zk::cppzk>::instance().set_path_value(test_path, test_path_value);
//
//    auto [ec, path_value] = cm::config_monitor<zk::cppzk>::instance().watch_path(test_path);
//    EXPECT_EQ(ec.value(), 0);
//    EXPECT_EQ(path_value, test_path_value);
//};
//
//TEST_P(cppzk_test, watch_not_exist_path) {
//    auto [ec, path_value] = cm::config_monitor<zk::cppzk>::instance().watch_path(test_path);
//    EXPECT_FALSE(ec.value() == 0);
//    EXPECT_EQ(path_value, "");
//};
//
//TEST_P(cppzk_test, watch_sub_path) {
//    std::string main_path = "/test_sub_path";
//    std::string sub_path1_ = "1";
//    std::string value1_ = "111";
//    std::string sub_path2_ = "2";
//    std::string value2_ = "222";
//    std::string sub_path3_ = "3";
//    std::string value3_ = "333";
//
//    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path1_, value1_);
//    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path2_, value2_);
//    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path3_, value3_);
//
//    auto [e, values] = cm::config_monitor<zk::cppzk>::instance().watch_sub_path(main_path);
//    EXPECT_EQ(e.value(), 0);
//    EXPECT_EQ((values[0] == value1_) || (values[0] == value2_) || (values[0] == value3_), true);
//    EXPECT_EQ((values[1] == value1_) || (values[1] == value2_) || (values[1] == value3_), true);
//    EXPECT_EQ((values[2] == value1_) || (values[2] == value2_) || (values[2] == value3_), true);
//
//    auto [ec, mapping_values] = cm::config_monitor<zk::cppzk>::instance().watch_sub_path(main_path);
//    EXPECT_EQ(value1_, mapping_values[sub_path1_]);
//    EXPECT_EQ(value2_, mapping_values[sub_path2_]);
//    EXPECT_EQ(value3_, mapping_values[sub_path3_]);
//
//    cm::config_monitor<zk::cppzk>::instance().del_path(main_path);
//};
//
//TEST_P(cppzk_test, watch_sub_path_not_exist_main_path) {
//    std::string main_path = "/test_sub_path";
//    auto [ec, values] = cm::config_monitor<zk::cppzk>::instance().watch_sub_path(main_path);
//    EXPECT_FALSE(ec.value() == 0);
//    EXPECT_EQ(values.size(), 0);
//
//    auto [ec1, mapping_values] = cm::config_monitor<zk::cppzk>::instance().watch_sub_path(main_path);
//    EXPECT_FALSE(ec1.value() == 0);
//    EXPECT_EQ(mapping_values.size(), 0);
//};
//
//TEST_P(cppzk_test, del_path) {
//    std::string main_path = "/delete_test";
//    std::string sub_path1_ = "1";
//    std::string sub_path2_ = "2";
//    std::string sub_path3_ = "3";
//
//    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path1_ + "/" + "11");
//    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path1_ + "/" + "12");
//    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path1_ + "/" + "13");
//    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path2_ + "/" + "21");
//    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path2_ + "/" + "22");
//    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path2_ + "/" + "23");
//    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path3_ + "/" + "31");
//    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path3_ + "/" + "32");
//    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path3_ + "/" + "33");
//
//    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path1_ + "/" + "11/1");
//    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path2_ + "/" + "21/2");
//    cm::config_monitor<zk::cppzk>::instance().create_path(main_path + "/" + sub_path3_ + "/" + "31/3");
//
//    auto ec = cm::config_monitor<zk::cppzk>::instance().del_path(main_path);
//    EXPECT_EQ(!ec, true);
//};
//
//TEST_P(cppzk_test, del_not_exist_path) {
//    std::string delete_test_path = "/delete_not_exist_test";
//    auto ec = cm::config_monitor<zk::cppzk>::instance().del_path(delete_test_path);
//    EXPECT_EQ(!ec, false);
//};
//
//
//TEST_P(cppzk_test, async_create_path) {
//    std::promise<std::pair<std::error_code, std::string>> pro;
//    cm::config_monitor<zk::cppzk>::instance().async_create_path(
//        async_test_path, [&pro](const std::error_code& ec, std::string&& path) {
//        pro.set_value({ ec, std::move(path) });
//    });
//    auto [ec, new_path] = pro.get_future().get();
//    EXPECT_EQ(ec.value(), 0);
//    EXPECT_EQ(new_path, async_test_path);
//};
//
//TEST_P(cppzk_test, async_create_exist_path) {
//    cm::config_monitor<zk::cppzk>::instance().create_path(async_test_path);
//
//    std::promise<std::pair<std::error_code, std::string>> pro;
//    cm::config_monitor<zk::cppzk>::instance().async_create_path(
//        async_test_path, [&pro](const std::error_code& ec, std::string&& path) {
//        pro.set_value({ ec, std::move(path) });
//    });
//    auto [ec, new_path] = pro.get_future().get();
//    EXPECT_FALSE(ec.value() == 0);
//    EXPECT_EQ(new_path, "");
//};
//
//TEST_P(cppzk_test, async_set_path_value) {
//    std::string async_test_path_value = "5201314";
//    cm::config_monitor<zk::cppzk>::instance().create_path(async_test_path);
//
//    std::promise<std::error_code> pro;
//    cm::config_monitor<zk::cppzk>::instance().async_set_path_value(
//        async_test_path, async_test_path_value, [&pro](const std::error_code& ec) {
//        pro.set_value(ec);
//    });
//    EXPECT_EQ(pro.get_future().get().value(), 0);
//};
//
//TEST_P(cppzk_test, async_set_not_exist_path_value) {
//    std::string async_test_path_value = "5201314";
//    std::promise<std::error_code> pro;
//    cm::config_monitor<zk::cppzk>::instance().async_set_path_value(
//        async_test_path, async_test_path_value, [&pro](const std::error_code& ec) {
//        pro.set_value(ec);
//    });
//    EXPECT_FALSE(pro.get_future().get().value() == 0);
//};
//
//TEST_P(cppzk_test, async_watch_path) {
//    std::string async_test_path_value = "5201314";
//    cm::config_monitor<zk::cppzk>::instance().create_path(async_test_path, async_test_path_value);
//
//    using delay_type = std::promise<std::pair<cm::path_event, std::string>>;
//    auto pro = std::make_shared<delay_type>();
//    cm::config_monitor<zk::cppzk>::instance().async_watch_path(
//        async_test_path, [&pro](cm::path_event eve, std::string&& value) {
//        pro->set_value({ eve , std::move(value) });
//    });
//    auto [eve0, value0] = pro->get_future().get();
//    EXPECT_EQ(eve0, cm::path_event::changed);
//    EXPECT_EQ(value0, async_test_path_value);
//};
//
//TEST_P(cppzk_test, async_watch_path_not_exist_then_create) {
//    std::string async_test_path_value = "5201314";
//    std::thread([this, async_test_path_value]() {
//        std::this_thread::sleep_for(std::chrono::milliseconds(15));
//        cm::config_monitor<zk::cppzk>::instance().create_path(
//            async_test_path, async_test_path_value);
//    }).detach();
//
//    using delay_type = std::promise<std::pair<cm::path_event, std::string>>;
//    auto pro = std::make_shared<delay_type>();
//    cm::config_monitor<zk::cppzk>::instance().async_watch_path(
//        async_test_path, [&pro](cm::path_event eve, std::string&& value) {
//        pro->set_value({ eve , std::move(value) });
//    });
//    auto [eve0, value0] = pro->get_future().get();
//    EXPECT_EQ(eve0, cm::path_event::changed);
//    EXPECT_EQ(value0, async_test_path_value);
//};
//
//TEST_P(cppzk_test, async_watch_path_change_value) {
//    std::string async_test_path_value = "5201314";
//    cm::config_monitor<zk::cppzk>::instance().create_path(async_test_path, async_test_path_value);
//
//    using delay_type = std::promise<std::pair<cm::path_event, std::string>>;
//    std::shared_ptr<delay_type> pro;
//    cm::config_monitor<zk::cppzk>::instance().async_watch_path(
//        async_test_path, [&pro](cm::path_event eve, std::string&& value) {
//        if (pro) {
//            pro->set_value({ eve , std::move(value) });
//        }
//    });
//
//    std::this_thread::sleep_for(std::chrono::milliseconds(15));
//    pro = std::make_shared<delay_type>();
//    std::string new_value = "this is changed test";
//    cm::config_monitor<zk::cppzk>::instance().set_path_value(async_test_path, new_value);
//    auto [eve1, value1] = pro->get_future().get();
//    EXPECT_EQ(eve1, cm::path_event::changed);
//    EXPECT_EQ(value1, new_value);
//};
//
//TEST_P(cppzk_test, async_watch_path_delete_path) {
//    std::string async_test_path_value = "5201314";
//    cm::config_monitor<zk::cppzk>::instance().create_path(async_test_path, async_test_path_value);
//
//    using delay_type = std::promise<std::pair<cm::path_event, std::string>>;
//    std::shared_ptr<delay_type> pro;
//    cm::config_monitor<zk::cppzk>::instance().async_watch_path(
//        async_test_path, [&pro](cm::path_event eve, std::string&& value) {
//        if (pro) {
//            pro->set_value({ eve , std::move(value) });
//        }
//    });
//
//    std::this_thread::sleep_for(std::chrono::milliseconds(15));
//    pro = std::make_shared<delay_type>();
//    cm::config_monitor<zk::cppzk>::instance().del_path(async_test_path);
//    auto [eve2, value2] = pro->get_future().get();
//    EXPECT_EQ(eve2, cm::path_event::del);
//    EXPECT_EQ(value2, "");
//};
//
//TEST_P(cppzk_test, async_watch_sub_path_no_mapping) {
//    std::string sub_path1 = "1";
//    std::string value1 = "111";
//    std::string sub_path2 = "2";
//    std::string value2 = "222";
//    std::string sub_path3 = "3";
//    std::string value3 = "333";
//    cm::config_monitor<zk::cppzk>::instance().create_path(async_main_path + "/" + sub_path1, value1);
//    cm::config_monitor<zk::cppzk>::instance().create_path(async_main_path + "/" + sub_path2, value2);
//    cm::config_monitor<zk::cppzk>::instance().create_path(async_main_path + "/" + sub_path3, value3);
//
//    auto pro = std::make_shared<std::promise<cm::path_event>>();
//    std::set<std::string> values;
//    cm::config_monitor<zk::cppzk>::instance().async_watch_sub_path<false>(
//        async_main_path, [&pro, &values](cm::path_event eve, std::string&& v) {
//        if (pro) {
//            values.emplace(std::move(v));
//            if (values.size() == 3) {
//                pro->set_value(eve);
//            }
//        }
//    });
//
//    EXPECT_EQ(pro->get_future().get(), cm::path_event::changed);
//    EXPECT_EQ(values.find(value1) != values.end(), true);
//    EXPECT_EQ(values.find(value2) != values.end(), true);
//    EXPECT_EQ(values.find(value3) != values.end(), true);
//};
//
//TEST_P(cppzk_test, async_watch_sub_path_mapping) {
//    std::string path1 = async_main_path + "/" + "11";
//    std::string value1 = "xxx";
//    std::string path2 = async_main_path + "/" + "12";
//    std::string value2 = "yyy";
//    std::string path3 = async_main_path + "/" + "13";
//    std::string value3 = "zzz";
//    cm::config_monitor<zk::cppzk>::instance().create_path(path1, value1);
//    cm::config_monitor<zk::cppzk>::instance().create_path(path2, value2);
//    cm::config_monitor<zk::cppzk>::instance().create_path(path3, value3);
//
//    std::promise<cm::path_event> pro1;
//    std::unordered_map<std::string, std::string> mapping_values;
//    cm::config_monitor<zk::cppzk>::instance().async_watch_sub_path(
//        async_main_path,
//        [&pro1, &mapping_values](cm::path_event eve, std::string&& v, const std::string& path) {
//        mapping_values.emplace(path, std::move(v));
//        if (mapping_values.size() == 3) {
//            pro1.set_value(eve);
//        }
//    });
//    EXPECT_EQ(pro1.get_future().get(), cm::path_event::changed);
//    EXPECT_EQ(value1, mapping_values[path1]);
//    EXPECT_EQ(value2, mapping_values[path2]);
//    EXPECT_EQ(value3, mapping_values[path3]);
//};
//
//TEST_P(cppzk_test, async_watch_sub_path_not_exist_then_create_no_mapping) {
//    std::string path1 = async_main_path + "/" + "44";
//    std::string value1 = "xxx";
//    std::string path2 = async_main_path + "/" + "55";
//    std::string value2 = "yyy";
//    std::string path3 = async_main_path + "/" + "66";
//    std::string value3 = "zzz";
//
//    std::thread([&]() {
//        std::this_thread::sleep_for(std::chrono::milliseconds(15));
//        cm::config_monitor<zk::cppzk>::instance().create_path(path1, value1);
//        cm::config_monitor<zk::cppzk>::instance().create_path(path2, value2);
//        cm::config_monitor<zk::cppzk>::instance().create_path(path3, value3);
//    }).detach();
//
//    auto pro = std::make_shared<std::promise<cm::path_event>>();
//    std::set<std::string> values;
//    cm::config_monitor<zk::cppzk>::instance().async_watch_sub_path<false>(
//        async_main_path,
//        [&pro, &values](cm::path_event eve, std::string&& value) {
//        values.emplace(std::move(value));
//        if (values.size() == 3) {
//            pro->set_value(eve);
//        }
//    });
//    EXPECT_EQ(pro->get_future().get(), cm::path_event::changed);
//    EXPECT_EQ(values.find(value1) != values.end(), true);
//    EXPECT_EQ(values.find(value2) != values.end(), true);
//    EXPECT_EQ(values.find(value3) != values.end(), true);
//};
//
//TEST_P(cppzk_test, async_watch_sub_path_not_exist_then_create_mapping) {
//    std::string path1 = async_main_path + "/" + "77";
//    std::string value1 = "777";
//    std::string path2 = async_main_path + "/" + "88";
//    std::string value2 = "888";
//    std::string path3 = async_main_path + "/" + "99";
//    std::string value3 = "999";
//
//    std::thread([&]() {
//        std::this_thread::sleep_for(std::chrono::milliseconds(15));
//        cm::config_monitor<zk::cppzk>::instance().create_path(path1, value1);
//        cm::config_monitor<zk::cppzk>::instance().create_path(path2, value2);
//        cm::config_monitor<zk::cppzk>::instance().create_path(path3, value3);
//    }).detach();
//
//    std::promise<cm::path_event> pro1;
//    std::unordered_map<std::string, std::string> mapping_values;
//    cm::config_monitor<zk::cppzk>::instance().async_watch_sub_path(
//        async_main_path,
//        [&pro1, &mapping_values](cm::path_event eve, std::string&& v, const std::string& path) {
//        mapping_values.emplace(path, std::move(v));
//        if (mapping_values.size() == 3) {
//            pro1.set_value(eve);
//        }
//    });
//    EXPECT_EQ(pro1.get_future().get(), cm::path_event::changed);
//    EXPECT_EQ(value1, mapping_values[path1]);
//    EXPECT_EQ(value2, mapping_values[path2]);
//    EXPECT_EQ(value3, mapping_values[path3]);
//};
//
//TEST_P(cppzk_test, async_watch_sub_path_change_value_no_mapping) {
//    std::string path1 = async_main_path + "/" + "777";
//    std::string value1 = "777";
//    std::string path2 = async_main_path + "/" + "888";
//    std::string value2 = "888";
//    std::string path3 = async_main_path + "/" + "999";
//    std::string value3 = "999";
//
//    cm::config_monitor<zk::cppzk>::instance().create_path(path1, value1);
//    cm::config_monitor<zk::cppzk>::instance().create_path(path2, value2);
//    cm::config_monitor<zk::cppzk>::instance().create_path(path3, value3);
//
//    using delay_type = std::promise<cm::path_event>;
//    auto pro = std::make_shared<delay_type>();
//    std::set<std::string> values;
//    cm::config_monitor<zk::cppzk>::instance().async_watch_sub_path<false>(
//        async_main_path, [&pro, &values](cm::path_event eve, std::string&& value) {
//        values.emplace(std::move(value));
//        if (values.size() == 3) {
//            pro->set_value(eve);
//        }
//    });
//    pro->get_future().get();
//    values.clear();
//
//    pro = std::make_shared<delay_type>();
//    std::string new_value1 = "this is changed test, 777";
//    std::string new_value2 = "this is changed test, 888";
//    std::string new_value3 = "this is changed test, 999";
//    cm::config_monitor<zk::cppzk>::instance().set_path_value(path1, new_value1);
//    cm::config_monitor<zk::cppzk>::instance().set_path_value(path2, new_value2);
//    cm::config_monitor<zk::cppzk>::instance().set_path_value(path3, new_value3);
//    EXPECT_EQ(pro->get_future().get(), cm::path_event::changed);
//    EXPECT_EQ(values.find(new_value1) != values.end(), true);
//    EXPECT_EQ(values.find(new_value2) != values.end(), true);
//    EXPECT_EQ(values.find(new_value3) != values.end(), true);
//};
//
//TEST_P(cppzk_test, async_watch_sub_path_change_value_mapping) {
//    std::string path1 = async_main_path + "/" + "7777";
//    std::string value1 = "777";
//    std::string path2 = async_main_path + "/" + "8888";
//    std::string value2 = "888";
//    std::string path3 = async_main_path + "/" + "9999";
//    std::string value3 = "999";
//
//    cm::config_monitor<zk::cppzk>::instance().create_path(path1, value1);
//    cm::config_monitor<zk::cppzk>::instance().create_path(path2, value2);
//    cm::config_monitor<zk::cppzk>::instance().create_path(path3, value3);
//
//    using delay_type = std::promise<cm::path_event>;
//    auto pro = std::make_shared<delay_type>();
//    std::unordered_map<std::string, std::string> mapping_values;
//    cm::config_monitor<zk::cppzk>::instance().async_watch_sub_path(
//        async_main_path,
//        [&pro, &mapping_values](cm::path_event eve, std::string&& value, const std::string& path) {
//        mapping_values.emplace(path, std::move(value));
//        if (mapping_values.size() == 3) {
//            pro->set_value(eve);
//        }
//    });
//    pro->get_future().get();
//    mapping_values.clear();
//
//    pro = std::make_shared<delay_type>();
//    std::string new_value1 = "this is changed test, 7777";
//    std::string new_value2 = "this is changed test, 8888";
//    std::string new_value3 = "this is changed test, 9999";
//    cm::config_monitor<zk::cppzk>::instance().set_path_value(path1, new_value1);
//    cm::config_monitor<zk::cppzk>::instance().set_path_value(path2, new_value2);
//    cm::config_monitor<zk::cppzk>::instance().set_path_value(path3, new_value3);
//    EXPECT_EQ(pro->get_future().get(), cm::path_event::changed);
//    EXPECT_EQ(new_value1, mapping_values[path1]);
//    EXPECT_EQ(new_value2, mapping_values[path2]);
//    EXPECT_EQ(new_value3, mapping_values[path3]);
//};
//
//TEST_P(cppzk_test, async_watch_sub_path_delete_path_no_mapping) {
//    std::string path1 = async_main_path + "/" + "xxx";
//    std::string value1 = "7dfg77";
//    cm::config_monitor<zk::cppzk>::instance().create_path(path1, value1);
//
//    using delay_type = std::promise<std::pair<cm::path_event, std::string>>;
//    std::shared_ptr<delay_type> pro;
//    cm::config_monitor<zk::cppzk>::instance().async_watch_sub_path<false>(
//      async_main_path, [&pro](cm::path_event eve, std::string&& value) {
//        if (pro) {
//            pro->set_value({ eve , std::move(value) });
//        }
//    });
//
//    std::this_thread::sleep_for(std::chrono::milliseconds(15));
//    pro = std::make_shared<delay_type>();
//    cm::config_monitor<zk::cppzk>::instance().del_path(path1);
//    auto [eve2, value2] = pro->get_future().get();
//    EXPECT_EQ(eve2, cm::path_event::del);
//    EXPECT_EQ(value2, value1);
//};
//
//TEST_P(cppzk_test, async_watch_sub_path_delete_path_mapping) {
//    std::string path1 = async_main_path + "/" + "xxx";
//    std::string value1 = "7dfg77";
//    cm::config_monitor<zk::cppzk>::instance().create_path(path1, value1);
//
//    using delay_type = std::promise<std::tuple<cm::path_event, std::string, std::string>>;
//    std::shared_ptr<delay_type> pro;
//    cm::config_monitor<zk::cppzk>::instance().async_watch_sub_path(
//        async_main_path, [&pro](cm::path_event eve, std::string&& value, const std::string& path) {
//        if (pro) {
//            pro->set_value({ eve , std::move(value),path });
//        }
//    });
//
//    std::this_thread::sleep_for(std::chrono::milliseconds(15));
//    pro = std::make_shared<delay_type>();
//    cm::config_monitor<zk::cppzk>::instance().del_path(async_main_path);
//    auto [eve2, value2, path] = pro->get_future().get();
//    EXPECT_EQ(eve2, cm::path_event::del);
//    EXPECT_EQ(value2, "");
//    EXPECT_EQ(path, path1);
//};
//
//TEST_P(cppzk_test, async_del_path) {
//    std::string delete_test_path = "/async_test_sub_path";
//    cm::config_monitor<zk::cppzk>::instance().create_path(delete_test_path + "/test/123/123/1");
//    std::promise<std::error_code> pro1;
//    cm::config_monitor<zk::cppzk>::instance().async_del_path(
//        delete_test_path, [&pro1](const std::error_code& ec) {
//        pro1.set_value(ec);
//    });
//    EXPECT_EQ(pro1.get_future().get().value(), 0);
//};
//
//INSTANTIATE_TEST_SUITE_P(cppzk_test_set, cppzk_test,
//                         ::testing::Values(zk_info{ "192.168.152.137:2181", 40000 }));
