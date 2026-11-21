#include "EditorCamera.h"

#include "Ember/Core/Input.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace Ember {

	EditorCamera::EditorCamera() = default;

	glm::vec3 EditorCamera::GetForward() const
	{
		return glm::normalize(glm::vec3(-std::sin(m_Yaw) * std::cos(m_Pitch), std::sin(m_Pitch), -std::cos(m_Yaw) * std::cos(m_Pitch)));
	}

	glm::vec3 EditorCamera::GetRight() const
	{
		return glm::normalize(glm::cross(GetForward(), glm::vec3(0.0f, 1.0f, 0.0f)));
	}

	glm::vec3 EditorCamera::GetUp() const
	{
		return glm::normalize(glm::cross(GetRight(), GetForward()));
	}

	glm::vec3 EditorCamera::GetPosition() const
	{
		return m_FocalPoint - GetForward() * m_Distance;
	}

	glm::mat4 EditorCamera::GetViewMatrix() const
	{
		return glm::lookAt(GetPosition(), m_FocalPoint, glm::vec3(0.0f, 1.0f, 0.0f));
	}

	glm::mat4 EditorCamera::GetProjection() const
	{
		return glm::perspective(m_FOV, m_AspectRatio, m_NearClip, m_FarClip);
	}

	RenderCamera EditorCamera::GetRenderCamera() const
	{
		RenderCamera camera;
		camera.View = GetViewMatrix();
		camera.Projection = GetProjection();
		camera.NearClip = m_NearClip;
		camera.FarClip = m_FarClip;
		camera.Orthographic = false;
		return camera;
	}

	void EditorCamera::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (width > 0 && height > 0)
			m_AspectRatio = static_cast<float>(width) / static_cast<float>(height);
	}

	void EditorCamera::LookAt(const glm::vec3& position, const glm::vec3& target)
	{
		glm::vec3 direction = target - position;
		float distance = glm::length(direction);
		if (distance < 1e-4f)
		{
			direction = glm::vec3(0.0f, 0.0f, -1.0f);
			distance = 1.0f;
		}
		direction /= distance;
		m_Pitch = std::asin(std::clamp(direction.y, -1.0f, 1.0f));
		m_Yaw = std::atan2(-direction.x, -direction.z);
		m_FocalPoint = target;
		m_Distance = distance;
	}

	void EditorCamera::Focus(const glm::vec3& center, float radius)
	{
		m_FocalPoint = center;
		m_Distance = std::max(radius, 0.1f) / std::sin(m_FOV * 0.5f) * 1.1f;
	}

	void EditorCamera::OnUpdate(Timestep ts, bool inputEnabled)
	{
		const glm::vec2 mouse = Input::GetMousePosition();
		const glm::vec2 delta = m_HadMouse ? (mouse - m_LastMouse) : glm::vec2(0.0f);
		m_LastMouse = mouse;
		m_HadMouse = true;
		if (!inputEnabled)
			return;

		const float dt = ts.GetSeconds();
		const bool alt = Input::IsKeyPressed(KeyCode::LeftAlt) || Input::IsKeyPressed(KeyCode::RightAlt);
		const bool shift = Input::IsKeyPressed(KeyCode::LeftShift) || Input::IsKeyPressed(KeyCode::RightShift);
		const bool middle = Input::IsMouseButtonPressed(MouseButton::Middle);
		const bool left = Input::IsMouseButtonPressed(MouseButton::Left);
		const bool right = Input::IsMouseButtonPressed(MouseButton::Right);

		if (right)
		{
			// Fly mode: rotate in place, WASDQE to move.
			const glm::vec3 position = GetPosition();
			m_Yaw -= delta.x * 0.004f;
			m_Pitch = std::clamp(m_Pitch - delta.y * 0.004f, -1.55f, 1.55f);
			float speed = std::max(m_Distance, 1.0f) * (shift ? 3.0f : 1.0f);
			glm::vec3 move(0.0f);
			if (Input::IsKeyPressed(KeyCode::W)) move += GetForward();
			if (Input::IsKeyPressed(KeyCode::S)) move -= GetForward();
			if (Input::IsKeyPressed(KeyCode::D)) move += GetRight();
			if (Input::IsKeyPressed(KeyCode::A)) move -= GetRight();
			if (Input::IsKeyPressed(KeyCode::E)) move += glm::vec3(0.0f, 1.0f, 0.0f);
			if (Input::IsKeyPressed(KeyCode::Q)) move -= glm::vec3(0.0f, 1.0f, 0.0f);
			const glm::vec3 newPosition = position + move * speed * dt;
			m_FocalPoint = newPosition + GetForward() * m_Distance;
		}
		else if (middle && shift)
		{
			const float panSpeed = m_Distance * 0.0015f;
			m_FocalPoint += (-GetRight() * delta.x + GetUp() * delta.y) * panSpeed;
		}
		else if ((alt && left) || middle)
		{
			m_Yaw -= delta.x * 0.005f;
			m_Pitch = std::clamp(m_Pitch - delta.y * 0.005f, -1.55f, 1.55f);
		}

		const float scroll = Input::GetScrollDelta().y;
		if (scroll != 0.0f)
			m_Distance = std::clamp(m_Distance * std::pow(0.88f, scroll), 0.1f, 5000.0f);
	}

}
