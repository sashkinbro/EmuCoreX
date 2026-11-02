// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <vulkan/vulkan_core.h>
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
#include <initializer_list>
#endif

namespace Vulkan
{
inline bool ShouldUseSwapchainPresentModes(bool maintenance_enabled, bool mali_sx2)
{
	// MaliSX2 must use the ordinary create path from the first attempt. Keep
	// maintenance1 enabled for present fences and releasing acquired images.
	return maintenance_enabled && !mali_sx2;
}

template <typename Create>
VkResult CreateSwapchainWithPresentModeFallback(const VkSwapchainCreateInfoKHR& info, Create&& create, bool* retried)
{
	*retried = false;
	const VkResult result = create(info);
	if (result != VK_ERROR_INITIALIZATION_FAILED || !info.pNext)
		return result;

	const auto* chain = static_cast<const VkBaseInStructure*>(info.pNext);
	if (chain->sType != VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_KHR || chain->pNext)
		return result;

	// Some Android loaders reject the optional maintenance1 present-mode list.
	// Retry the ordinary swapchain only after that failure, retaining every other
	// setting and leaving successful drivers and unrelated extension chains intact.
	VkSwapchainCreateInfoKHR fallback = info;
	fallback.pNext = nullptr;
	// The first create attempt retires oldSwapchain even when it fails (VUID 01933).
	// Its owner destroys it after this operation; it cannot be passed a second time.
	fallback.oldSwapchain = VK_NULL_HANDLE;
	*retried = true;
	return create(fallback);
}

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
inline bool TestSwapchainPresentModeFallback()
{
	if (ShouldUseSwapchainPresentModes(true, true) ||
		!ShouldUseSwapchainPresentModes(true, false) ||
		ShouldUseSwapchainPresentModes(false, true) || ShouldUseSwapchainPresentModes(false, false))
		return false;
	const VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
	VkSwapchainPresentModesCreateInfoKHR modes = {
		VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_KHR, nullptr, 1, &mode};
	VkSwapchainCreateInfoKHR info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
	info.pNext = &modes;
	info.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
	info.imageExtent = {1280, 720};
	info.minImageCount = 3;
	info.presentMode = mode;
	info.oldSwapchain = reinterpret_cast<VkSwapchainKHR>(0x1234);
	bool retried = true;
	int calls = 0;
	bool preserved = true;
	const auto broken_icd = [&](const VkSwapchainCreateInfoKHR& candidate) {
		calls++;
		preserved &= candidate.imageFormat == info.imageFormat && candidate.imageExtent.width == 1280 &&
			candidate.imageExtent.height == 720 && candidate.minImageCount == 3 &&
			candidate.oldSwapchain == (calls == 1 ? info.oldSwapchain : VK_NULL_HANDLE) && candidate.presentMode == mode;
		return candidate.pNext ? VK_ERROR_INITIALIZATION_FAILED : VK_SUCCESS;
	};
	if (CreateSwapchainWithPresentModeFallback(info, broken_icd, &retried) != VK_SUCCESS ||
		!retried || calls != 2 || !preserved || info.pNext != &modes)
		return false;

	for (const VkResult result : {VK_SUCCESS, VK_ERROR_OUT_OF_DEVICE_MEMORY, VK_ERROR_DEVICE_LOST})
	{
		calls = 0;
		const auto icd = [&](const VkSwapchainCreateInfoKHR& candidate) { calls++; return result; };
		if (CreateSwapchainWithPresentModeFallback(info, icd, &retried) != result || retried || calls != 1)
			return false;
	}

	const auto failing_icd = [&](const VkSwapchainCreateInfoKHR&) { calls++; return VK_ERROR_INITIALIZATION_FAILED; };
	calls = 0;
	if (CreateSwapchainWithPresentModeFallback(info, failing_icd, &retried) != VK_ERROR_INITIALIZATION_FAILED ||
		!retried || calls != 2)
		return false;

	info.pNext = nullptr;
	calls = 0;
	if (CreateSwapchainWithPresentModeFallback(info, failing_icd, &retried) != VK_ERROR_INITIALIZATION_FAILED ||
		retried || calls != 1)
		return false;

	// Never discard other chained settings, including desktop fullscreen control.
	VkBaseInStructure other = {VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr};
	modes.pNext = &other;
	info.pNext = &modes;
	calls = 0;
	if (CreateSwapchainWithPresentModeFallback(info, failing_icd, &retried) != VK_ERROR_INITIALIZATION_FAILED ||
		retried || calls != 1)
		return false;
	info.pNext = &other;
	calls = 0;
	return CreateSwapchainWithPresentModeFallback(info, failing_icd, &retried) == VK_ERROR_INITIALIZATION_FAILED &&
		!retried && calls == 1;
}
#endif
} // namespace Vulkan
