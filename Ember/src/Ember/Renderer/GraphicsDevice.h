#pragma once

#include "Ember/Core/Base.h"

#include <nvrhi/nvrhi.h>

#include <atomic>
#include <string>
#include <vector>

namespace Ember {

	class Window;

	struct GraphicsDeviceSpecification
	{
		// Window to present to. nullptr = headless (offscreen rendering only, no swapchain).
		Window* TargetWindow = nullptr;
		bool VSync = true;
#if defined(EMBER_DEBUG)
		bool EnableValidation = true;
#else
		bool EnableValidation = false;
#endif
		// Case-insensitive substring of the GPU name to prefer (e.g. "llvmpipe" for software rendering).
		// If empty, the EMBER_GPU environment variable is consulted, then the best GPU is chosen.
		std::string PreferredAdapter;
		uint32_t MaxFramesInFlight = 2;
	};

	// Owns the Vulkan instance/device, the NVRHI device, and (when a window is given) the swapchain.
	class GraphicsDevice
	{
	public:
		// Returns nullptr (and logs why) if no suitable Vulkan 1.3 device is available.
		static Scope<GraphicsDevice> Create(const GraphicsDeviceSpecification& specification);
		virtual ~GraphicsDevice() = default;

		GraphicsDevice(const GraphicsDevice&) = delete;
		GraphicsDevice& operator=(const GraphicsDevice&) = delete;

		virtual nvrhi::IDevice* GetDevice() const = 0;
		virtual const std::string& GetAdapterName() const = 0;
		virtual bool IsHeadless() const = 0;

		// Swapchain (no-ops / null in headless mode).
		// Acquires the next back buffer. Returns false if there is nothing to render to this frame
		// (window minimized or swapchain being recreated); the caller should skip rendering.
		virtual bool BeginFrame() = 0;
		virtual void Present() = 0;
		virtual nvrhi::ITexture* GetCurrentBackBuffer() const = 0;
		virtual nvrhi::IFramebuffer* GetCurrentFramebuffer() const = 0;
		virtual nvrhi::Format GetBackBufferFormat() const = 0;
		virtual uint32_t GetBackBufferWidth() const = 0;
		virtual uint32_t GetBackBufferHeight() const = 0;
		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;
		// Notifies the device that the window size changed; the swapchain is recreated lazily.
		virtual void OnWindowResize() = 0;

		// Number of errors reported by NVRHI / the Vulkan validation layer since creation.
		// Tests assert this stays zero.
		uint32_t GetErrorCount() const { return m_ErrorCount.load(); }
		void ResetErrorCount() { m_ErrorCount = 0; }

	protected:
		GraphicsDevice() = default;
		std::atomic<uint32_t> m_ErrorCount{ 0 };
	};

}
