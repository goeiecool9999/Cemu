#pragma once
#include "util/helpers/Semaphore.h"

class RendererShader;
class ShaderThreadPool
{
public:
	void StartThreads()
	{
		if (m_threadsActive.exchange(true))
			return;
		// create thread pool
		const uint32 threadCount = 2;
		for (uint32 i = 0; i < threadCount; ++i)
			s_threads.emplace_back(&ShaderThreadPool::CompilerThreadFunc, this);
	}

	void StopThreads()
	{
		if (!m_threadsActive.exchange(false))
			return;
		for (uint32 i = 0; i < s_threads.size(); ++i)
			s_compilationQueueCount.increment();
		for (auto& it : s_threads)
			it.join();
		s_threads.clear();
	}

	~ShaderThreadPool()
	{
		StopThreads();
	}

	void CompilerThreadFunc();

	bool HasThreadsRunning() const { return m_threadsActive; }

public:
	std::vector<std::thread> s_threads;

	std::deque<RendererShader*> s_compilationQueue;
	CounterSemaphore s_compilationQueueCount;
	std::mutex s_compilationQueueMutex;

private:
	std::atomic<bool> m_threadsActive;
};
extern ShaderThreadPool g_shaderThreadPool;
