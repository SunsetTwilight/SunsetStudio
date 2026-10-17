// ============================================================
//  editor/src/host/Bridge.cpp
// ============================================================
#include "Bridge.h"

#include <atomic>
#include <exception>

namespace studio
{
// 1 回の呼び出しの返答の状態。最初の ok / error だけを送る
struct Bridge::Reply::State
{
	Sender           sender;
	std::string      id;
	std::string      method;
	std::atomic_bool sent { false };

	void send(int status, const Json& value)
	{
		bool expected = false;
		if (!sent.compare_exchange_strong(expected, true))
			return;   // 2 回目以降は無視する
		sender(id, status, value.dump(-1, ' ', false, Json::error_handler_t::replace));
	}

	~State()
	{
		// 返答しないまま破棄された (ハンドラーの実装漏れ)
		if (!sent.load())
			send(1, Json{ { "code", static_cast<int>(BridgeError::Internal) },
			              { "message", "メソッド '" + method + "' が返答しませんでした" } });
	}
};

void Bridge::Reply::ok(Json result) const
{
	m_state->send(0, result);
}

void Bridge::Reply::error(BridgeError code, const std::string& message) const
{
	error(static_cast<int>(code), message);
}

void Bridge::Reply::error(int code, const std::string& message) const
{
	m_state->send(1, Json{ { "code", code }, { "message", message } });
}

bool Bridge::Reply::done() const
{
	return m_state->sent.load();
}

void Bridge::registerMethod(const std::string& name, Handler handler)
{
	std::lock_guard lock(m_mutex);
	m_handlers[name] = std::move(handler);
}

bool Bridge::hasMethod(const std::string& name) const
{
	std::lock_guard lock(m_mutex);
	return m_handlers.count(name) != 0;
}

std::vector<std::string> Bridge::methods() const
{
	std::lock_guard lock(m_mutex);
	std::vector<std::string> out;
	for (const auto& [name, h] : m_handlers)
		out.push_back(name);
	return out;
}

void Bridge::handleCall(const std::string& id, const std::string& argsJson)
{
	dispatch(id, argsJson, m_sender);
}

void Bridge::callLocal(const std::string& method, const Json& params, LocalDone done)
{
	const Json args = Json::array({ method, params });
	dispatch("local", args.dump(), [done](const std::string&, int status, const std::string& json) {
		if (done)
			done(status, Json::parse(json, nullptr, false));
	});
}

void Bridge::dispatch(const std::string& id, const std::string& argsJson, const Sender& sender)
{
	auto state    = std::make_shared<Reply::State>();
	state->sender = sender;
	state->id     = id;
	Reply reply(state);

	// ---- 引数: ["method", params?] ----
	Json args = Json::parse(argsJson, nullptr, false);
	if (args.is_discarded() || !args.is_array() || args.empty() || !args[0].is_string())
	{
		reply.error(BridgeError::InvalidRequest, "呼び出しの形式が不正です (studio.call(method, params) で呼んでください)");
		return;
	}
	state->method = args[0].get<std::string>();
	Json params = args.size() > 1 && !args[1].is_null() ? args[1] : Json::object();

	Handler handler;
	{
		std::lock_guard lock(m_mutex);
		auto it = m_handlers.find(state->method);
		if (it != m_handlers.end())
			handler = it->second;
	}
	if (!handler)
	{
		reply.error(BridgeError::MethodNotFound, "メソッド '" + state->method + "' はありません");
		return;
	}

	try
	{
		handler(params, reply);
	}
	catch (const Json::exception& e)
	{
		// params の型の間違い (params.at("x").get<int>() など)
		reply.error(BridgeError::InvalidParams, std::string("params が不正です: ") + e.what());
	}
	catch (const std::exception& e)
	{
		reply.error(BridgeError::Internal, e.what());
	}
}
} // namespace studio
