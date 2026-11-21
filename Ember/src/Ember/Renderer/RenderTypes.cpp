#include "Ember/Renderer/RenderTypes.h"

#include <algorithm>
#include <cmath>

namespace Ember {

	AABB AABB::Transformed(const glm::mat4& transform) const
	{
		// Arvo's method: transform the center and accumulate absolute extents.
		const glm::vec3 center = glm::vec3(transform * glm::vec4(Center(), 1.0f));
		const glm::vec3 extents = Extents();
		glm::vec3 newExtents(0.0f);
		for (int i = 0; i < 3; i++)
		{
			for (int j = 0; j < 3; j++)
				newExtents[i] += std::abs(transform[j][i]) * extents[j];
		}
		return AABB{ center - newExtents, center + newExtents };
	}

	RenderSettings RenderSettings::LowQuality()
	{
		RenderSettings settings;
		settings.ShadowMapSize = 1024;
		settings.ShadowCascadeCount = 2;
		settings.ShadowDistance = 40.0f;
		settings.SSAOSampleCount = 8;
		settings.EnvironmentCubeSize = 256;
		settings.IrradianceSize = 16;
		settings.PrefilteredSize = 64;
		settings.PrefilterSampleCount = 48;
		return settings;
	}

}
