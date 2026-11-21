#include "GpuTestContext.h"

#include "Ember/Core/Base.h"

namespace EmberTest {

	namespace {

		struct GpuHolder
		{
			Ember::Scope<Ember::GraphicsDevice> Device;
			bool Attempted = false;
		};

		GpuHolder& GetHolder()
		{
			static GpuHolder s_Holder;
			return s_Holder;
		}

	}

	Ember::GraphicsDevice* GetGpu()
	{
		GpuHolder& holder = GetHolder();
		if (!holder.Attempted)
		{
			holder.Attempted = true;
			Ember::GraphicsDeviceSpecification specification;
			specification.TargetWindow = nullptr;
			// Enables NVRHI's validation layer (and Khronos validation if installed), so API misuse fails tests.
			specification.EnableValidation = true;
			holder.Device = Ember::GraphicsDevice::Create(specification);
		}
		return holder.Device.get();
	}

	void ShutdownGpu()
	{
		GetHolder().Device.reset();
	}

}
