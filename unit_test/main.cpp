//#include <gtest/gtest.h>
//
//int main(int argc, char *argv[]) {
//   ::testing::InitGoogleTest(&argc, argv);
//   return RUN_ALL_TESTS();
//}

#include <future>
#include "cppzk/cppzk.hpp"
#include "config_monitor.hpp"
#include "nlohmann/json.hpp"

class example {
public:	 
	struct rule_model {
	};

private:
	std::vector<std::atomic<std::shared_ptr<rule_model>>> asset_rule_models_; //index is asset_id
	std::unordered_map<size_t, nlohmann::json> rule_id_to_json_;

	std::string whitelist_ = "/gateway/policy/whitelist";
	std::string user_define_ = "/gateway/policy/user_define";
	std::string intrusion_detect_ = "/gateway/policy/intrusion_detect";
	std::string associate_ = "/gateway/policy/associate";

public:
	static auto& instance() {
		static example ex;
		return ex;
	}

	auto& get_asset_rule_model(size_t asset_id) {
		return asset_rule_models_[asset_id];
	}

private:
	example() 
		:asset_rule_models_(1000000) {
		sync_get_all_config();
		async_watch_all_config();
	}

	rule_model make_rule_model(const std::vector<size_t>& rule_ids) {
		for (auto rule_id : rule_ids) {
			auto& one_config = rule_id_to_json_[rule_id];
		}
		return rule_model{};
	}

	static size_t parse_id(std::string_view sub_path) {
		auto pos = sub_path.find_last_of("/");
		auto data = sub_path.substr(pos + 1);
		char* end_ptr = nullptr;
		auto id = std::strtoull(data.data(), &end_ptr, 10);
		if (end_ptr == data.data()) { //failed
			return 0;
		}
		return id;
	}

	auto sync_get_config(std::string_view path, std::deque<std::string>& configs) {
		auto [_, subs] = cm::config_monitor<>::instance().get_sub_path(path);
		if (_) {
			return _;
		}
		for (auto&& sub : subs) {
			auto [ec, val] = cm::config_monitor<>::instance().get_path_value(sub);
			if (ec) {
				return ec;
			}
			configs.emplace_back(std::move(val.value()));
		}
		return std::error_code{};
	}

	void sync_get_all_config() {
		std::deque<std::string> rule_configs;
		auto ec = sync_get_config(whitelist_, rule_configs);
		ec = sync_get_config(user_define_, rule_configs);
		ec = sync_get_config(intrusion_detect_, rule_configs);
		for (const auto& config : rule_configs) {
			auto j = nlohmann::json::parse(config);
			size_t id = j["id"];
			rule_id_to_json_.emplace(id, std::move(j));
		}

		std::unordered_map<int32_t, std::vector<uint64_t>> asset_rules;
		auto [_, subs] = cm::config_monitor<>::instance().get_sub_path(associate_);
		if (_) {
			return;
		}
		for (auto&& sub : subs) {
			auto [ec1, val] = cm::config_monitor<>::instance().get_path_value(sub);
			if (ec1) {
				return;
			}
			auto j = nlohmann::json::parse(val.value());
			asset_rules.emplace(parse_id(sub), j.get<std::vector<uint64_t>>());
		}

		//construct each asset rule_model by asset_rules
		for (auto& [asset_id, rule_ids] : asset_rules) {
			auto model = make_rule_model(rule_ids);
			asset_rule_models_[asset_id].store(std::make_shared<rule_model>(std::move(model)));
		}
	}

	void async_get_config(std::string_view path) {
		cm::config_monitor<>::instance().watch_sub_path(
			path, [&](auto eve, auto changed_path, auto&& val) {
			if (!val.has_value()) {
				return;
			}
			auto id = parse_id(changed_path);
			if (eve == cm::path_event::changed) {
				auto j = nlohmann::json::parse(val.value());
				rule_id_to_json_[id] = std::move(j);
			}
			if (eve == cm::path_event::del) {
				rule_id_to_json_.erase(id);
			}
		});
	}

	void async_watch_all_config() {
		async_get_config(whitelist_);
		async_get_config(user_define_);
		async_get_config(intrusion_detect_);

		cm::config_monitor<>::instance().watch_sub_path(
			associate_, [this](auto eve, std::string_view changed_path, auto&& val) {
			auto asset_id = parse_id(changed_path);
			if (eve == cm::path_event::del) {
				asset_rule_models_[asset_id] = {};
			}
			if (eve == cm::path_event::changed) {
				auto j = nlohmann::json::parse(val.value());
				auto rule_ids = j.get<std::vector<size_t>>();
				auto model = make_rule_model(rule_ids);
				asset_rule_models_[asset_id].store(std::make_shared<rule_model>(std::move(model)));
			}
		});
	}
};

int main() {
	cm::config_monitor<>::instance().init("192.168.152.137:2181", 40000, "digest", "root:111");

	size_t index = 0;
	std::weak_ptr<example::rule_model> rule_model;
	while (1) {
		auto sp = rule_model.lock();
		if (!sp) {
			auto asset_id = index % 2 + 1;
			sp = example::instance().get_asset_rule_model(asset_id);
			rule_model = sp;
		}

		//use sp
		
	}
}