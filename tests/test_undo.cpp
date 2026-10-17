// ============================================================
//  編集の履歴 (UndoStack) の単体テスト (GPU / webview 不要)
// ============================================================
#include <doctest/doctest.h>
#include "services/UndoStack.h"

using namespace studio;

namespace
{
UndoStack::Entry edit(const char* label, int before, int after)
{
	return { label, "graph", "", Json{ { "v", before } }, Json{ { "v", after } } };
}
} // namespace

TEST_CASE("履歴: 元に戻す / やり直す")
{
	UndoStack s;
	CHECK_FALSE(s.canUndo());
	CHECK_FALSE(s.canRedo());
	REQUIRE(s.push(edit("a", 0, 1)));
	REQUIRE(s.push(edit("b", 1, 2)));
	CHECK(s.state()["undoLabel"] == "b");

	// 元に戻す: before を適用してから didUndo
	REQUIRE(s.peekUndo() != nullptr);
	CHECK(s.peekUndo()->before["v"] == 1);
	s.didUndo();
	CHECK(s.state()["undoLabel"] == "a");
	CHECK(s.state()["redoLabel"] == "b");

	// やり直す: after を適用してから didRedo
	REQUIRE(s.peekRedo() != nullptr);
	CHECK(s.peekRedo()->after["v"] == 2);
	s.didRedo();
	CHECK_FALSE(s.canRedo());
	CHECK(s.undoCount() == 2);

	// 戻したあとに新しい編集をすると、やり直しの履歴は消える
	s.didUndo();
	CHECK(s.canRedo());
	REQUIRE(s.push(edit("c", 1, 5)));
	CHECK_FALSE(s.canRedo());
	CHECK(s.state()["undoLabel"] == "c");

	// 適用に失敗したときは did* を呼ばない (履歴はそのまま)
	CHECK(s.peekUndo()->label == "c");
	CHECK(s.undoCount() == 2);
}

TEST_CASE("履歴: 変化のない編集は入れない、上限を超えたら古いものから消す")
{
	UndoStack s(3);
	CHECK_FALSE(s.push(edit("same", 4, 4)));
	CHECK_FALSE(s.canUndo());
	for (int i = 0; i < 5; ++i)
		s.push(edit(std::to_string(i).c_str(), i, i + 1));
	CHECK(s.undoCount() == 3);
	s.didUndo();
	s.didUndo();
	s.didUndo();
	CHECK_FALSE(s.canUndo());
	CHECK(s.peekRedo()->label == "2");   // 0 と 1 は消えた
	s.didUndo();   // 空のときは何もしない
	s.clear();
	CHECK_FALSE(s.canRedo());
}
