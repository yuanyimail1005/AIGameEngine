#include "Ember/Math/Math.h"

#include <glm/gtx/quaternion.hpp>

#include <cmath>

namespace Ember::Math {

	bool DecomposeTransform(const glm::mat4& transform, glm::vec3& outTranslation, glm::quat& outRotation, glm::vec3& outScale)
	{
		outTranslation = glm::vec3(transform[3]);

		glm::vec3 columns[3] = { glm::vec3(transform[0]), glm::vec3(transform[1]), glm::vec3(transform[2]) };
		outScale = { glm::length(columns[0]), glm::length(columns[1]), glm::length(columns[2]) };
		if (outScale.x < 1e-8f || outScale.y < 1e-8f || outScale.z < 1e-8f)
		{
			outRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			return false;
		}

		for (int i = 0; i < 3; i++)
			columns[i] /= outScale[i];

		// Negative determinant = mirrored: flip one axis so the rotation stays a proper rotation.
		if (glm::dot(columns[0], glm::cross(columns[1], columns[2])) < 0.0f)
		{
			outScale.x = -outScale.x;
			columns[0] = -columns[0];
		}

		const glm::mat3 rotationMatrix(columns[0], columns[1], columns[2]);
		outRotation = glm::normalize(glm::quat_cast(rotationMatrix));
		return true;
	}

	bool DecomposeTransform(const glm::mat4& transform, glm::vec3& outTranslation, glm::vec3& outRotation, glm::vec3& outScale)
	{
		glm::quat rotation;
		const bool ok = DecomposeTransform(transform, outTranslation, rotation, outScale);
		outRotation = glm::eulerAngles(rotation);
		return ok;
	}

	glm::mat4 ComposeTransform(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale)
	{
		return glm::translate(glm::mat4(1.0f), translation) * glm::toMat4(rotation) * glm::scale(glm::mat4(1.0f), scale);
	}

	bool NearlyEqual(float a, float b, float epsilon)
	{
		return std::fabs(a - b) <= epsilon;
	}

	bool NearlyEqual(const glm::vec3& a, const glm::vec3& b, float epsilon)
	{
		return NearlyEqual(a.x, b.x, epsilon) && NearlyEqual(a.y, b.y, epsilon) && NearlyEqual(a.z, b.z, epsilon);
	}

	bool NearlyEqual(const glm::mat4& a, const glm::mat4& b, float epsilon)
	{
		for (int c = 0; c < 4; c++)
		{
			for (int r = 0; r < 4; r++)
			{
				if (!NearlyEqual(a[c][r], b[c][r], epsilon))
					return false;
			}
		}
		return true;
	}

}
