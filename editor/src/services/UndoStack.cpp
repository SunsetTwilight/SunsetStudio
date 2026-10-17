// ============================================================
//  editor/src/services/UndoStack.cpp
// ============================================================
#include "UndoStack.h"

namespace studio
{
bool UndoStack::push(Entry entry)
{
	if (entry.before == entry.after)
		return false;
	m_redo.clear();
	m_undo.push_back(std::move(entry));
	if (m_undo.size() > m_max)
		m_undo.erase(m_undo.begin(), m_undo.begin() + static_cast<std::ptrdiff_t>(m_undo.size() - m_max));
	return true;
}

void UndoStack::didUndo()
{
	if (m_undo.empty())
		return;
	m_redo.push_back(std::move(m_undo.back()));
	m_undo.pop_back();
}

void UndoStack::didRedo()
{
	if (m_redo.empty())
		return;
	m_undo.push_back(std::move(m_redo.back()));
	m_redo.pop_back();
}

void UndoStack::clear()
{
	m_undo.clear();
	m_redo.clear();
}

Json UndoStack::state() const
{
	return {
		{ "canUndo",   canUndo() },
		{ "canRedo",   canRedo() },
		{ "undoLabel", canUndo() ? m_undo.back().label : std::string() },
		{ "redoLabel", canRedo() ? m_redo.back().label : std::string() },
		{ "undoCount", m_undo.size() },
		{ "redoCount", m_redo.size() },
	};
}
} // namespace studio
