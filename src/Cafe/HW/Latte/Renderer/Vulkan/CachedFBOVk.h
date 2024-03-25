#pragma once

#include "Cafe/HW/Latte/Core/LatteCachedFBO.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VKRBase.h"

class CachedFBOVk : public LatteCachedFBO
{
public:
	CachedFBOVk(uint64 key, VkDevice device)
		: LatteCachedFBO(key), m_device(device)
	{
		CreateRenderPass();
		CreateFramebuffer();
		InitDynamicRenderingData();
	}

	~CachedFBOVk();

	static inline FSpinlock s_spinlockDependency;

	VKRObjectFramebuffer* GetFramebufferObj() const
	{
		if(HasFeedbackLoop())
			return m_vkrObjFramebufferSelfReferencing;
		return m_vkrObjFramebuffer;
	}

	VKRObjectRenderPass* GetRenderPassObj() const
	{
		if(HasFeedbackLoop())
			return m_vkrObjRenderPassSelfReferencing;
		return m_vkrObjRenderPass;
	}
	VKRObjectRenderPass* GetRenderPassObjSelfRef() const
	{
		return m_vkrObjRenderPassSelfReferencing;
	}

	// for KHR_dynamic_rendering
	VkRenderingInfoKHR* GetRenderingInfo()
	{
		return &m_vkRenderingInfo;
	}


	void TrackDependency(class PipelineInfo* pipelineInfo)
	{
		s_spinlockDependency.lock();
		m_usedByPipelines.emplace_back(pipelineInfo);
		s_spinlockDependency.unlock();
	}

	void RemoveDependency(class PipelineInfo* pipelineInfo)
	{
		s_spinlockDependency.lock();
		vectorRemoveByValue(m_usedByPipelines, pipelineInfo);
		s_spinlockDependency.unlock();
	}

	[[nodiscard]] const VkExtent2D& GetExtend() const { return m_extend;}

	// checks if any of the sampled textures are output by the FBO
	void UpdateFeedbackLoop(VkDescriptorSetInfo* vsDS, VkDescriptorSetInfo* gsDS, VkDescriptorSetInfo* psDS);
	std::vector<LatteTextureVk*> GetFeedbackLoopedTextures() const;
	bool HasFeedbackLoop() const;

private:

	void CreateRenderPass();
	void CreateFramebuffer();

	void InitDynamicRenderingData();

	std::optional<size_t> GetColorTextureAttachmentIndex(LatteTexture* tex) const;
	VKRObjectTextureView* GetColorBufferImageView(uint32 index);
	VKRObjectTextureView* GetDepthStencilBufferImageView(bool& hasStencil);

	VkDevice m_device;
	VKRObjectRenderPass* m_vkrObjRenderPass{};
	VKRObjectRenderPass* m_vkrObjRenderPassSelfReferencing{};
	VKRObjectFramebuffer* m_vkrObjFramebuffer{};
	VKRObjectFramebuffer* m_vkrObjFramebufferSelfReferencing{};

	VkExtent2D m_extend;

	std::bitset<maxColorBuffer> m_feedbackLoopColorAttachments;
	bool m_feedbackLoopDepth{};
	std::vector<LatteTextureVk*> loopbackedTextures;

	// for KHR_dynamic_rendering
	VkRenderingInfoKHR m_vkRenderingInfo;
	VkRenderingAttachmentInfoKHR m_vkColorAttachments[maxColorBuffer];
	VkRenderingAttachmentInfoKHR m_vkDepthAttachment;
	VkRenderingAttachmentInfoKHR m_vkStencilAttachment;

	std::vector<class PipelineInfo*> m_usedByPipelines; // PipelineInfo objects which use this renderpass/framebuffer
};
