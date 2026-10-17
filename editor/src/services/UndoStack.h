#pragma once
// ============================================================
//  editor/src/services/UndoStack.h
//
//  編集の履歴 (Undo / Redo、M4)。GPU も webview も使わないので単体テストできる。
//
//  1 つの編集は「何を (kind / target)」「変更前 (before)」「変更後 (after)」の JSON で表す。
//  元に戻すときは before を、やり直すときは after を、編集と同じ経路で適用する
//  (グラフはグラフ全体の JSON、パイプラインは記述子の全フィールド)。
//
//  使い方 (適用は描画スレッドで行い、成否を見てから履歴を動かす):
//    push(entry)                     新しい編集。やり直しの履歴は消える
//    peekUndo() → 適用 → didUndo()   成功したら、その編集をやり直しの側へ移す
//    peekRedo() → 適用 → didRedo()
//
//  ファイルの外部での変更 (ホットリロード) は履歴に入れない。元に戻すと、その変更より前の内容に戻る。
// ============================================================

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace studio
{
using Json = nlohmann::json;

class UndoStack
{
public:
	struct Entry
	{
		std::string label;    // 表示名 ("Grid を無効にしました")
		std::string kind;     // "graph" / "pipeline"
		std::string target;   // パイプラインのアセットのパスなど (graph は空)
		Json        before;
		Json        after;
	};

	explicit UndoStack(size_t maxEntries = 200) : m_max(maxEntries) {}

	// before と after が同じ編集は入れない (false を返す)
	bool push(Entry entry);

	const Entry* peekUndo() const { return m_undo.empty() ? nullptr : &m_undo.back(); }
	const Entry* peekRedo() const { return m_redo.empty() ? nullptr : &m_redo.back(); }
	void         didUndo();
	void         didRedo();
	void         clear();

	bool   canUndo() const   { return !m_undo.empty(); }
	bool   canRedo() const   { return !m_redo.empty(); }
	size_t undoCount() const { return m_undo.size(); }
	size_t redoCount() const { return m_redo.size(); }

	// UI に送る状態 { canUndo, canRedo, undoLabel, redoLabel, undoCount, redoCount }
	Json state() const;

private:
	size_t             m_max;
	std::vector<Entry> m_undo;   // 末尾が最新
	std::vector<Entry> m_redo;   // 末尾が次にやり直すもの
};
} // namespace studio
