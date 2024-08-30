#include <future>

#include "config_monitor.hpp"
#include "cppzk/cppzk.hpp"
#include "gtest/gtest.h"

using namespace std::chrono_literals;

struct zk_info {
	std::string ips;
	int tiemout;
	std::string schema;
	std::string credential;
};

class cppzk_test : public testing::TestWithParam<zk_info> {
protected:
	std::string prefix = "/test";
	std::string watch_prefix = "/watch_test";
	std::string watch_sub_prefix = "/watch_sub_test";
	inline static bool is_init_ = false;

public:
	static void SetUpTestSuite() {
		//GetParam();
	}

	static void TearDownTestSuite() {

	}

	void SetUp() override {
		if (!is_init_) {
			is_init_ = true;
			cm::config_monitor<zk::cppzk>::instance().init(
				GetParam().ips, GetParam().tiemout, GetParam().schema, GetParam().credential);
		}
	}

	void TearDown() override {
		cm::config_monitor<>::instance().remove_watches(watch_prefix, cm::watch_type::watch_path);
		cm::config_monitor<>::instance().remove_watches(watch_sub_prefix, cm::watch_type::watch_sub_path);
		//cm::config_monitor<>::instance().remove_watches(watch_sub_prefix, cm::watch_type::watch_path);

		cm::config_monitor<>::instance().del_path(prefix);
		cm::config_monitor<>::instance().del_path(watch_prefix);
		cm::config_monitor<>::instance().del_path(watch_sub_prefix);
	}
};

//test create
TEST_P(cppzk_test, create_path) {
	std::string path = prefix;
	auto [ec, new_path] = cm::config_monitor<>::instance().create_path(path);
	EXPECT_EQ(ec.value(), 0);
	EXPECT_EQ(new_path, path);

	auto [gec, val] = cm::config_monitor<>::instance().get_path_value(new_path);
	EXPECT_EQ(gec.value(), 0);
	EXPECT_TRUE(val.has_value() == false);
};

TEST_P(cppzk_test, create_persistent_sequential_path_with_value) {
	std::string path = prefix + "/1";
	std::string value = "123456";
	auto [ec, new_path] = cm::config_monitor<>::instance().create_path(
		path, value, cm::create_mode::persistent_sequential);
	EXPECT_EQ(ec.value(), 0);
	EXPECT_TRUE(new_path != path);

	auto [gec, val] = cm::config_monitor<>::instance().get_path_value(new_path);
	EXPECT_EQ(gec.value(), 0);
	EXPECT_EQ(val, value);
};

TEST_P(cppzk_test, create_exist_path) {
	std::string path = prefix + "/1/2";
	cm::config_monitor<>::instance().create_path(path);
	auto [ec, new_path] = cm::config_monitor<>::instance().create_path(path);
	EXPECT_FALSE(ec.value() == 0);
	EXPECT_EQ(new_path, "");
};

TEST_P(cppzk_test, async_create_path) {
	std::string path = prefix;
	std::promise<std::pair<std::error_code, std::string>> pro;
	cm::config_monitor<>::instance().async_create_path(
		path, [&pro](const std::error_code& ec, std::string&& path) {
		pro.set_value({ ec, std::move(path) });
	});
	auto [ec, new_path] = pro.get_future().get();
	EXPECT_EQ(ec.value(), 0);
	EXPECT_EQ(new_path, path);

	auto [gec, val] = cm::config_monitor<>::instance().get_path_value(new_path);
	EXPECT_EQ(gec.value(), 0);
	EXPECT_TRUE(val.has_value() == false);
};

TEST_P(cppzk_test, async_create_persistent_sequential_path_with_value) {
	std::string path = prefix + "/1";
	std::string value = "123456";
	std::promise<std::pair<std::error_code, std::string>> pro;
	cm::config_monitor<>::instance().async_create_path(path,
		[&pro](const std::error_code& ec, std::string&& path) {
		pro.set_value({ ec, std::move(path) });
	}, value, cm::create_mode::persistent_sequential);
	auto [ec, new_path] = pro.get_future().get();
	EXPECT_EQ(ec.value(), 0);
	EXPECT_TRUE(new_path != path);

	auto [gec, val] = cm::config_monitor<>::instance().get_path_value(new_path);
	EXPECT_EQ(gec.value(), 0);
	EXPECT_EQ(val, value);
};

TEST_P(cppzk_test, async_create_exist_path) {
	std::string path = prefix + "/1/2";
	cm::config_monitor<>::instance().create_path(path);

	std::promise<std::pair<std::error_code, std::string>> pro;
	cm::config_monitor<>::instance().async_create_path(
		path, [&pro](const std::error_code& ec, std::string&& path) {
		pro.set_value({ ec, std::move(path) });
	});
	auto [ec, new_path] = pro.get_future().get();
	EXPECT_FALSE(ec.value() == 0);
	EXPECT_EQ(new_path, "");
};

//test delete
TEST_P(cppzk_test, del_path) {
	cm::config_monitor<>::instance().create_path(prefix);
	cm::config_monitor<>::instance().create_path(prefix + "/1/2/3/4/5/6/7/8/9");
	cm::config_monitor<>::instance().create_path(prefix + "/9/8/7/6/5/4/3/2/1");
	auto ec = cm::config_monitor<>::instance().del_path(prefix);
	EXPECT_TRUE(ec.value() == 0);
};

TEST_P(cppzk_test, del_not_exist_path) {
	auto ec = cm::config_monitor<>::instance().del_path(prefix);
	EXPECT_TRUE(ec.value() != 0);
};

TEST_P(cppzk_test, async_del_path) {
	cm::config_monitor<>::instance().create_path(prefix);
	cm::config_monitor<>::instance().create_path(prefix + "/1/2/3/4/5/6/7/8/9");
	cm::config_monitor<>::instance().create_path(prefix + "/9/8/7/6/5/4/3/2/1");

	std::promise<std::error_code> pro1;
	cm::config_monitor<>::instance().async_del_path(
		prefix, [&pro1](const std::error_code& ec) {
		pro1.set_value(ec);
	});
	EXPECT_TRUE(pro1.get_future().get().value() == 0);
};

TEST_P(cppzk_test, async_del_not_exist_path) {
	std::promise<std::error_code> pro1;
	cm::config_monitor<>::instance().async_del_path(
		prefix, [&pro1](const std::error_code& ec) {
		pro1.set_value(ec);
	});
	EXPECT_TRUE(pro1.get_future().get().value() != 0);
};

//test set
TEST_P(cppzk_test, set_path_value) {
	std::string path = prefix + "/1";
	std::string value = "123456";
	cm::config_monitor<>::instance().create_path(path);

	auto ec = cm::config_monitor<>::instance().set_path_value(path, value);
	EXPECT_EQ(ec.value(), 0);

	auto [gec, val] = cm::config_monitor<>::instance().get_path_value(path);
	EXPECT_EQ(gec.value(), 0);
	EXPECT_EQ(val, value);
};

TEST_P(cppzk_test, set_not_exist_path_value) {
	std::string path = prefix + "/1";
	auto ec = cm::config_monitor<>::instance().set_path_value(path, "5201314");
	EXPECT_FALSE(ec.value() == 0);
};

TEST_P(cppzk_test, async_set_path_value) {
	std::string path = prefix + "/1";
	std::string value = "5201314";
	cm::config_monitor<>::instance().create_path(path);

	std::promise<std::error_code> pro;
	cm::config_monitor<>::instance().async_set_path_value(
		path, value, [&pro](const std::error_code& ec) {
		pro.set_value(ec);
	});
	EXPECT_EQ(pro.get_future().get().value(), 0);

	auto [gec, val] = cm::config_monitor<>::instance().get_path_value(path);
	EXPECT_EQ(gec.value(), 0);
	EXPECT_EQ(val, value);
};

TEST_P(cppzk_test, async_set_not_exist_path_value) {
	std::string path = prefix + "/1";
	std::string value = "5201314";
	std::promise<std::error_code> pro;
	cm::config_monitor<>::instance().async_set_path_value(
		path, value, [&pro](const std::error_code& ec) {
		pro.set_value(ec);
	});
	EXPECT_FALSE(pro.get_future().get().value() == 0);
};

//test get
TEST_P(cppzk_test, get_path_value) {
	std::string path = prefix + "/1";
	std::string value = "5201314";
	cm::config_monitor<>::instance().create_path(path, value);

	auto [ec, val] = cm::config_monitor<>::instance().get_path_value(path);
	EXPECT_EQ(ec.value(), 0);
	EXPECT_EQ(val.value(), value);
};

TEST_P(cppzk_test, get_not_exist_path_value) {
	std::string path = prefix + "/1";
	std::string value = "5201314";

	auto [ec, val] = cm::config_monitor<>::instance().get_path_value(path);
	EXPECT_TRUE(ec.value() != 0);
	EXPECT_TRUE(val.has_value() != true);
};

TEST_P(cppzk_test, async_get_path_value) {
	std::string path = prefix + "/1";
	std::string value = "5201314";
	cm::config_monitor<>::instance().create_path(path, value);

	std::promise<void> pro;
	cm::config_monitor<>::instance().async_get_path_value(path,
		[&pro, value](const std::error_code& ec, std::optional<std::string>&& val) {
		EXPECT_EQ(ec.value(), 0);
		EXPECT_EQ(val.value(), value);
		pro.set_value();
	});
	pro.get_future().get();
};

TEST_P(cppzk_test, async_get_not_exist_path_value) {
	std::string path = prefix + "/1";
	std::string value = "5201314";

	std::promise<void> pro;
	cm::config_monitor<>::instance().async_get_path_value(path,
		[&pro, value](const std::error_code& ec, std::optional<std::string>&& val) {
		EXPECT_TRUE(ec.value() != 0);
		EXPECT_TRUE(val.has_value() != true);
		pro.set_value();
	});
	pro.get_future().get();
};

TEST_P(cppzk_test, get_sub_path_value) {
	std::string path1 = prefix + "/1";
	std::string value1 = "5201314";
	std::string path2 = prefix + "/2";
	std::string value2 = "123456";
	std::string path3 = prefix + "/3";
	std::string value3 = "654321";

	cm::config_monitor<>::instance().create_path(path1, value1);
	cm::config_monitor<>::instance().create_path(path2, value2);
	cm::config_monitor<>::instance().create_path(path3, value3);

	auto [ec, subs] = cm::config_monitor<>::instance().get_sub_path(prefix);
	EXPECT_TRUE(ec.value() == 0);
	EXPECT_TRUE(subs.size() == 3);

	auto [ec1, val1] = cm::config_monitor<>::instance().get_path_value(subs[0]);
	EXPECT_TRUE(ec1.value() == 0);
	EXPECT_TRUE(val1.value() == value1);
	
	auto [ec2, val2] = cm::config_monitor<>::instance().get_path_value(subs[1]);
	EXPECT_TRUE(ec2.value() == 0);
	EXPECT_TRUE(val2.value() == value2);

	auto [ec3, val3] = cm::config_monitor<>::instance().get_path_value(subs[2]);
	EXPECT_TRUE(ec3.value() == 0);
	EXPECT_TRUE(val3.value() == value3);
};


//test watch path
TEST_P(cppzk_test, watch_exist_path) {
	cm::config_monitor<>::instance().create_path(watch_prefix);

	using delay_type = std::promise<std::pair<cm::path_event, std::optional<std::string>>>;
	auto pro = std::make_shared<delay_type>();
	cm::config_monitor<>::instance().watch_path(
		watch_prefix, [&pro](cm::path_event eve, std::optional<std::string>&& value) {
		pro->set_value({ eve , std::move(value) });
	});
	auto [eve, val] = pro->get_future().get();
	EXPECT_EQ(eve, cm::path_event::changed);
	EXPECT_TRUE(val.has_value() == false);
};

TEST_P(cppzk_test, watch_not_exist_path_then_create) {
	std::string value = "5201314";
	std::thread([this, value]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		cm::config_monitor<>::instance().create_path(watch_prefix, value);
	}).detach();

	using delay_type = std::promise<std::pair<cm::path_event, std::optional<std::string>>>;
	auto pro = std::make_shared<delay_type>();
	cm::config_monitor<>::instance().watch_path(
		watch_prefix, [&pro](cm::path_event eve, std::optional<std::string>&& value) {
		pro->set_value({ eve, std::move(value) });
	});
	auto [eve, val] = pro->get_future().get();
	EXPECT_EQ(eve, cm::path_event::changed);
	EXPECT_EQ(val, value);
};

TEST_P(cppzk_test, watch_exist_path_change_value) {
	std::string value = "5201314";
	cm::config_monitor<>::instance().create_path(watch_prefix, value);

	using delay_type = std::promise<std::pair<cm::path_event, std::optional<std::string>>>;
	std::shared_ptr<delay_type> pro = nullptr;
	cm::config_monitor<>::instance().watch_path(
		watch_prefix, [&pro](cm::path_event eve, std::optional<std::string>&& value) {
		if (pro) {
			pro->set_value({ eve, std::move(value) });
		}
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	pro = std::make_shared<delay_type>();
	std::string new_value = "this is changed test";
	cm::config_monitor<>::instance().set_path_value(watch_prefix, new_value);
	auto [eve, val] = pro->get_future().get();
	EXPECT_EQ(eve, cm::path_event::changed);
	EXPECT_EQ(val, new_value);
};

TEST_P(cppzk_test, watch_exist_path_delete_path) {
	std::string value = "5201314";
	cm::config_monitor<>::instance().create_path(watch_prefix, value);

	using delay_type = std::promise<std::pair<cm::path_event, std::optional<std::string>>>;
	std::shared_ptr<delay_type> pro;
	cm::config_monitor<>::instance().watch_path(
		watch_prefix, [&pro](cm::path_event eve, std::optional<std::string>&& value) {
		if (pro) {
			pro->set_value({ eve, std::move(value) });
		}
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	pro = std::make_shared<delay_type>();
	cm::config_monitor<>::instance().del_path(watch_prefix);
	auto [eve, val] = pro->get_future().get();
	EXPECT_EQ(eve, cm::path_event::del);
	EXPECT_TRUE(val.has_value() == false);
};

//test watch sub path
TEST_P(cppzk_test, watch_exist_sub_path) {
	std::string path1 = watch_sub_prefix + "/1";
	std::string path2 = watch_sub_prefix + "/2";
	std::string path3 = watch_sub_prefix + "/3";

	cm::config_monitor<>::instance().create_path(path1);
	cm::config_monitor<>::instance().create_path(path2);
	cm::config_monitor<>::instance().create_path(path3);

	std::promise<void> pro;
	cm::config_monitor<>::instance().watch_sub_path(watch_sub_prefix,
		[&](cm::path_event eve, std::string_view path, std::optional<std::string>&& val) {
		static int count = 0;
		count++;
		EXPECT_LE(count, 3);

		EXPECT_EQ(eve, cm::path_event::changed);
		if (count == 1) {
			EXPECT_EQ(path, path1);
			EXPECT_TRUE(val.has_value() == false);
		}
		if (count == 2) {
			EXPECT_EQ(path, path2);
			EXPECT_TRUE(val.has_value() == false);
		}
		if (count == 3) {
			EXPECT_EQ(path, path3);
			EXPECT_TRUE(val.has_value() == false);
			pro.set_value();
		}
	});
	pro.get_future().get();
};

TEST_P(cppzk_test, watch_not_exist_sub_path_then_create) {
	std::string path1 = watch_sub_prefix + "/1";
	std::string value1 = "111";
	std::string path2 = watch_sub_prefix + "/2";
	std::string value2 = "222";
	std::string path3 = watch_sub_prefix + "/3";
	std::string value3 = "333";

	std::promise<void> pro;
	cm::config_monitor<>::instance().watch_sub_path(watch_sub_prefix,
		[&](cm::path_event eve, std::string_view path, std::optional<std::string>&& val) {
		static int count = 0;
		count++;
		EXPECT_LE(count, 3);

		EXPECT_EQ(eve, cm::path_event::changed);
		if (count == 1) {
			EXPECT_EQ(path, path1);
			EXPECT_EQ(val, value1);
		}
		if (count == 2) {
			EXPECT_EQ(path, path2);
			EXPECT_EQ(val, value2);
		}
		if (count == 3) {
			EXPECT_EQ(path, path3);
			EXPECT_EQ(val, value3);
			pro.set_value();
		}
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	cm::config_monitor<>::instance().create_path(path1, value1);
	cm::config_monitor<>::instance().create_path(path2, value2);
	cm::config_monitor<>::instance().create_path(path3, value3);
	pro.get_future().get();
};

TEST_P(cppzk_test, watch_exist_sub_path_change_value) {
	std::string path1 = watch_sub_prefix + "/1";
	std::string path2 = watch_sub_prefix + "/2";
	std::string path3 = watch_sub_prefix + "/3";
	std::string value1 = "this is changed test, 777";
	std::string value2 = "this is changed test, 888";
	std::string value3 = "this is changed test, 999";

	cm::config_monitor<>::instance().create_path(path1);
	cm::config_monitor<>::instance().create_path(path2);
	cm::config_monitor<>::instance().create_path(path3);

	std::promise<void> pro;
	cm::config_monitor<>::instance().watch_sub_path(watch_sub_prefix,
		[&](cm::path_event eve, std::string_view path, std::optional<std::string>&& val) {
		static int count = 0;
		count++;
		EXPECT_LE(count, 6);

		EXPECT_EQ(eve, cm::path_event::changed);
		if (count == 1) {
			EXPECT_EQ(path, path1);
			EXPECT_TRUE(val.has_value() == false);
		}
		if (count == 2) {
			EXPECT_EQ(path, path2);
			EXPECT_TRUE(val.has_value() == false);
		}
		if (count == 3) {
			EXPECT_EQ(path, path3);
			EXPECT_TRUE(val.has_value() == false);
		}
		if (count == 4) {
			EXPECT_EQ(path, path1);
			EXPECT_EQ(val, value1);
		}
		if (count == 5) {
			EXPECT_EQ(path, path2);
			EXPECT_EQ(val, value2);
		}
		if (count == 6) {
			EXPECT_EQ(path, path3);
			EXPECT_EQ(val, value3);
			pro.set_value();
		}
	});
	
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	cm::config_monitor<>::instance().set_path_value(path1, value1);
	cm::config_monitor<>::instance().set_path_value(path2, value2);
	cm::config_monitor<>::instance().set_path_value(path3, value3);
	pro.get_future().get();
};

TEST_P(cppzk_test, watch_exist_sub_path_delete_path) {
	std::string path1 = watch_sub_prefix + "/1";
	std::string path2 = watch_sub_prefix + "/2";
	std::string path3 = watch_sub_prefix + "/3";
	cm::config_monitor<>::instance().create_path(path1);
	cm::config_monitor<>::instance().create_path(path2);
	cm::config_monitor<>::instance().create_path(path3);

	std::promise<void> pro;
	cm::config_monitor<>::instance().watch_sub_path(watch_sub_prefix,
		[&](cm::path_event eve, std::string_view path, std::optional<std::string>&& val) {
		static int count = 0;
		count++;
		EXPECT_LE(count, 6);

		if (count == 1) {
			EXPECT_EQ(path, path1);
			EXPECT_TRUE(val.has_value() == false);
			EXPECT_EQ(eve, cm::path_event::changed);
		}
		if (count == 2) {
			EXPECT_EQ(path, path2);
			EXPECT_TRUE(val.has_value() == false);
			EXPECT_EQ(eve, cm::path_event::changed);
		}
		if (count == 3) {
			EXPECT_EQ(path, path3);
			EXPECT_TRUE(val.has_value() == false);
			EXPECT_EQ(eve, cm::path_event::changed);
		}
		if (count == 4) {
			EXPECT_EQ(path, path1);
			EXPECT_TRUE(val.has_value() == false);
			EXPECT_EQ(eve, cm::path_event::del);
		}
		if (count == 5) {
			EXPECT_EQ(path, path2);
			EXPECT_TRUE(val.has_value() == false);
			EXPECT_EQ(eve, cm::path_event::del);
		}
		if (count == 6) {
			EXPECT_EQ(path, path3);
			EXPECT_TRUE(val.has_value() == false);
			EXPECT_EQ(eve, cm::path_event::del);
			pro.set_value();
		}
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	cm::config_monitor<>::instance().del_path(path1);
	cm::config_monitor<>::instance().del_path(path2);
	cm::config_monitor<>::instance().del_path(path3);
	pro.get_future().get();
};

//test remove watch
TEST_P(cppzk_test, remove_watch) {
	std::string remove_prefix = "/1";
	std::string path = remove_prefix + "/1";
	std::string value = "this is changed test, 777";
	cm::config_monitor<>::instance().create_path(path, value);

	std::promise<void> pro;
	cm::config_monitor<>::instance().watch_path(remove_prefix, [&pro](auto eve, auto&& val) {
		static int count = 0;
		count++;
		if (count == 1) {
			EXPECT_TRUE(val.has_value() == false);
			EXPECT_EQ(eve, cm::path_event::changed);
		}
		if (count == 2) {
			pro.set_value();
		}
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	cm::config_monitor<>::instance().async_remove_watches(remove_prefix,
		cm::watch_type::watch_path, [this, remove_prefix, value](const std::error_code& ec) {
		EXPECT_EQ(ec.value(), 0);
		cm::config_monitor<>::instance().set_path_value(remove_prefix, "123");
	});

	std::chrono::milliseconds wait_time(100);
	auto status = pro.get_future().wait_for(wait_time);
	EXPECT_EQ(status, std::future_status::timeout);
	cm::config_monitor<>::instance().delete_path(remove_prefix);
};

TEST_P(cppzk_test, remove_sub_watch) {
	std::string remove_prefix = "/1";
	std::string path = remove_prefix + "/1";
	std::string value = "this is changed test, 777";
	cm::config_monitor<>::instance().create_path(path, value);

	std::promise<void> pro;
	cm::config_monitor<>::instance().watch_sub_path(
		remove_prefix, [&](auto eve, auto changed_path, auto&& val) {
		static int count = 0;
		count++;
		if (count == 1) {
			EXPECT_EQ(eve, cm::path_event::changed);
			EXPECT_EQ(changed_path, path);
			EXPECT_EQ(val, value);
		}
		if (count == 2) {
			pro.set_value();
		}
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	cm::config_monitor<>::instance().async_remove_watches(
		remove_prefix, cm::watch_type::watch_sub_path, [&](const std::error_code& ec) {
		auto str = ec.message();
		EXPECT_EQ(ec.value(), 0);
		cm::config_monitor<>::instance().set_path_value(path, "123");
	});

	std::chrono::milliseconds wait_time(100);
	auto status = pro.get_future().wait_for(wait_time);
	EXPECT_EQ(status, std::future_status::timeout);
	cm::config_monitor<>::instance().delete_path(remove_prefix);
};

INSTANTIATE_TEST_SUITE_P(cppzk_test_set, cppzk_test,
	::testing::Values(zk_info{ "192.168.152.137:2181", 40000, "digest", "root:111" }));
