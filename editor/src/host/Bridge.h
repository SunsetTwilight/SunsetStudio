#pragma once
// ============================================================
//  editor/src/host/Bridge.h
//
//  JS → C++ の RPC。メソッド名からハンドラーを引いて呼び、結果を JSON で返す。
//  webview に依存しない (JSON の文字列を受け取って、送信関数に JSON の文字列を渡すだけ) ので、
//  単体テストできる。
//
//  JS 側: studio.call(method, params) → window.__studio_call(method, params)
//         webview はこれを (呼び出しの id, 引数の JSON 配列 '["method", {...}]') として渡す。
//  C++ 側: handleCall(id, argsJson) → ハンドラー → Reply::ok / Reply::error → Sender(id, status, json)
//         status 0 は成功 (Promise の resolve)、それ以外は失敗 (reject。値は { code, message })。
//
//  ・ハンドラーは handleCall を呼んだスレッド (UI スレッド) で呼ばれる。
//  ・Reply はコピーでき、あとで別のスレッドから返してもよい (描画スレッドの処理を待つ場合)。
//    Sender はどのスレッドから呼ばれてもよいように作ること (WebHost は UI スレッドに転送する)。
//  ・Reply を返さずにすべてのコピーが破棄されたら、内部エラーとして返す (JS の Promise が残らない)。
// ============================================================

#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace studio
{
using Json = nlohmann::json;

// エラーコード (JSON-RPC 2.0 と同じ値)
enum class BridgeError : int
{
	InvalidRequest = -32600,   // 引数の形が不正
	MethodNotFound = -32601,
	InvalidParams  = -32602,   // ハンドラーが params を受け付けない
	Internal       = -32603,   // ハンドラーの例外、返答なし
	Unavailable    = -32000,   // 今は処理できない (エンジンが初期化されていないなど)
};

class Bridge
{
public:
	// 返答を送る関数: (呼び出しの id, status (0 = 成功), 結果の JSON)
	using Sender = std::function<void(const std::string& id, int status, const std::string& resultJson)>;

	class Reply
	{
	public:
		void ok(Json result = Json::object()) const;
		void error(BridgeError code, const std::string& message) const;
		void error(int code, const std::string& message) const;
		bool done() const;

	private:
		friend class Bridge;
		struct State;
		explicit Reply(std::shared_ptr<State> s) : m_state(std::move(s)) {}
		std::shared_ptr<State> m_state;
	};

	using Handler = std::function<void(const Json& params, Reply reply)>;

	explicit Bridge(Sender sender) : m_sender(std::move(sender)) {}

	// 同じ名前を登録し直すと置き換える
	void registerMethod(const std::string& name, Handler handler);
	bool hasMethod(const std::string& name) const;
	std::vector<std::string> methods() const;

	// webview のバインドから呼ぶ。argsJson は JS の引数の配列 ('["method", params]')
	void handleCall(const std::string& id, const std::string& argsJson);

	// C++ の中から同じメソッドを呼ぶ (自動テストなど)。返答は done に届く (返答したスレッドから呼ばれる)
	using LocalDone = std::function<void(int status, const Json& result)>;
	void callLocal(const std::string& method, const Json& params, LocalDone done);

private:
	void dispatch(const std::string& id, const std::string& argsJson, const Sender& sender);

	Sender                         m_sender;
	mutable std::mutex             m_mutex;
	std::map<std::string, Handler> m_handlers;
};
} // namespace studio
