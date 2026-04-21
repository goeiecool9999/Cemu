#include "Cafe/HW/Latte/Renderer/RendererShader.h"

#include "Renderer.h"
#include "ShaderThreadPool.h"
#include "Cafe/GameProfile/GameProfile.h"
#include "Cemu/FileCache/FileCache.h"
#include "Vulkan/VulkanRenderer.h"

#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

extern std::atomic_int g_compiled_shaders_async;

bool s_isLoadingShaders{ false };
class FileCache* s_spirvCache{nullptr};

RendererShader::RendererShader(ShaderType type, uint64 baseHash, uint64 auxHash, bool isGameShader, bool isGfxPackShader)
		: m_type(type), m_baseHash(baseHash), m_auxHash(auxHash), m_isGameShader(isGameShader), m_isGfxPackShader(isGfxPackShader)
{
	// start async compilation
	g_shaderThreadPool.s_compilationQueueMutex.lock();
	m_compilationState.setValue(COMPILATION_STATE::QUEUED);
	g_shaderThreadPool.s_compilationQueue.push_back(this);
	g_shaderThreadPool.s_compilationQueueCount.increment();
	g_shaderThreadPool.s_compilationQueueMutex.unlock();
	cemu_assert_debug(g_shaderThreadPool.HasThreadsRunning()); // make sure .StartThreads() was called
}


void RendererShader::PreponeCompilation()
{
	g_shaderThreadPool.s_compilationQueueMutex.lock();
	bool isStillQueued = m_compilationState.hasState(COMPILATION_STATE::QUEUED);
	if (isStillQueued)
	{
		// remove from queue
		g_shaderThreadPool.s_compilationQueue.erase(std::remove(g_shaderThreadPool.s_compilationQueue.begin(), g_shaderThreadPool.s_compilationQueue.end(), this), g_shaderThreadPool.s_compilationQueue.end());
		m_compilationState.setValue(COMPILATION_STATE::COMPILING);
	}
	g_shaderThreadPool.s_compilationQueueMutex.unlock();
	if (!isStillQueued)
	{
		m_compilationState.waitUntilValue(COMPILATION_STATE::DONE);
		--g_compiled_shaders_async; // compilation caused a stall so we don't consider this one async
		return;
	}
	else
	{
		// compile synchronously
		CompileInternal();
		m_compilationState.setValue(COMPILATION_STATE::DONE);
	}
}

// generate a Cemu version and setting dependent id
uint32 RendererShader::GeneratePrecompiledCacheId()
{
	uint32 v = 0;
	const char* s = EMULATOR_VERSION_SUFFIX;
	while (*s)
	{
		v = std::rotl<uint32>(v, 7);
		v += (uint32)(*s);
		s++;
	}
	v += (EMULATOR_VERSION_MAJOR * 1000000u);
	v += (EMULATOR_VERSION_MINOR * 10000u);
	v += (EMULATOR_VERSION_PATCH * 100u);

	// settings that can influence shaders
	v += (uint32)g_current_game_profile->GetAccurateShaderMul() * 133;

	return v;
}

void RendererShader::GenerateShaderPrecompiledCacheFilename(RendererShader::ShaderType type, uint64 baseHash, uint64 auxHash, uint64& h1, uint64& h2)
{
	h1 = baseHash;
	h2 = auxHash;

	if (type == RendererShader::ShaderType::kVertex)
		h2 += 0xA16374cull;
	else if (type == RendererShader::ShaderType::kFragment)
		h2 += 0x8752deull;
	else if (type == RendererShader::ShaderType::kGeometry)
		h2 += 0x65a035ull;
}

consteval TBuiltInResource GetDefaultBuiltInResource()
{
	TBuiltInResource defaultResource = {};
	defaultResource.maxLights = 32;
	defaultResource.maxClipPlanes = 6;
	defaultResource.maxTextureUnits = 32;
	defaultResource.maxTextureCoords = 32;
	defaultResource.maxVertexAttribs = 64;
	defaultResource.maxVertexUniformComponents = 4096;
	defaultResource.maxVaryingFloats = 64;
	defaultResource.maxVertexTextureImageUnits = 32;
	defaultResource.maxCombinedTextureImageUnits = 80;
	defaultResource.maxTextureImageUnits = 32;
	defaultResource.maxFragmentUniformComponents = 4096;
	defaultResource.maxDrawBuffers = 32;
	defaultResource.maxVertexUniformVectors = 128;
	defaultResource.maxVaryingVectors = 8;
	defaultResource.maxFragmentUniformVectors = 16;
	defaultResource.maxVertexOutputVectors = 16;
	defaultResource.maxFragmentInputVectors = 15;
	defaultResource.minProgramTexelOffset = -8;
	defaultResource.maxProgramTexelOffset = 7;
	defaultResource.maxClipDistances = 8;
	defaultResource.maxComputeWorkGroupCountX = 65535;
	defaultResource.maxComputeWorkGroupCountY = 65535;
	defaultResource.maxComputeWorkGroupCountZ = 65535;
	defaultResource.maxComputeWorkGroupSizeX = 1024;
	defaultResource.maxComputeWorkGroupSizeY = 1024;
	defaultResource.maxComputeWorkGroupSizeZ = 64;
	defaultResource.maxComputeUniformComponents = 1024;
	defaultResource.maxComputeTextureImageUnits = 16;
	defaultResource.maxComputeImageUniforms = 8;
	defaultResource.maxComputeAtomicCounters = 8;
	defaultResource.maxComputeAtomicCounterBuffers = 1;
	defaultResource.maxVaryingComponents = 60;
	defaultResource.maxVertexOutputComponents = 64;
	defaultResource.maxGeometryInputComponents = 64;
	defaultResource.maxGeometryOutputComponents = 128;
	defaultResource.maxFragmentInputComponents = 128;
	defaultResource.maxImageUnits = 8;
	defaultResource.maxCombinedImageUnitsAndFragmentOutputs = 8;
	defaultResource.maxCombinedShaderOutputResources = 8;
	defaultResource.maxImageSamples = 0;
	defaultResource.maxVertexImageUniforms = 0;
	defaultResource.maxTessControlImageUniforms = 0;
	defaultResource.maxTessEvaluationImageUniforms = 0;
	defaultResource.maxGeometryImageUniforms = 0;
	defaultResource.maxFragmentImageUniforms = 8;
	defaultResource.maxCombinedImageUniforms = 8;
	defaultResource.maxGeometryTextureImageUnits = 16;
	defaultResource.maxGeometryOutputVertices = 256;
	defaultResource.maxGeometryTotalOutputComponents = 1024;
	defaultResource.maxGeometryUniformComponents = 1024;
	defaultResource.maxGeometryVaryingComponents = 64;
	defaultResource.maxTessControlInputComponents = 128;
	defaultResource.maxTessControlOutputComponents = 128;
	defaultResource.maxTessControlTextureImageUnits = 16;
	defaultResource.maxTessControlUniformComponents = 1024;
	defaultResource.maxTessControlTotalOutputComponents = 4096;
	defaultResource.maxTessEvaluationInputComponents = 128;
	defaultResource.maxTessEvaluationOutputComponents = 128;
	defaultResource.maxTessEvaluationTextureImageUnits = 16;
	defaultResource.maxTessEvaluationUniformComponents = 1024;
	defaultResource.maxTessPatchComponents = 120;
	defaultResource.maxPatchVertices = 32;
	defaultResource.maxTessGenLevel = 64;
	defaultResource.maxViewports = 16;
	defaultResource.maxVertexAtomicCounters = 0;
	defaultResource.maxTessControlAtomicCounters = 0;
	defaultResource.maxTessEvaluationAtomicCounters = 0;
	defaultResource.maxGeometryAtomicCounters = 0;
	defaultResource.maxFragmentAtomicCounters = 8;
	defaultResource.maxCombinedAtomicCounters = 8;
	defaultResource.maxAtomicCounterBindings = 1;
	defaultResource.maxVertexAtomicCounterBuffers = 0;
	defaultResource.maxTessControlAtomicCounterBuffers = 0;
	defaultResource.maxTessEvaluationAtomicCounterBuffers = 0;
	defaultResource.maxGeometryAtomicCounterBuffers = 0;
	defaultResource.maxFragmentAtomicCounterBuffers = 1;
	defaultResource.maxCombinedAtomicCounterBuffers = 1;
	defaultResource.maxAtomicCounterBufferSize = 16384;
	defaultResource.maxTransformFeedbackBuffers = 4;
	defaultResource.maxTransformFeedbackInterleavedComponents = 64;
	defaultResource.maxCullDistances = 8;
	defaultResource.maxCombinedClipAndCullDistances = 8;
	defaultResource.maxSamples = 4;
	defaultResource.maxMeshOutputVerticesNV = 256;
	defaultResource.maxMeshOutputPrimitivesNV = 512;
	defaultResource.maxMeshWorkGroupSizeX_NV = 32;
	defaultResource.maxMeshWorkGroupSizeY_NV = 1;
	defaultResource.maxMeshWorkGroupSizeZ_NV = 1;
	defaultResource.maxTaskWorkGroupSizeX_NV = 32;
	defaultResource.maxTaskWorkGroupSizeY_NV = 1;
	defaultResource.maxTaskWorkGroupSizeZ_NV = 1;
	defaultResource.maxMeshViewCountNV = 4;

	defaultResource.limits = {};
	defaultResource.limits.nonInductiveForLoops = true;
	defaultResource.limits.whileLoops = true;
	defaultResource.limits.doWhileLoops = true;
	defaultResource.limits.generalUniformIndexing = true;
	defaultResource.limits.generalAttributeMatrixVectorIndexing = true;
	defaultResource.limits.generalVaryingIndexing = true;
	defaultResource.limits.generalSamplerIndexing = true;
	defaultResource.limits.generalVariableIndexing = true;
	defaultResource.limits.generalConstantMatrixVectorIndexing = true;
	return defaultResource;
};

std::vector<uint32> RendererShader::GLSLToSPIRV(const std::string& glslCode)
{
	const bool compileWithDebugInfo = ((VulkanRenderer*)g_renderer.get())->IsTracingToolEnabled();

	// try to retrieve SPIR-V module from cache
	if (s_isLoadingShaders && (m_isGameShader && !m_isGfxPackShader) && s_spirvCache && !compileWithDebugInfo)
	{
		cemu_assert_debug(m_baseHash != 0);
		uint64 h1, h2;
		GenerateShaderPrecompiledCacheFilename(m_type, m_baseHash, m_auxHash, h1, h2);
		std::vector<uint8> cacheFileData;
		if (s_spirvCache->GetFile({ h1, h2 }, cacheFileData))
		{
			/*
			// generate shader from cached SPIR-V buffer
			CreateVkShaderModule(std::span<uint32>((uint32*)cacheFileData.data(), cacheFileData.size() / sizeof(uint32)));
			FinishCompilation();*/
			cemu_assert(cacheFileData.size() % 4 == 0);
			std::vector<uint32> cacheFileData32 (cacheFileData.size()/4, 0);
			std::memcpy(cacheFileData32.data(), cacheFileData.data(), cacheFileData.size());
			return cacheFileData32;
		}
	}

	EShLanguage state;
	switch (GetType())
	{
	case ShaderType::kVertex:
		state = EShLangVertex;
		break;
	case ShaderType::kFragment:
		state = EShLangFragment;
		break;
	case ShaderType::kGeometry:
		state = EShLangGeometry;
		break;
	default:
		cemu_assert_debug(false);
	}

	glslang::TShader Shader(state);
	const char* cstr = glslCode.c_str();
	Shader.setStrings(&cstr, 1);
	Shader.setEnvInput(glslang::EShSourceGlsl, state, glslang::EShClientVulkan, 100);
	Shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetClientVersion::EShTargetVulkan_1_1);
	Shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetLanguageVersion::EShTargetSpv_1_3);

	std::string PreprocessedGLSL;
	glslang::TShader::ForbidIncluder Includer;
	TBuiltInResource Resources = GetDefaultBuiltInResource();
	EShMessages messagesPreprocess = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
	if (!Shader.preprocess(&Resources, 450, ENoProfile, false, false, messagesPreprocess, &PreprocessedGLSL, Includer))
	{
		cemuLog_log(LogType::Force, fmt::format("GLSL Preprocessing Failed For {:016x}_{:016x}: \"{}\"", m_baseHash, m_auxHash, Shader.getInfoLog()));
		FinishCompilation();
		return {};
	}

	const char* PreprocessedCStr = PreprocessedGLSL.c_str();
	Shader.setStrings(&PreprocessedCStr, 1);
	EShMessages messagesParseLink = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
	if (!Shader.parse(&Resources, 100, false, messagesParseLink))
	{
		cemuLog_log(LogType::Force, fmt::format("GLSL parsing failed for {:016x}_{:016x}: \"{}\"", m_baseHash, m_auxHash, Shader.getInfoLog()));
		cemuLog_logDebug(LogType::Force, "GLSL source:\n{}", glslCode);
		cemu_assert_debug(false);
		return {};
	}

	glslang::TProgram Program;
	Program.addShader(&Shader);

	if (!Program.link(messagesParseLink))
	{
		cemuLog_log(LogType::Force, fmt::format("GLSL linking failed for {:016x}_{:016x}: \"{}\"", m_baseHash, m_auxHash, Program.getInfoLog()));
		cemu_assert_debug(false);
		return {};
	}

	if (!Program.mapIO())
	{
		cemuLog_log(LogType::Force, fmt::format("GLSL linking failed for {:016x}_{:016x}: \"{}\"", m_baseHash, m_auxHash, Program.getInfoLog()));
		return {};
	}

	// temp storage for SPIR-V after translation
	std::vector<uint32> spirvBuffer;
	spv::SpvBuildLogger logger;

	glslang::SpvOptions spvOptions;
	spvOptions.disableOptimizer = false;
	spvOptions.validate = false;
	spvOptions.optimizeSize = true;
	if (compileWithDebugInfo)
	{
		spvOptions.generateDebugInfo = true;
		spvOptions.emitNonSemanticShaderDebugInfo = true;
		spvOptions.emitNonSemanticShaderDebugSource = true;

		Shader.addSourceText(glslCode.c_str(), (uint32)glslCode.size());
		Shader.setSourceFile(fmt::format("shader_{:016x}_{:016x}.glsl", m_baseHash, m_auxHash).c_str());
	}

	//auto beginTime = benchmarkTimer_start();

	GlslangToSpv(*Program.getIntermediate(state), spirvBuffer, &logger, &spvOptions);

	//double timeDur = benchmarkTimer_stop(beginTime);
	//forceLogRemoveMe_printf("Shader GLSL-to-SPIRV compilation took %lfms Size %08x", timeDur, spirvBuffer.size()*4);

	// store in cache, unless it got compiled with debug info or is a modified shader from a gfx pack
	if (s_spirvCache && m_isGameShader && m_isGfxPackShader == false && !compileWithDebugInfo)
	{
		uint64 h1, h2;
		GenerateShaderPrecompiledCacheFilename(m_type, m_baseHash, m_auxHash, h1, h2);
		s_spirvCache->AddFile({ h1, h2 }, (const uint8*)spirvBuffer.data(), spirvBuffer.size() * sizeof(uint32));
	}
	return spirvBuffer;
}

void RendererShader::FinishCompilation()
{
}
