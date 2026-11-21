// Vulkan implementation of GraphicsDevice.
//
// NVRHI (linked statically) uses Vulkan-Hpp's dynamic dispatcher, whose storage must be defined by the
// application exactly once - it is defined here. Ember's own Vulkan calls go through the same dispatcher's
// raw C function pointers, so there is a single source of Vulkan entry points in the process and no mixing of
// Vulkan-Hpp configuration macros across translation units.

#include "Ember/Renderer/GraphicsDevice.h"

#include "Ember/Core/Log.h"
#include "Ember/Core/Window.h"

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include <nvrhi/validation.h>
#include <nvrhi/vulkan.h>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <optional>
#include <string_view>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace Ember {

	namespace {

		auto& VK() { return VULKAN_HPP_DEFAULT_DISPATCHER; }

		std::string ToLower(std::string_view text)
		{
			std::string result(text);
			std::transform(result.begin(), result.end(), result.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return result;
		}

		bool HasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name)
		{
			return std::any_of(extensions.begin(), extensions.end(),
				[name](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
		}

		class NvrhiMessageCallback final : public nvrhi::IMessageCallback
		{
		public:
			explicit NvrhiMessageCallback(std::atomic<uint32_t>& errorCount) : m_ErrorCount(errorCount) {}

			void message(nvrhi::MessageSeverity severity, const char* messageText) override
			{
				switch (severity)
				{
					case nvrhi::MessageSeverity::Info:    EMBER_CORE_TRACE("[NVRHI] {}", messageText); break;
					case nvrhi::MessageSeverity::Warning: EMBER_CORE_WARN("[NVRHI] {}", messageText); break;
					case nvrhi::MessageSeverity::Error:
					case nvrhi::MessageSeverity::Fatal:
						EMBER_CORE_ERROR("[NVRHI] {}", messageText);
						m_ErrorCount++;
						break;
				}
			}

		private:
			std::atomic<uint32_t>& m_ErrorCount;
		};

		VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
			VkDebugUtilsMessageTypeFlagsEXT /*types*/, const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData)
		{
			auto* errorCount = static_cast<std::atomic<uint32_t>*>(userData);
			const char* text = data && data->pMessage ? data->pMessage : "(no message)";
			if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
			{
				EMBER_CORE_ERROR("[Vulkan] {}", text);
				(*errorCount)++;
			}
			else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
			{
				EMBER_CORE_WARN("[Vulkan] {}", text);
			}
			return VK_FALSE;
		}

	}

	class VulkanGraphicsDevice final : public GraphicsDevice
	{
	public:
		explicit VulkanGraphicsDevice(const GraphicsDeviceSpecification& specification)
			: m_Specification(specification), m_MessageCallback(m_ErrorCount)
		{
		}

		~VulkanGraphicsDevice() override
		{
			Destroy();
		}

		bool Initialize()
		{
			VK().init();
			if (!VK().vkCreateInstance)
			{
				EMBER_CORE_ERROR("Vulkan loader not found");
				return false;
			}

			if (!CreateInstance())
				return false;
			if (m_Specification.TargetWindow && !CreateSurface())
				return false;
			if (!PickPhysicalDevice())
				return false;
			if (!CreateLogicalDevice())
				return false;
			if (!CreateNvrhiDevice())
				return false;
			if (m_Surface != VK_NULL_HANDLE)
			{
				m_PresentCommandList = m_NvrhiDevice->createCommandList();
				if (!CreateSwapchain())
					return false;
			}
			return true;
		}

		nvrhi::IDevice* GetDevice() const override { return m_NvrhiDevice; }
		const std::string& GetAdapterName() const override { return m_AdapterName; }
		bool IsHeadless() const override { return m_Surface == VK_NULL_HANDLE; }

		bool BeginFrame() override
		{
			if (IsHeadless())
				return true;

			if (m_SwapchainDirty || m_Swapchain == VK_NULL_HANDLE)
			{
				if (!RecreateSwapchain())
					return false;
			}

			for (int attempt = 0; attempt < 3; attempt++)
			{
				VkSemaphore semaphore = m_AcquireSemaphores[m_AcquireSemaphoreIndex];
				const VkResult result = VK().vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, semaphore,
					VK_NULL_HANDLE, &m_SwapchainIndex);

				if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
				{
					m_AcquireSemaphoreIndex = (m_AcquireSemaphoreIndex + 1) % static_cast<uint32_t>(m_AcquireSemaphores.size());
					m_VulkanDevice->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, semaphore, 0);
					if (result == VK_SUBOPTIMAL_KHR)
						m_SwapchainDirty = true;
					m_FrameAcquired = true;
					return true;
				}

				if (result != VK_ERROR_OUT_OF_DATE_KHR)
				{
					EMBER_CORE_ERROR("vkAcquireNextImageKHR failed: {}", nvrhi::vulkan::resultToString(result));
					return false;
				}

				if (!RecreateSwapchain())
					return false;
			}
			return false;
		}

		void Present() override
		{
			if (IsHeadless() || !m_FrameAcquired)
				return;
			m_FrameAcquired = false;

			VkSemaphore presentSemaphore = m_PresentSemaphores[m_SwapchainIndex];
			m_VulkanDevice->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, presentSemaphore, 0);

			// NVRHI only submits pending semaphore signals together with a command list.
			m_PresentCommandList->open();
			m_PresentCommandList->close();
			m_NvrhiDevice->executeCommandList(m_PresentCommandList);

			VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
			presentInfo.waitSemaphoreCount = 1;
			presentInfo.pWaitSemaphores = &presentSemaphore;
			presentInfo.swapchainCount = 1;
			presentInfo.pSwapchains = &m_Swapchain;
			presentInfo.pImageIndices = &m_SwapchainIndex;
			const VkResult result = VK().vkQueuePresentKHR(m_GraphicsQueue, &presentInfo);
			if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
				m_SwapchainDirty = true;
			else if (result != VK_SUCCESS)
				EMBER_CORE_ERROR("vkQueuePresentKHR failed: {}", nvrhi::vulkan::resultToString(result));

			// Limit CPU run-ahead so per-frame resources are never overwritten while the GPU still uses them.
			while (m_FramesInFlight.size() >= m_Specification.MaxFramesInFlight)
			{
				nvrhi::EventQueryHandle query = m_FramesInFlight.front();
				m_FramesInFlight.pop_front();
				m_NvrhiDevice->waitEventQuery(query);
				m_QueryPool.push_back(query);
			}

			nvrhi::EventQueryHandle query;
			if (!m_QueryPool.empty())
			{
				query = m_QueryPool.back();
				m_QueryPool.pop_back();
			}
			else
			{
				query = m_NvrhiDevice->createEventQuery();
			}
			m_NvrhiDevice->resetEventQuery(query);
			m_NvrhiDevice->setEventQuery(query, nvrhi::CommandQueue::Graphics);
			m_FramesInFlight.push_back(query);

			m_NvrhiDevice->runGarbageCollection();
		}

		nvrhi::ITexture* GetCurrentBackBuffer() const override
		{
			if (IsHeadless() || m_BackBuffers.empty())
				return nullptr;
			return m_BackBuffers[m_SwapchainIndex].Texture;
		}

		nvrhi::IFramebuffer* GetCurrentFramebuffer() const override
		{
			if (IsHeadless() || m_BackBuffers.empty())
				return nullptr;
			return m_BackBuffers[m_SwapchainIndex].Framebuffer;
		}

		nvrhi::Format GetBackBufferFormat() const override { return m_BackBufferFormat; }
		uint32_t GetBackBufferWidth() const override { return m_SwapchainExtent.width; }
		uint32_t GetBackBufferHeight() const override { return m_SwapchainExtent.height; }

		void SetVSync(bool enabled) override
		{
			if (m_Specification.VSync != enabled)
			{
				m_Specification.VSync = enabled;
				m_SwapchainDirty = true;
			}
		}

		bool IsVSync() const override { return m_Specification.VSync; }
		void OnWindowResize() override { m_SwapchainDirty = true; }

	private:
		bool CreateInstance()
		{
			uint32_t apiVersion = VK_API_VERSION_1_0;
			if (VK().vkEnumerateInstanceVersion)
				VK().vkEnumerateInstanceVersion(&apiVersion);
			if (apiVersion < VK_API_VERSION_1_3)
			{
				EMBER_CORE_ERROR("Vulkan 1.3 loader required (found {}.{})", VK_API_VERSION_MAJOR(apiVersion),
					VK_API_VERSION_MINOR(apiVersion));
				return false;
			}

			uint32_t count = 0;
			VK().vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
			std::vector<VkExtensionProperties> available(count);
			VK().vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());

			std::vector<const char*> extensions;
			if (m_Specification.TargetWindow)
			{
				uint32_t glfwCount = 0;
				const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwCount);
				if (!glfwExtensions)
				{
					EMBER_CORE_ERROR("GLFW reports Vulkan presentation is not supported on this system");
					return false;
				}
				extensions.insert(extensions.end(), glfwExtensions, glfwExtensions + glfwCount);
			}

			VkInstanceCreateFlags flags = 0;
			if (HasExtension(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
			{
				extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
				flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
			}
			const bool hasDebugUtils = HasExtension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			if (hasDebugUtils)
				extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

			std::vector<const char*> layers;
			if (m_Specification.EnableValidation)
			{
				uint32_t layerCount = 0;
				VK().vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
				std::vector<VkLayerProperties> availableLayers(layerCount);
				VK().vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
				const bool hasValidation = std::any_of(availableLayers.begin(), availableLayers.end(),
					[](const VkLayerProperties& l) { return std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0; });
				if (hasValidation)
					layers.push_back("VK_LAYER_KHRONOS_validation");
				else
					EMBER_CORE_WARN("Vulkan validation requested but VK_LAYER_KHRONOS_validation is not installed");
			}

			VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
			appInfo.pApplicationName = "Ember";
			appInfo.pEngineName = "Ember";
			appInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
			appInfo.apiVersion = VK_API_VERSION_1_3;

			VkInstanceCreateInfo createInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
			createInfo.flags = flags;
			createInfo.pApplicationInfo = &appInfo;
			createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
			createInfo.ppEnabledExtensionNames = extensions.data();
			createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
			createInfo.ppEnabledLayerNames = layers.data();

			const VkResult result = VK().vkCreateInstance(&createInfo, nullptr, &m_Instance);
			if (result != VK_SUCCESS)
			{
				EMBER_CORE_ERROR("vkCreateInstance failed: {}", nvrhi::vulkan::resultToString(result));
				return false;
			}
			VK().init(vk::Instance(m_Instance));
			m_InstanceExtensions = extensions;

			if (hasDebugUtils && !layers.empty())
			{
				VkDebugUtilsMessengerCreateInfoEXT messengerInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
				messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
				messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
				messengerInfo.pfnUserCallback = DebugUtilsCallback;
				messengerInfo.pUserData = &m_ErrorCount;
				VK().vkCreateDebugUtilsMessengerEXT(m_Instance, &messengerInfo, nullptr, &m_DebugMessenger);
			}
			return true;
		}

		bool CreateSurface()
		{
			const VkResult result = glfwCreateWindowSurface(m_Instance, m_Specification.TargetWindow->GetNativeWindow(),
				nullptr, &m_Surface);
			if (result != VK_SUCCESS)
			{
				EMBER_CORE_ERROR("glfwCreateWindowSurface failed: {}", nvrhi::vulkan::resultToString(result));
				return false;
			}
			return true;
		}

		struct DeviceCandidate
		{
			VkPhysicalDevice Device = VK_NULL_HANDLE;
			uint32_t QueueFamily = 0;
			int Score = 0;
			std::string Name;
		};

		std::optional<DeviceCandidate> EvaluateDevice(VkPhysicalDevice device)
		{
			VkPhysicalDeviceProperties properties{};
			VK().vkGetPhysicalDeviceProperties(device, &properties);
			DeviceCandidate candidate;
			candidate.Device = device;
			candidate.Name = properties.deviceName;

			if (properties.apiVersion < VK_API_VERSION_1_3)
			{
				EMBER_CORE_INFO("  Skipping '{}': Vulkan 1.3 not supported", candidate.Name);
				return std::nullopt;
			}

			VkPhysicalDeviceVulkan12Features features12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
			VkPhysicalDeviceVulkan13Features features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
			features12.pNext = &features13;
			VkPhysicalDeviceFeatures2 features2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			features2.pNext = &features12;
			VK().vkGetPhysicalDeviceFeatures2(device, &features2);
			if (!features12.timelineSemaphore || !features13.dynamicRendering || !features13.synchronization2)
			{
				EMBER_CORE_INFO("  Skipping '{}': missing timelineSemaphore/dynamicRendering/synchronization2", candidate.Name);
				return std::nullopt;
			}

			uint32_t extensionCount = 0;
			VK().vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
			std::vector<VkExtensionProperties> extensions(extensionCount);
			VK().vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());
			if (m_Surface != VK_NULL_HANDLE && !HasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
			{
				EMBER_CORE_INFO("  Skipping '{}': no swapchain support", candidate.Name);
				return std::nullopt;
			}

			uint32_t familyCount = 0;
			VK().vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
			std::vector<VkQueueFamilyProperties> families(familyCount);
			VK().vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());

			bool foundQueue = false;
			for (uint32_t i = 0; i < familyCount; i++)
			{
				const VkQueueFlags required = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
				if ((families[i].queueFlags & required) != required)
					continue;
				if (m_Surface != VK_NULL_HANDLE)
				{
					VkBool32 presentSupported = VK_FALSE;
					VK().vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface, &presentSupported);
					if (!presentSupported)
						continue;
				}
				candidate.QueueFamily = i;
				foundQueue = true;
				break;
			}
			if (!foundQueue)
			{
				EMBER_CORE_INFO("  Skipping '{}': no graphics+compute(+present) queue family", candidate.Name);
				return std::nullopt;
			}

			switch (properties.deviceType)
			{
				case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   candidate.Score = 1000; break;
				case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: candidate.Score = 500; break;
				case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    candidate.Score = 250; break;
				case VK_PHYSICAL_DEVICE_TYPE_CPU:            candidate.Score = 10; break;
				default:                                     candidate.Score = 50; break;
			}
			return candidate;
		}

		bool PickPhysicalDevice()
		{
			uint32_t count = 0;
			VK().vkEnumeratePhysicalDevices(m_Instance, &count, nullptr);
			std::vector<VkPhysicalDevice> devices(count);
			VK().vkEnumeratePhysicalDevices(m_Instance, &count, devices.data());
			if (devices.empty())
			{
				EMBER_CORE_ERROR("No Vulkan physical devices found");
				return false;
			}

			std::string preferred = m_Specification.PreferredAdapter;
			if (preferred.empty())
			{
				if (const char* env = std::getenv("EMBER_GPU"))
					preferred = env;
			}
			preferred = ToLower(preferred);

			std::optional<DeviceCandidate> best;
			for (VkPhysicalDevice device : devices)
			{
				std::optional<DeviceCandidate> candidate = EvaluateDevice(device);
				if (!candidate)
					continue;
				if (!preferred.empty() && ToLower(candidate->Name).find(preferred) != std::string::npos)
					candidate->Score += 100000;
				if (!best || candidate->Score > best->Score)
					best = candidate;
			}

			if (!best)
			{
				EMBER_CORE_ERROR("No suitable Vulkan 1.3 device found");
				return false;
			}

			m_PhysicalDevice = best->Device;
			m_QueueFamily = best->QueueFamily;
			m_AdapterName = best->Name;
			EMBER_CORE_INFO("Using GPU: {}", m_AdapterName);
			return true;
		}

		bool CreateLogicalDevice()
		{
			uint32_t extensionCount = 0;
			VK().vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extensionCount, nullptr);
			std::vector<VkExtensionProperties> available(extensionCount);
			VK().vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extensionCount, available.data());

			std::vector<const char*> extensions;
			if (m_Surface != VK_NULL_HANDLE)
				extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
			// Required by the spec when present (MoltenVK).
			if (HasExtension(available, "VK_KHR_portability_subset"))
				extensions.push_back("VK_KHR_portability_subset");

			// Query supported features and enable the subset Ember uses.
			VkPhysicalDeviceVulkan11Features supported11{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
			VkPhysicalDeviceVulkan12Features supported12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
			VkPhysicalDeviceVulkan13Features supported13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
			supported11.pNext = &supported12;
			supported12.pNext = &supported13;
			VkPhysicalDeviceFeatures2 supported{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			supported.pNext = &supported11;
			VK().vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &supported);

			VkPhysicalDeviceVulkan11Features enabled11{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
			VkPhysicalDeviceVulkan12Features enabled12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
			VkPhysicalDeviceVulkan13Features enabled13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
			enabled11.pNext = &enabled12;
			enabled12.pNext = &enabled13;
			VkPhysicalDeviceFeatures2 enabled{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			enabled.pNext = &enabled11;

			const VkPhysicalDeviceFeatures& s = supported.features;
			VkPhysicalDeviceFeatures& e = enabled.features;
			e.samplerAnisotropy = s.samplerAnisotropy;
			e.depthClamp = s.depthClamp;
			e.fillModeNonSolid = s.fillModeNonSolid;
			e.imageCubeArray = s.imageCubeArray;
			e.independentBlend = s.independentBlend;
			e.textureCompressionBC = s.textureCompressionBC;
			e.textureCompressionETC2 = s.textureCompressionETC2;
			e.shaderImageGatherExtended = s.shaderImageGatherExtended;
			e.fragmentStoresAndAtomics = s.fragmentStoresAndAtomics;
			e.shaderStorageImageWriteWithoutFormat = s.shaderStorageImageWriteWithoutFormat;
			e.wideLines = s.wideLines;

			enabled11.shaderDrawParameters = supported11.shaderDrawParameters;
			enabled12.timelineSemaphore = VK_TRUE;
			enabled12.bufferDeviceAddress = supported12.bufferDeviceAddress;
			enabled12.hostQueryReset = supported12.hostQueryReset;
			enabled12.scalarBlockLayout = supported12.scalarBlockLayout;
			enabled12.uniformBufferStandardLayout = supported12.uniformBufferStandardLayout;
			enabled13.dynamicRendering = VK_TRUE;
			enabled13.synchronization2 = VK_TRUE;
			enabled13.maintenance4 = supported13.maintenance4;

			if (enabled12.bufferDeviceAddress)
				m_BufferDeviceAddressSupported = true;

			const float priority = 1.0f;
			VkDeviceQueueCreateInfo queueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
			queueInfo.queueFamilyIndex = m_QueueFamily;
			queueInfo.queueCount = 1;
			queueInfo.pQueuePriorities = &priority;

			VkDeviceCreateInfo createInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
			createInfo.pNext = &enabled;
			createInfo.queueCreateInfoCount = 1;
			createInfo.pQueueCreateInfos = &queueInfo;
			createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
			createInfo.ppEnabledExtensionNames = extensions.data();

			const VkResult result = VK().vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device);
			if (result != VK_SUCCESS)
			{
				EMBER_CORE_ERROR("vkCreateDevice failed: {}", nvrhi::vulkan::resultToString(result));
				return false;
			}
			VK().init(vk::Device(m_Device));
			VK().vkGetDeviceQueue(m_Device, m_QueueFamily, 0, &m_GraphicsQueue);
			m_DeviceExtensions = extensions;
			return true;
		}

		bool CreateNvrhiDevice()
		{
			nvrhi::vulkan::DeviceDesc desc;
			desc.errorCB = &m_MessageCallback;
			desc.instance = m_Instance;
			desc.physicalDevice = m_PhysicalDevice;
			desc.device = m_Device;
			desc.graphicsQueue = m_GraphicsQueue;
			desc.graphicsQueueIndex = static_cast<int>(m_QueueFamily);
			desc.instanceExtensions = m_InstanceExtensions.data();
			desc.numInstanceExtensions = m_InstanceExtensions.size();
			desc.deviceExtensions = m_DeviceExtensions.data();
			desc.numDeviceExtensions = m_DeviceExtensions.size();
			desc.bufferDeviceAddressSupported = m_BufferDeviceAddressSupported;

			m_VulkanDevice = nvrhi::vulkan::createDevice(desc);
			m_NvrhiDevice = m_VulkanDevice;
			if (!m_NvrhiDevice)
			{
				EMBER_CORE_ERROR("Failed to create NVRHI Vulkan device");
				return false;
			}

			if (m_Specification.EnableValidation)
				m_NvrhiDevice = nvrhi::validation::createValidationLayer(m_NvrhiDevice);
			return true;
		}

		bool CreateSwapchain()
		{
			VkSurfaceCapabilitiesKHR capabilities{};
			VK().vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &capabilities);

			uint32_t width = 0, height = 0;
			m_Specification.TargetWindow->GetFramebufferSize(width, height);
			if (capabilities.currentExtent.width != UINT32_MAX)
			{
				width = capabilities.currentExtent.width;
				height = capabilities.currentExtent.height;
			}
			width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
			height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
			if (width == 0 || height == 0)
				return false; // minimized

			uint32_t formatCount = 0;
			VK().vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, nullptr);
			std::vector<VkSurfaceFormatKHR> formats(formatCount);
			VK().vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, formats.data());
			if (formats.empty())
			{
				EMBER_CORE_ERROR("Surface reports no formats");
				return false;
			}

			// UNORM back buffer: Ember applies tonemapping and sRGB encoding in its final pass.
			VkSurfaceFormatKHR chosen = formats[0];
			bool found = false;
			for (VkFormat candidate : { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM })
			{
				for (const VkSurfaceFormatKHR& format : formats)
				{
					if (format.format == candidate && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
					{
						chosen = format;
						found = true;
						break;
					}
				}
				if (found)
					break;
			}
			if (!found)
			{
				EMBER_CORE_ERROR("Surface supports neither BGRA8 nor RGBA8 UNORM");
				return false;
			}
			m_BackBufferFormat = chosen.format == VK_FORMAT_B8G8R8A8_UNORM ? nvrhi::Format::BGRA8_UNORM : nvrhi::Format::RGBA8_UNORM;

			uint32_t presentModeCount = 0;
			VK().vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &presentModeCount, nullptr);
			std::vector<VkPresentModeKHR> presentModes(presentModeCount);
			VK().vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &presentModeCount, presentModes.data());
			auto hasMode = [&](VkPresentModeKHR mode) { return std::find(presentModes.begin(), presentModes.end(), mode) != presentModes.end(); };
			VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
			if (!m_Specification.VSync)
			{
				if (hasMode(VK_PRESENT_MODE_MAILBOX_KHR))
					presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
				else if (hasMode(VK_PRESENT_MODE_IMMEDIATE_KHR))
					presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
			}

			uint32_t imageCount = std::max(capabilities.minImageCount + 1, 3u);
			if (capabilities.maxImageCount > 0)
				imageCount = std::min(imageCount, capabilities.maxImageCount);

			VkSwapchainKHR oldSwapchain = m_Swapchain;
			VkSwapchainCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
			createInfo.surface = m_Surface;
			createInfo.minImageCount = imageCount;
			createInfo.imageFormat = chosen.format;
			createInfo.imageColorSpace = chosen.colorSpace;
			createInfo.imageExtent = { width, height };
			createInfo.imageArrayLayers = 1;
			createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
				VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			createInfo.preTransform = (capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
				? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : capabilities.currentTransform;
			createInfo.compositeAlpha = (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
				? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
			createInfo.presentMode = presentMode;
			createInfo.clipped = VK_TRUE;
			createInfo.oldSwapchain = oldSwapchain;

			const VkResult result = VK().vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &m_Swapchain);
			if (oldSwapchain != VK_NULL_HANDLE)
				VK().vkDestroySwapchainKHR(m_Device, oldSwapchain, nullptr);
			if (result != VK_SUCCESS)
			{
				m_Swapchain = VK_NULL_HANDLE;
				EMBER_CORE_ERROR("vkCreateSwapchainKHR failed: {}", nvrhi::vulkan::resultToString(result));
				return false;
			}
			m_SwapchainExtent = { width, height };

			uint32_t swapImageCount = 0;
			VK().vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &swapImageCount, nullptr);
			std::vector<VkImage> images(swapImageCount);
			VK().vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &swapImageCount, images.data());

			for (uint32_t i = 0; i < swapImageCount; i++)
			{
				nvrhi::TextureDesc textureDesc;
				textureDesc.width = width;
				textureDesc.height = height;
				textureDesc.format = m_BackBufferFormat;
				textureDesc.debugName = "SwapchainImage";
				textureDesc.initialState = nvrhi::ResourceStates::Present;
				textureDesc.keepInitialState = true;
				textureDesc.isRenderTarget = true;

				BackBuffer backBuffer;
				backBuffer.Texture = m_NvrhiDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image,
					nvrhi::Object(images[i]), textureDesc);
				backBuffer.Framebuffer = m_NvrhiDevice->createFramebuffer(
					nvrhi::FramebufferDesc().addColorAttachment(backBuffer.Texture));
				m_BackBuffers.push_back(std::move(backBuffer));
			}

			// One acquire semaphore per frame in flight (+1 so the next acquire never reuses an in-use one),
			// one present semaphore per swapchain image.
			VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
			const size_t acquireCount = std::max<size_t>(swapImageCount, m_Specification.MaxFramesInFlight + 1);
			m_AcquireSemaphores.resize(acquireCount, VK_NULL_HANDLE);
			for (VkSemaphore& semaphore : m_AcquireSemaphores)
				VK().vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &semaphore);
			m_PresentSemaphores.resize(swapImageCount, VK_NULL_HANDLE);
			for (VkSemaphore& semaphore : m_PresentSemaphores)
				VK().vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &semaphore);
			m_AcquireSemaphoreIndex = 0;
			m_SwapchainIndex = 0;
			m_SwapchainDirty = false;

			EMBER_CORE_INFO("Swapchain created: {}x{}, {} images, {}", width, height, swapImageCount,
				presentMode == VK_PRESENT_MODE_FIFO_KHR ? "vsync" : "no vsync");
			return true;
		}

		void DestroySwapchainResources()
		{
			if (m_NvrhiDevice)
				m_NvrhiDevice->waitForIdle();
			m_FramesInFlight.clear();
			m_BackBuffers.clear();
			for (VkSemaphore semaphore : m_AcquireSemaphores)
				VK().vkDestroySemaphore(m_Device, semaphore, nullptr);
			for (VkSemaphore semaphore : m_PresentSemaphores)
				VK().vkDestroySemaphore(m_Device, semaphore, nullptr);
			m_AcquireSemaphores.clear();
			m_PresentSemaphores.clear();
		}

		bool RecreateSwapchain()
		{
			if (m_Specification.TargetWindow->IsMinimized())
				return false;
			DestroySwapchainResources();
			// Keep the old swapchain handle alive for oldSwapchain; CreateSwapchain destroys it.
			return CreateSwapchain();
		}

		void Destroy()
		{
			if (m_Device != VK_NULL_HANDLE)
			{
				DestroySwapchainResources();
				if (m_Swapchain != VK_NULL_HANDLE)
					VK().vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
				m_Swapchain = VK_NULL_HANDLE;
			}

			m_PresentCommandList = nullptr;
			m_QueryPool.clear();
			if (m_NvrhiDevice)
			{
				m_NvrhiDevice->waitForIdle();
				m_NvrhiDevice->runGarbageCollection();
			}
			m_NvrhiDevice = nullptr;
			m_VulkanDevice = nullptr;

			if (m_Device != VK_NULL_HANDLE)
				VK().vkDestroyDevice(m_Device, nullptr);
			m_Device = VK_NULL_HANDLE;

			if (m_Surface != VK_NULL_HANDLE)
				VK().vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
			m_Surface = VK_NULL_HANDLE;

			if (m_DebugMessenger != VK_NULL_HANDLE)
				VK().vkDestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);
			m_DebugMessenger = VK_NULL_HANDLE;

			if (m_Instance != VK_NULL_HANDLE)
				VK().vkDestroyInstance(m_Instance, nullptr);
			m_Instance = VK_NULL_HANDLE;
		}

	private:
		struct BackBuffer
		{
			nvrhi::TextureHandle Texture;
			nvrhi::FramebufferHandle Framebuffer;
		};

		GraphicsDeviceSpecification m_Specification;
		NvrhiMessageCallback m_MessageCallback;

		VkInstance m_Instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
		uint32_t m_QueueFamily = 0;
		bool m_BufferDeviceAddressSupported = false;
		std::string m_AdapterName;
		std::vector<const char*> m_InstanceExtensions;
		std::vector<const char*> m_DeviceExtensions;

		// m_VulkanDevice is the raw backend device (needed for semaphore interop); m_NvrhiDevice may wrap it
		// in NVRHI's validation layer and is what the rest of the engine uses.
		nvrhi::vulkan::DeviceHandle m_VulkanDevice;
		nvrhi::DeviceHandle m_NvrhiDevice;
		nvrhi::CommandListHandle m_PresentCommandList;

		VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
		VkExtent2D m_SwapchainExtent{ 0, 0 };
		nvrhi::Format m_BackBufferFormat = nvrhi::Format::RGBA8_UNORM;
		std::vector<BackBuffer> m_BackBuffers;
		std::vector<VkSemaphore> m_AcquireSemaphores;
		std::vector<VkSemaphore> m_PresentSemaphores;
		uint32_t m_AcquireSemaphoreIndex = 0;
		uint32_t m_SwapchainIndex = 0;
		bool m_SwapchainDirty = false;
		bool m_FrameAcquired = false;

		std::deque<nvrhi::EventQueryHandle> m_FramesInFlight;
		std::vector<nvrhi::EventQueryHandle> m_QueryPool;
	};

	Scope<GraphicsDevice> GraphicsDevice::Create(const GraphicsDeviceSpecification& specification)
	{
		auto device = CreateScope<VulkanGraphicsDevice>(specification);
		if (!device->Initialize())
			return nullptr;
		return device;
	}

}
