#pragma once

#include "Ember/Core/Timestep.h"
#include "Ember/Renderer/RenderTypes.h"

#include <glm/glm.hpp>

namespace Ember {

	// Orbit / fly camera for the editor viewport.
	//   Alt + LMB or MMB drag: orbit, Shift + MMB: pan, scroll: zoom, RMB + WASDQE: fly.
	class EditorCamera
	{
	public:
		EditorCamera();

		// inputEnabled: whether the viewport is hovered/focused (mouse/keyboard belong to the viewport).
		void OnUpdate(Timestep ts, bool inputEnabled);
		void SetViewportSize(uint32_t width, uint32_t height);

		void SetFocalPoint(const glm::vec3& point) { m_FocalPoint = point; }
		const glm::vec3& GetFocalPoint() const { return m_FocalPoint; }
		void SetDistance(float distance) { m_Distance = glm::max(distance, 0.05f); }
		float GetDistance() const { return m_Distance; }
		void SetPitchYaw(float pitch, float yaw) { m_Pitch = pitch; m_Yaw = yaw; }
		float GetPitch() const { return m_Pitch; }
		float GetYaw() const { return m_Yaw; }

		// Places the camera at position looking at target.
		void LookAt(const glm::vec3& position, const glm::vec3& target);
		// Frames a sphere (center, radius).
		void Focus(const glm::vec3& center, float radius);

		glm::vec3 GetPosition() const;
		glm::vec3 GetForward() const;
		glm::vec3 GetRight() const;
		glm::vec3 GetUp() const;
		glm::mat4 GetViewMatrix() const;
		glm::mat4 GetProjection() const;
		RenderCamera GetRenderCamera() const;

		float GetVerticalFOV() const { return m_FOV; }

	private:
		glm::vec3 m_FocalPoint{ 0.0f };
		float m_Distance = 10.0f;
		float m_Pitch = -0.4f; // radians, negative = looking down
		float m_Yaw = 0.6f;
		float m_FOV = 0.785398f;
		float m_NearClip = 0.05f;
		float m_FarClip = 1000.0f;
		float m_AspectRatio = 16.0f / 9.0f;
		glm::vec2 m_LastMouse{ 0.0f };
		bool m_HadMouse = false;
	};

}
