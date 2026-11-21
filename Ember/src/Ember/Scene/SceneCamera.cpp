#include "Ember/Scene/SceneCamera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace Ember {

	void SceneCamera::SetPerspective(float verticalFovRadians, float nearClip, float farClip)
	{
		m_ProjectionType = ProjectionType::Perspective;
		m_PerspectiveFOV = verticalFovRadians;
		m_PerspectiveNear = nearClip;
		m_PerspectiveFar = farClip;
		RecalculateProjection();
	}

	void SceneCamera::SetOrthographic(float size, float nearClip, float farClip)
	{
		m_ProjectionType = ProjectionType::Orthographic;
		m_OrthographicSize = size;
		m_OrthographicNear = nearClip;
		m_OrthographicFar = farClip;
		RecalculateProjection();
	}

	void SceneCamera::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0)
			return;
		m_AspectRatio = static_cast<float>(width) / static_cast<float>(height);
		RecalculateProjection();
	}

	void SceneCamera::RecalculateProjection()
	{
		// Right-handed, depth range [0, 1] (GLM_FORCE_DEPTH_ZERO_TO_ONE). NVRHI flips the viewport so that
		// +Y in NDC is up, matching glm's OpenGL-style convention: no manual Y flip is needed.
		if (m_ProjectionType == ProjectionType::Perspective)
		{
			const float nearClip = std::max(m_PerspectiveNear, 1e-4f);
			const float farClip = std::max(m_PerspectiveFar, nearClip + 1e-3f);
			m_Projection = glm::perspective(std::clamp(m_PerspectiveFOV, 0.01f, 3.13f), m_AspectRatio, nearClip, farClip);
		}
		else
		{
			const float halfHeight = m_OrthographicSize * 0.5f;
			const float halfWidth = halfHeight * m_AspectRatio;
			m_Projection = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, m_OrthographicNear, m_OrthographicFar);
		}
	}

}
