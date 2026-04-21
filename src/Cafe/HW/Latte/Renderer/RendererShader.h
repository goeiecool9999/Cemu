#pragma once

#include "util/helpers/Semaphore.h"
class RendererShader
{
	friend class ShaderThreadPool;
public:
	enum class ShaderType
	{
		kVertex,
		kFragment,
		kGeometry
	};

	enum class COMPILATION_STATE : uint32
	{
		NONE,
		QUEUED,
		COMPILING,
		DONE
	};

	virtual ~RendererShader() = default;

	ShaderType GetType() const { return m_type; }
	
	void PreponeCompilation(); // if shader not yet compiled, compile it synchronously (if possible) or alternatively wait for compilation. After this function IsCompiled() is guaranteed to be true
	virtual bool IsCompiled() = 0;
	virtual bool WaitForCompiled() = 0;

	std::vector<uint32> GLSLToSPIRV(const std::string& glslCode);


protected:
	// if isGameShader is true, then baseHash and auxHash are valid
	RendererShader(ShaderType type, uint64 baseHash, uint64 auxHash, bool isGameShader, bool isGfxPackShader);

	virtual void CompileInternal() {};
	virtual void FinishCompilation();

	static uint32 GeneratePrecompiledCacheId();
	static void GenerateShaderPrecompiledCacheFilename(ShaderType type, uint64 baseHash, uint64 auxHash, uint64& h1, uint64& h2);

protected:
	StateSemaphore<COMPILATION_STATE> m_compilationState{ COMPILATION_STATE::NONE };
	ShaderType m_type;
	uint64 m_baseHash;	
	uint64 m_auxHash;
	bool m_isGameShader;
	bool m_isGfxPackShader;
};

