#include "ShaderThreadPool.h"

#include "RendererShader.h"
#include "util/helpers/helpers.h"

extern std::atomic_int g_compiled_shaders_async;

void ShaderThreadPool::CompilerThreadFunc()
{
	SetThreadName("vkShaderComp");
	while (m_threadsActive.load(std::memory_order::relaxed))
	{
		s_compilationQueueCount.decrementWithWait();
		s_compilationQueueMutex.lock();
		if (s_compilationQueue.empty())
		{
			// queue empty again, shaders compiled synchronously via PreponeCompilation()
			s_compilationQueueMutex.unlock();
			continue;
		}
		RendererShader* job = s_compilationQueue.front();
		s_compilationQueue.pop_front();
		// set compilation state
		cemu_assert_debug(job->m_compilationState.getValue() == RendererShader::COMPILATION_STATE::QUEUED);
		job->m_compilationState.setValue(RendererShader::COMPILATION_STATE::COMPILING);
		s_compilationQueueMutex.unlock();
		// compile
		job->CompileInternal();
		++g_compiled_shaders_async;
		// mark as compiled
		cemu_assert_debug(job->m_compilationState.getValue() == RendererShader::COMPILATION_STATE::COMPILING);
		job->m_compilationState.setValue(RendererShader::COMPILATION_STATE::DONE);
	}
}

ShaderThreadPool g_shaderThreadPool;