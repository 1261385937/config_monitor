#include <future>

#include "config_monitor.hpp"
#include "local_file/local_file.hpp"
#include "gtest/gtest.h"

using namespace std::chrono_literals;

class local_file_test : public ::testing::Test {
public:
    std::string test_path = "./test";
    std::string async_test_path = "./async_test";
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

//****************create_path********************

TEST_F(local_file_test, create_path_with_value) {
    std::string multilayer_path = test_path + "/1/2/3/4/5";
    auto [ec, path] = cm::config_monitor<loc::loc_file>::instance().create_path(multilayer_path, "333");
    EXPECT_EQ(ec.value(), 0);
    EXPECT_EQ(path, multilayer_path);

    auto [e, path_value] = cm::config_monitor<loc::loc_file>::instance().watch_path(multilayer_path);
    EXPECT_EQ(e.value(), 0);
    EXPECT_EQ(path_value, "333");
};

TEST_F(local_file_test, create_path_without_value) {
    std::string multilayer_path = test_path + "/1/2/3/4/5";
    auto [ec, path] = cm::config_monitor<loc::loc_file>::instance().create_path(multilayer_path);
    EXPECT_EQ(ec.value(), 0);
    EXPECT_EQ(path, multilayer_path);

    auto [e, path_value] = cm::config_monitor<loc::loc_file>::instance().watch_path(multilayer_path);
    EXPECT_EQ(e.value(), 0);
    EXPECT_EQ(path_value.value(), "");
};


TEST_F(local_file_test, create_exist_path) {
    std::string multilayer_path = test_path + "/1/2/3/4/5";
    auto [ec0, path0] = cm::config_monitor<loc::loc_file>::instance().create_path(multilayer_path);
    EXPECT_EQ(ec0.value(), 0);
    EXPECT_EQ(path0, multilayer_path);
    auto [ec1, path1] = cm::config_monitor<loc::loc_file>::instance().create_path(multilayer_path);
    EXPECT_FALSE(ec1.value() == 0);
    EXPECT_EQ(path1, "");
};

//****************async_create_path********************

TEST_F(local_file_test, async_create_path_with_value) {
    std::string multilayer_path = async_test_path + "/1/2/3/4/5";
    std::string value = std::string("haha");
    std::promise<std::pair<std::error_code, std::string>> pro;
    cm::config_monitor<loc::loc_file>::instance().async_create_path(multilayer_path,
        [&pro](const std::error_code& ec, std::string&& path) {
        pro.set_value({ ec, std::move(path) });
    }, value);
    auto [ec, new_path] = pro.get_future().get();
    EXPECT_EQ(ec.value(), 0);
    EXPECT_EQ(new_path, multilayer_path);

    auto [e, path_value] = cm::config_monitor<loc::loc_file>::instance().watch_path(multilayer_path);
    EXPECT_EQ(e.value(), 0);
    EXPECT_EQ(path_value.value(), value);
};

TEST_F(local_file_test, async_create_path_without_value) {
    std::string multilayer_path = async_test_path + "/1/2/3/4/5";
    std::promise<std::pair<std::error_code, std::string>> pro;
    cm::config_monitor<loc::loc_file>::instance().async_create_path(multilayer_path,
        [&pro](const std::error_code& ec, std::string&& path) {
        pro.set_value({ ec, std::move(path) });
    });
    auto [ec, new_path] = pro.get_future().get();
    EXPECT_EQ(ec.value(), 0);
    EXPECT_EQ(new_path, multilayer_path);

    auto [e, path_value] = cm::config_monitor<loc::loc_file>::instance().watch_path(multilayer_path);
    EXPECT_EQ(e.value(), 0);
    EXPECT_EQ(path_value.value(), "");
};

TEST_F(local_file_test, async_create_exist_path) {
    std::string multilayer_path = async_test_path + "/1/2/3/4/5";
    cm::config_monitor<loc::loc_file>::instance().create_path(multilayer_path);
    std::promise<std::pair<std::error_code, std::string>> pro;
    cm::config_monitor<loc::loc_file>::instance().async_create_path(
        multilayer_path, [&pro](const std::error_code& ec, std::string&& path) {
        pro.set_value({ ec, std::move(path) });
    });
    auto [ec, new_path] = pro.get_future().get();
    EXPECT_FALSE(ec.value() == 0);
    EXPECT_EQ(new_path, "");
};

//****************set_path_value********************

TEST_F(local_file_test, set_path_value) {
    cm::config_monitor<loc::loc_file>::instance().create_path(test_path);
    auto ec = cm::config_monitor<loc::loc_file>::instance().set_path_value(test_path, "5201314");
    EXPECT_EQ(ec.value(), 0);

    auto [e, path_value] = cm::config_monitor<loc::loc_file>::instance().watch_path(test_path);
    EXPECT_EQ(e.value(), 0);
    EXPECT_EQ(path_value.value(), "5201314");
};

TEST_F(local_file_test, set_not_exist_path_value) {
    auto ec = cm::config_monitor<loc::loc_file>::instance().set_path_value(test_path, "5201314");
    EXPECT_FALSE(ec.value() == 0);
};

//****************async_set_path_value********************

TEST_F(local_file_test, async_set_path_value) {
    std::string async_test_path_value = "5201314";
    cm::config_monitor<loc::loc_file>::instance().create_path(async_test_path);
    std::promise<std::error_code> pro;
    cm::config_monitor<loc::loc_file>::instance().async_set_path_value(
        async_test_path, async_test_path_value, [&pro](const std::error_code& ec) {
        pro.set_value(ec);
    });
    EXPECT_EQ(pro.get_future().get().value(), 0);

    auto [e, path_value] = cm::config_monitor<loc::loc_file>::instance().watch_path(async_test_path);
    EXPECT_EQ(e.value(), 0);
    EXPECT_EQ(path_value.value(), async_test_path_value);
};

TEST_F(local_file_test, async_set_not_exist_path_value) {
    std::string async_test_path_value = "5201314";
    std::promise<std::error_code> pro;
    cm::config_monitor<loc::loc_file>::instance().async_set_path_value(
        async_test_path, async_test_path_value, [&pro](const std::error_code& ec) {
        pro.set_value(ec);
    });
    EXPECT_FALSE(pro.get_future().get().value() == 0);
};

//****************del_path********************

TEST_F(local_file_test, del_path) {
    std::string main_path = "/delete_test";

    cm::config_monitor<loc::loc_file>::instance().create_path(main_path + "/1/11");
    cm::config_monitor<loc::loc_file>::instance().create_path(main_path + "/2/12");
    cm::config_monitor<loc::loc_file>::instance().create_path(main_path + "/3/13");
    cm::config_monitor<loc::loc_file>::instance().create_path(main_path + "/1/21");
    cm::config_monitor<loc::loc_file>::instance().create_path(main_path + "/2/22");
    cm::config_monitor<loc::loc_file>::instance().create_path(main_path + "/3/23");
    cm::config_monitor<loc::loc_file>::instance().create_path(main_path + "/1/31");
    cm::config_monitor<loc::loc_file>::instance().create_path(main_path + "/2/32");
    cm::config_monitor<loc::loc_file>::instance().create_path(main_path + "/3/33");
    cm::config_monitor<loc::loc_file>::instance().create_path(main_path + "/1/11/1");
    cm::config_monitor<loc::loc_file>::instance().create_path(main_path + "/2/21/2");
    cm::config_monitor<loc::loc_file>::instance().create_path(main_path + "/3/31/3");

    auto ec = cm::config_monitor<loc::loc_file>::instance().del_path(main_path);
    EXPECT_EQ(!ec, true);
    auto [e, path_value] = cm::config_monitor<loc::loc_file>::instance().watch_path(main_path + "/1");
    EXPECT_FALSE(e.value() == 0);
};

TEST_F(local_file_test, del_not_exist_path) {
    std::string delete_test_path = "/delete_not_exist_test";
    auto ec = cm::config_monitor<loc::loc_file>::instance().del_path(delete_test_path);
    EXPECT_EQ(!ec, false);
};

//****************async_del_path********************

TEST_F(local_file_test, async_del_path) {
    std::string delete_test_path = "/async_test_sub_path";
    cm::config_monitor<loc::loc_file>::instance().create_path(delete_test_path + "/test/123/123/1");
    std::promise<std::error_code> pro1;
    cm::config_monitor<loc::loc_file>::instance().async_del_path(
        delete_test_path, [&pro1](const std::error_code& ec) {
        pro1.set_value(ec);
    });
    EXPECT_EQ(pro1.get_future().get().value(), 0);

    auto [e, _] = cm::config_monitor<loc::loc_file>::instance().watch_path(delete_test_path + "/test");
    EXPECT_FALSE(e.value() == 0);
};

TEST_F(local_file_test, async_del_not_exist_path) {
    std::string delete_test_path = "/delete_not_exist_test";
    std::promise<std::error_code> pro1;
    cm::config_monitor<loc::loc_file>::instance().async_del_path(
        delete_test_path, [&pro1](const std::error_code& ec) {
        pro1.set_value(ec);
    });
    EXPECT_FALSE(pro1.get_future().get().value() == 0);
};

//****************watch_path********************

TEST_F(local_file_test, watch_path) {
    std::string test_path_value = "5201314";
    cm::config_monitor<loc::loc_file>::instance().create_path(test_path);
    cm::config_monitor<loc::loc_file>::instance().set_path_value(test_path, test_path_value);

    auto [ec, path_value] = cm::config_monitor<loc::loc_file>::instance().watch_path(test_path);
    EXPECT_EQ(ec.value(), 0);
    EXPECT_EQ(path_value, test_path_value);
};

TEST_F(local_file_test, watch_not_exist_path) {
    auto [ec, path_value] = cm::config_monitor<loc::loc_file>::instance().watch_path(test_path);
    EXPECT_FALSE(ec.value() == 0);
    EXPECT_EQ(path_value.value(), "");
};

//****************async_watch_path********************

TEST_F(local_file_test, async_watch_path) {
    std::string async_test_path_value = "5201314";
    cm::config_monitor<loc::loc_file>::instance().create_path(async_test_path, async_test_path_value);

    using delay_type = std::promise<std::pair<cm::path_event, std::optional<std::string>>>;
    auto pro = std::make_shared<delay_type>();
    cm::config_monitor<loc::loc_file>::instance().async_watch_path(async_test_path,
        [&pro](cm::path_event eve, std::optional<std::string>&& value, const std::string&) {
        pro->set_value({ eve , std::move(value) });
    });
    auto [eve0, value0] = pro->get_future().get();
    EXPECT_EQ(eve0, cm::path_event::changed);
    EXPECT_EQ(value0.value(), async_test_path_value);
};

TEST_F(local_file_test, async_watch_path_not_exist_then_create) {
    std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        cm::config_monitor<loc::loc_file>::instance().create_path(async_test_path);
    }).detach();

    using delay_type = std::promise<std::pair<cm::path_event, std::optional<std::string>>>;
    auto pro = std::make_shared<delay_type>();
    cm::config_monitor<loc::loc_file>::instance().async_watch_path(async_test_path,
        [&pro](cm::path_event eve, std::optional<std::string>&& value, const std::string&) {
        pro->set_value({ eve , std::move(value) });
    });
    auto [eve0, value0] = pro->get_future().get();
    EXPECT_EQ(eve0, cm::path_event::changed);
    EXPECT_EQ(value0.value(), "");
};

TEST_F(local_file_test, async_watch_path_change_delete_create_change) {
    std::string async_test_path_value = "5201314";
    cm::config_monitor<loc::loc_file>::instance().create_path(async_test_path, async_test_path_value);

    using delay_type = std::promise<std::pair<cm::path_event, std::optional<std::string>>>;
    std::shared_ptr<delay_type> pro;
    cm::config_monitor<loc::loc_file>::instance().async_watch_path(async_test_path,
        [&pro](cm::path_event eve, std::optional<std::string>&& value, const std::string&) {
        if (pro) {
            pro->set_value({ eve , std::move(value) });
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    pro = std::make_shared<delay_type>();
    std::string new_value = "this is changed test";
    cm::config_monitor<loc::loc_file>::instance().set_path_value(async_test_path, new_value);
    auto [eve, value] = pro->get_future().get();
    EXPECT_EQ(eve, cm::path_event::changed);
    EXPECT_EQ(value.value(), new_value);


    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    pro = std::make_shared<delay_type>();
    cm::config_monitor<loc::loc_file>::instance().del_path(async_test_path);
    auto [eve1, value1] = pro->get_future().get();
    EXPECT_EQ(eve1, cm::path_event::del);
    EXPECT_EQ(value1.has_value(), false);

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    pro = std::make_shared<delay_type>();
    std::string new_value2 = "this is changed test2";
    cm::config_monitor<loc::loc_file>::instance().create_path(async_test_path, new_value2);
    auto [eve2, value2] = pro->get_future().get();
    EXPECT_EQ(eve2, cm::path_event::changed);
    EXPECT_EQ(value2.value(), new_value2);

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    pro = std::make_shared<delay_type>();
    std::string new_value3 = "this is changed test3";
    cm::config_monitor<loc::loc_file>::instance().set_path_value(async_test_path, new_value3);
    auto [eve3, value3] = pro->get_future().get();
    EXPECT_EQ(eve3, cm::path_event::changed);
    EXPECT_EQ(value3.value(), new_value3);
};

TEST_F(local_file_test, async_watch_path_delete_path) {
    std::string async_test_path_value = "5201314";
    cm::config_monitor<loc::loc_file>::instance().create_path(async_test_path, async_test_path_value);

    using delay_type = std::promise<std::pair<cm::path_event, std::optional<std::string>>>;
    std::shared_ptr<delay_type> pro;
    cm::config_monitor<loc::loc_file>::instance().async_watch_path(async_test_path,
        [&pro](cm::path_event eve, std::optional<std::string>&& value, const std::string&) {
        if (pro) {
            pro->set_value({ eve , std::move(value) });
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    pro = std::make_shared<delay_type>();
    cm::config_monitor<loc::loc_file>::instance().del_path(async_test_path);
    auto [eve2, value2] = pro->get_future().get();
    EXPECT_EQ(eve2, cm::path_event::del);
    EXPECT_EQ(value2.has_value(), false);
};

//****************watch_sub_path********************

TEST_F(local_file_test, watch_sub_path) {
    std::string main_path = "/test_sub_path";
    std::string sub_path1_ = "1";
    std::string value1_ = "111";
    std::string sub_path2_ = "2";
    std::string value2_ = "222";
    std::string sub_path3_ = "3";
    std::string value3_ = "333";

    cm::config_monitor<loc::loc_file>::instance().create_path(main_path + "/" + sub_path1_, value1_);
    cm::config_monitor<loc::loc_file>::instance().create_path(main_path + "/" + sub_path2_, value2_);
    cm::config_monitor<loc::loc_file>::instance().create_path(main_path + "/" + sub_path3_, value3_);

    auto [ec, mapping_values] = cm::config_monitor<loc::loc_file>::instance().watch_sub_path(main_path);
    EXPECT_EQ(value1_, mapping_values[main_path + "/" + sub_path1_]);
    EXPECT_EQ(value2_, mapping_values[main_path + "/" + sub_path2_]);
    EXPECT_EQ(value3_, mapping_values[main_path + "/" + sub_path3_]);

    cm::config_monitor<loc::loc_file>::instance().del_path(main_path);
};

TEST_F(local_file_test, watch_sub_path_not_exist_main_path) {
    std::string main_path = "/test_sub_path";

    auto [ec1, mapping_values] = cm::config_monitor<loc::loc_file>::instance().watch_sub_path(main_path);
    EXPECT_FALSE(ec1.value() == 0);
    EXPECT_EQ(mapping_values.size(), 0);
};

/****************async_watch_sub_path********************/

TEST_F(local_file_test, async_watch_sub_path) {
    std::string sub_path1 = "1";
    std::string value1 = "111";
    std::string sub_path2 = "2";
    std::string value2 = "222";
    std::string sub_path3 = "3";
    std::string value3 = "333";
    cm::config_monitor<loc::loc_file>::instance().create_path(async_main_path + "/" + sub_path1, value1);
    cm::config_monitor<loc::loc_file>::instance().create_path(async_main_path + "/" + sub_path2, value2);
    cm::config_monitor<loc::loc_file>::instance().create_path(async_main_path + "/" + sub_path3, value3);

    auto pro = std::make_shared<std::promise<cm::path_event>>();
    std::set<std::string> values;
    cm::config_monitor<loc::loc_file>::instance().async_watch_sub_path(async_main_path,
        [&pro, &values](cm::path_event eve, std::optional<std::string>&& v, const std::string&) {
        if (pro) {
            values.emplace(std::move(v.value()));
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

TEST_F(local_file_test, async_watch_sub_path_not_exist_then_create) {
    std::string path1 = async_main_path + "/" + "44";
    std::string value1 = "xxx";
    std::string path2 = async_main_path + "/" + "55";
    std::string value2 = "yyy";
    std::string path3 = async_main_path + "/" + "66";
    std::string value3 = "zzz";

    std::thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        cm::config_monitor<loc::loc_file>::instance().create_path(path1, value1);
        cm::config_monitor<loc::loc_file>::instance().create_path(path2, value2);
        cm::config_monitor<loc::loc_file>::instance().create_path(path3, value3);
    }).detach();

    auto pro = std::make_shared<std::promise<cm::path_event>>();
    std::set<std::string> values;
    cm::config_monitor<loc::loc_file>::instance().async_watch_sub_path(async_main_path,
        [&pro, &values](cm::path_event eve, std::optional<std::string>&& v, const std::string&) {
        values.emplace(std::move(v.value()));
        if (values.size() == 3) {
            pro->set_value(eve);
        }
    });
    EXPECT_EQ(pro->get_future().get(), cm::path_event::changed);
    EXPECT_EQ(values.find(value1) != values.end(), true);
    EXPECT_EQ(values.find(value2) != values.end(), true);
    EXPECT_EQ(values.find(value3) != values.end(), true);
};

TEST_F(local_file_test, async_watch_sub_path_change_value) {
    std::string path1 = async_main_path + "/" + "777";
    std::string value1 = "777";
    std::string path2 = async_main_path + "/" + "888";
    std::string value2 = "888";
    std::string path3 = async_main_path + "/" + "999";
    std::string value3 = "999";

    cm::config_monitor<loc::loc_file>::instance().create_path(path1, value1);
    cm::config_monitor<loc::loc_file>::instance().create_path(path2, value2);
    cm::config_monitor<loc::loc_file>::instance().create_path(path3, value3);

    using delay_type = std::promise<cm::path_event>;
    auto pro = std::make_shared<delay_type>();
    std::set<std::string> values;
    cm::config_monitor<loc::loc_file>::instance().async_watch_sub_path(async_main_path,
        [&pro, &values](cm::path_event eve, std::optional<std::string>&& v, const std::string&) {
        values.emplace(std::move(v.value()));
        if (values.size() == 3) {
            pro->set_value(eve);
        }
    });
    pro->get_future().get();
    values.clear();

    pro = std::make_shared<delay_type>();
    std::string new_value1 = "this is changed test, 777";
    std::string new_value2 = "this is changed test, 888";
    std::string new_value3 = "this is changed test, 999";
    cm::config_monitor<loc::loc_file>::instance().set_path_value(path1, new_value1);
    cm::config_monitor<loc::loc_file>::instance().set_path_value(path2, new_value2);
    cm::config_monitor<loc::loc_file>::instance().set_path_value(path3, new_value3);
    EXPECT_EQ(pro->get_future().get(), cm::path_event::changed);
    EXPECT_EQ(values.find(new_value1) != values.end(), true);
    EXPECT_EQ(values.find(new_value2) != values.end(), true);
    EXPECT_EQ(values.find(new_value3) != values.end(), true);
};

TEST_F(local_file_test, async_watch_sub_path_delete_path) {
    std::string path1 = async_main_path + "/" + "xxx";
    std::string value1 = "7dfg77";
    cm::config_monitor<loc::loc_file>::instance().create_path(path1, value1);

    using delay_type = std::promise<std::pair<cm::path_event, std::string>>;
    std::shared_ptr<delay_type> pro;
    cm::config_monitor<loc::loc_file>::instance().async_watch_sub_path(async_main_path,
        [&pro](cm::path_event eve, std::optional<std::string>&& v, const std::string&) {
        if (pro) {
            pro->set_value({ eve , v.has_value() ? std::move(v.value()) : std::string{} });
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    pro = std::make_shared<delay_type>();
    cm::config_monitor<loc::loc_file>::instance().del_path(path1);
    auto [eve2, value2] = pro->get_future().get();
    EXPECT_EQ(eve2, cm::path_event::del);
    EXPECT_EQ(value2, "");
};