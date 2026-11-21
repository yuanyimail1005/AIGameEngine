#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Ember::Math {

	// Decomposes an affine TRS matrix. Returns false if the matrix is degenerate (zero scale).
	// Rotation is returned as XYZ Euler angles in radians (matching TransformComponent::Rotation).
	bool DecomposeTransform(const glm::mat4& transform, glm::vec3& outTranslation, glm::vec3& outRotation, glm::vec3& outScale);
	bool DecomposeTransform(const glm::mat4& transform, glm::vec3& outTranslation, glm::quat& outRotation, glm::vec3& outScale);

	glm::mat4 ComposeTransform(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale);

	bool NearlyEqual(float a, float b, float epsilon = 1e-4f);
	bool NearlyEqual(const glm::vec3& a, const glm::vec3& b, float epsilon = 1e-4f);
	bool NearlyEqual(const glm::mat4& a, const glm::mat4& b, float epsilon = 1e-4f);

}
