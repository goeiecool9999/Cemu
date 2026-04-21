#pragma once

#include "Cafe/HW/Latte/Renderer/RendererShader.h"
#include "util/helpers/ConcurrentQueue.h"

#include <vulkan/vulkan_core.h>
#include "util/helpers/fspinlock.h"

class RendererShaderVk : public RendererShader
{
	friend class VulkanRenderer;

public:
	static void ShaderCacheLoading_begin(uint64 cacheTitleId);
    static void ShaderCacheLoading_end();
    static void ShaderCacheLoading_Close();

	RendererShaderVk(ShaderType type, uint64 baseHash, uint64 auxHash, bool isGameShader, bool isGfxPackShader, std::string&& glslCode);
	virtual ~RendererShaderVk();

	static void Init();
	static void Shutdown();

	VkShaderModule& GetShaderModule() { return m_shader_module; }

	static inline FSpinlock s_dependencyLock;

	void TrackDependency(class PipelineInfo* p)
	{
		s_dependencyLock.lock();
		list_pipelineInfo.emplace_back(p);
		s_dependencyLock.unlock();
	}

	void RemoveDependency(class PipelineInfo* p)
	{
		s_dependencyLock.lock();
		vectorRemoveByValue(list_pipelineInfo, p);
		s_dependencyLock.unlock();
	}

	bool IsCompiled() override;
	bool WaitForCompiled() override;

private:
  virtual void CompileInternal() override;

	void FinishCompilation() override;

	VkShaderModule m_shader_module = nullptr;


	std::string m_glslCode;

	void CreateVkShaderModule(std::span<uint32> spirvBuffer);

	// pipeline infos
	std::vector<class PipelineInfo*> list_pipelineInfo;
};
