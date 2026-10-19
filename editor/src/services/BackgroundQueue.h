#pragma once
// ============================================================
//  editor/src/services/BackgroundQueue.h
//
//  時間のかかる処理 (モデルの取り込みなど) を順番に実行する 1 本のスレッド。
//  結果は、処理の中から WebHost::post などで UI スレッドへ返す。
//  stop() は実行中の処理の終わりを待ち、残りの処理は捨てる。
// ============================================================

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace studio
{
class BackgroundQueue
{
public:
	using Task = std::function<void()>;

	BackgroundQueue() = default;
	~BackgroundQueue() { stop(); }
	BackgroundQueue(const BackgroundQueue&)            = delete;
	BackgroundQueue& operator=(const BackgroundQueue&) = delete;

	void post(Task task)
	{
		{
			std::lock_guard lock(m_mutex);
			if (m_stopping)
				return;
			m_tasks.push_back(std::move(task));
			if (!m_thread.joinable())
				m_thread = std::thread([this] { run(); });
		}
		m_cv.notify_one();
	}

	void stop()
	{
		{
			std::lock_guard lock(m_mutex);
			m_stopping = true;
			m_tasks.clear();
		}
		m_cv.notify_one();
		if (m_thread.joinable())
			m_thread.join();
	}

private:
	void run()
	{
		for (;;)
		{
			Task task;
			{
				std::unique_lock lock(m_mutex);
				m_cv.wait(lock, [this] { return m_stopping || !m_tasks.empty(); });
				if (m_stopping)
					return;
				task = std::move(m_tasks.front());
				m_tasks.pop_front();
			}
			task();
		}
	}

	std::mutex              m_mutex;
	std::condition_variable m_cv;
	std::deque<Task>        m_tasks;
	std::thread             m_thread;
	bool                    m_stopping = false;
};
} // namespace studio
