#include "Cafe/HW/Latte/Renderer/Vulkan/RendererShaderVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cemu/FileCache/FileCache.h"
#include "config/ActiveSettings.h"
#include "config/CemuConfig.h"
#include "util/helpers/ConcurrentQueue.h"
#include "HW/Latte/Renderer/ShaderThreadPool.h"

#include "util/helpers/helpers.h"


extern std::atomic_int g_compiled_shaders_total;
extern std::atomic_int g_compiled_shaders_async;

extern bool s_isLoadingShaders;
extern FileCache* s_spirvCache;

RendererShaderVk::RendererShaderVk(ShaderType type, uint64 baseHash, uint64 auxHash, bool isGameShader, bool isGfxPackShader, std::string&& glslCode)
	: RendererShader(type, baseHash, auxHash, isGameShader, isGfxPackShader), m_glslCode(std::move(glslCode))
{
}

RendererShaderVk::~RendererShaderVk()
{
	while (!list_pipelineInfo.empty())
		delete list_pipelineInfo[0];

	VkDevice vkDev = VulkanRenderer::GetInstance()->GetLogicalDevice();
	vkDestroyShaderModule(vkDev, m_shader_module, nullptr);
}

void RendererShaderVk::Init()
{
	g_shaderThreadPool.StartThreads();
}

void RendererShaderVk::Shutdown()
{
	g_shaderThreadPool.StopThreads();
}

void RendererShaderVk::CreateVkShaderModule(std::span<uint32> spirvBuffer)
{
	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = spirvBuffer.size_bytes();
	createInfo.pCode = spirvBuffer.data();

	VulkanRenderer* vkr = (VulkanRenderer*)g_renderer.get();

	VkDevice m_device = vkr->GetLogicalDevice();

	VkResult result = vkCreateShaderModule(m_device, &createInfo, nullptr, &m_shader_module);
	if (result != VK_SUCCESS)
	{
		cemuLog_log(LogType::Force, "Vulkan: Shader error");
		throw std::runtime_error(fmt::format("Failed to create shader module: {}", result));
	}

	// set debug name
	if (vkr->IsDebugMarkersEnabled())
	{
		VkDebugUtilsObjectNameInfoEXT objName{};
		objName.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		objName.objectType = VK_OBJECT_TYPE_SHADER_MODULE;
		objName.pNext = nullptr;
		objName.objectHandle = (uint64_t)m_shader_module;
		auto objNameStr = fmt::format("shader_{:016x}_{:016x}", m_baseHash, m_auxHash);
		objName.pObjectName = objNameStr.c_str();
		vkSetDebugUtilsObjectNameEXT(vkr->GetLogicalDevice(), &objName);
	}
}

void RendererShaderVk::FinishCompilation()
{
	m_glslCode.clear();
	m_glslCode.shrink_to_fit();
}

void RendererShaderVk::CompileInternal()
{

	std::vector<uint32> spirvBuffer = GLSLToSPIRV(m_glslCode);
	if (spirvBuffer.empty())
	{
		FinishCompilation();
		return;
	}

	CreateVkShaderModule(spirvBuffer);

	// count compiled shader
	if (!s_isLoadingShaders)
	{
		if( m_isGameShader )
			++g_compiled_shaders_total;
	}

	FinishCompilation();
}

bool RendererShaderVk::IsCompiled()
{
	return m_compilationState.hasState(COMPILATION_STATE::DONE);
};

bool RendererShaderVk::WaitForCompiled()
{
	m_compilationState.waitUntilValue(COMPILATION_STATE::DONE);
	return true;
}

void RendererShaderVk::ShaderCacheLoading_begin(uint64 cacheTitleId)
{
	if (s_spirvCache)
	{
		delete s_spirvCache;
		s_spirvCache = nullptr;
	}
	uint32 spirvCacheMagic = GeneratePrecompiledCacheId();
	const std::string cacheFilename = fmt::format("{:016x}_spirv.bin", cacheTitleId);
	const fs::path cachePath = ActiveSettings::GetCachePath("shaderCache/precompiled/{}", cacheFilename);
	s_spirvCache = FileCache::Open(cachePath, true, spirvCacheMagic);
	if (s_spirvCache == nullptr)
		cemuLog_log(LogType::Force, "Unable to open SPIR-V cache {}", cacheFilename);
	s_isLoadingShaders = true;
}

void RendererShaderVk::ShaderCacheLoading_end()
{
	// keep g_spirvCache open since we will write to it while the game is running
	s_isLoadingShaders = false;
}

void RendererShaderVk::ShaderCacheLoading_Close()
{
    delete s_spirvCache;
    s_spirvCache = nullptr;
}
