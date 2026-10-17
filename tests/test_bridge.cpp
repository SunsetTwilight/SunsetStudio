// ============================================================
//  Bridge (JS → C++ の RPC) の単体テスト。webview も GPU も使わない
// ============================================================
#include <doctest/doctest.h>
#include "host/Bridge.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using studio::Bridge;
using studio::BridgeError;
using studio::Json;

namespace
{
struct Sent
{
	std::string id;
	int         status = -1;
	Json        value;
};

struct Fixture
{
	std::vector<Sent> sent;
	Bridge bridge{ [this](const std::string& id, int status, const std::string& json) {
		sent.push_back({ id, status, Json::parse(json) });
	} };
};
} // namespace

TEST_CASE("Bridge: メソッドを呼んで結果を返す")
{
	Fixture f;
	f.bridge.registerMethod("math.add", [](const Json& p, Bridge::Reply r) {
		r.ok({ { "sum", p.at("a").get<int>() + p.at("b").get<int>() } });
	});
	CHECK(f.bridge.hasMethod("math.add"));

	f.bridge.handleCall("1", R"(["math.add", { "a": 2, "b": 3 }])");
	REQUIRE(f.sent.size() == 1);
	CHECK(f.sent[0].id == "1");
	CHECK(f.sent[0].status == 0);
	CHECK(f.sent[0].value["sum"] == 5);
}

TEST_CASE("Bridge: params を省略すると空のオブジェクト")
{
	Fixture f;
	f.bridge.registerMethod("echo", [](const Json& p, Bridge::Reply r) { r.ok(p); });
	f.bridge.handleCall("a", R"(["echo"])");
	f.bridge.handleCall("b", R"(["echo", null])");
	REQUIRE(f.sent.size() == 2);
	CHECK(f.sent[0].value == Json::object());
	CHECK(f.sent[1].value == Json::object());
}

TEST_CASE("Bridge: エラー")
{
	Fixture f;
	f.bridge.registerMethod("throws", [](const Json&, Bridge::Reply) { throw std::runtime_error("boom"); });
	f.bridge.registerMethod("needsInt", [](const Json& p, Bridge::Reply r) { r.ok({ { "v", p.at("v").get<int>() } }); });
	f.bridge.registerMethod("fails", [](const Json&, Bridge::Reply r) { r.error(BridgeError::Unavailable, "not ready"); });

	auto lastCode = [&] { return f.sent.back().value["code"].get<int>(); };

	f.bridge.handleCall("1", "not json");
	CHECK(f.sent.back().status != 0);
	CHECK(lastCode() == static_cast<int>(BridgeError::InvalidRequest));

	f.bridge.handleCall("2", R"([42])");
	CHECK(lastCode() == static_cast<int>(BridgeError::InvalidRequest));

	f.bridge.handleCall("3", R"(["nope", {}])");
	CHECK(lastCode() == static_cast<int>(BridgeError::MethodNotFound));
	CHECK(f.sent.back().value["message"].get<std::string>().find("nope") != std::string::npos);

	f.bridge.handleCall("4", R"(["throws"])");
	CHECK(lastCode() == static_cast<int>(BridgeError::Internal));
	CHECK(f.sent.back().value["message"] == "boom");

	f.bridge.handleCall("5", R"(["needsInt", { "v": "text" }])");
	CHECK(lastCode() == static_cast<int>(BridgeError::InvalidParams));

	f.bridge.handleCall("6", R"(["fails"])");
	CHECK(lastCode() == static_cast<int>(BridgeError::Unavailable));
	CHECK(f.sent.back().value["message"] == "not ready");
	CHECK(f.sent.size() == 6);
}

TEST_CASE("Bridge: あとから別のスレッドで返答できる。2 回目の返答は無視する")
{
	Fixture f;
	std::optional<Bridge::Reply> pending;
	f.bridge.registerMethod("later", [&](const Json&, Bridge::Reply r) { pending = r; });

	f.bridge.handleCall("x", R"(["later"])");
	CHECK(f.sent.empty());
	REQUIRE(pending.has_value());
	CHECK_FALSE(pending->done());

	std::thread([r = *pending] { r.ok({ { "from", "worker" } }); }).join();
	REQUIRE(f.sent.size() == 1);
	CHECK(f.sent[0].value["from"] == "worker");
	CHECK(pending->done());

	pending->ok({ { "again", true } });
	pending->error(BridgeError::Internal, "late");
	CHECK(f.sent.size() == 1);
}

TEST_CASE("Bridge: 返答しないまま破棄されたら内部エラーを返す")
{
	Fixture f;
	f.bridge.registerMethod("forgets", [](const Json&, Bridge::Reply) {});
	f.bridge.handleCall("y", R"(["forgets"])");
	REQUIRE(f.sent.size() == 1);
	CHECK(f.sent[0].status != 0);
	CHECK(f.sent[0].value["code"] == static_cast<int>(BridgeError::Internal));
}

TEST_CASE("Bridge: 登録の置き換えと一覧")
{
	Fixture f;
	f.bridge.registerMethod("m", [](const Json&, Bridge::Reply r) { r.ok(1); });
	f.bridge.registerMethod("m", [](const Json&, Bridge::Reply r) { r.ok(2); });
	f.bridge.registerMethod("a", [](const Json&, Bridge::Reply r) { r.ok(0); });
	f.bridge.handleCall("1", R"(["m"])");
	CHECK(f.sent.back().value == 2);
	CHECK(f.bridge.methods() == std::vector<std::string>{ "a", "m" });
}
