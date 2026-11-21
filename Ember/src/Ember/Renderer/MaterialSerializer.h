#pragma once

#include "Ember/Renderer/MaterialParameters.h"

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <optional>

namespace Ember {

	// YAML conversion for MaterialParameters. Used by MeshRenderer material overrides and .ematerial asset files.
	namespace MaterialSerializer {

		YAML::Node ToYAML(const MaterialParameters& material);
		// Patch semantics: only keys present are applied. Returns false if any present value is malformed.
		// BaseColor accepts RGB (alpha kept) or RGBA.
		bool FromYAML(const YAML::Node& node, MaterialParameters& material);

		bool Save(const MaterialParameters& material, const std::filesystem::path& path);
		std::optional<MaterialParameters> Load(const std::filesystem::path& path);

		const char* AlphaModeToString(AlphaMode mode);
	}

}
