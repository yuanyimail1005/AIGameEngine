#pragma once

#include "Ember/Core/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Ember {

	enum class TextAlignment : uint8_t
	{
		Left = 0, Center, Right
	};

	// One glyph quad of laid-out text. Positions are in pixels relative to the top-left corner of the text block
	// (y down); texture coordinates address the font's SDF atlas.
	struct TextQuad
	{
		glm::vec2 Min{ 0.0f };
		glm::vec2 Max{ 0.0f };
		glm::vec2 UVMin{ 0.0f };
		glm::vec2 UVMax{ 0.0f };
	};

	struct TextLayout
	{
		std::vector<TextQuad> Quads;
		glm::vec2 Size{ 0.0f }; // width of the widest line, height of all lines (pixels)
		uint32_t LineCount = 0;
	};

	// A TrueType font baked into a signed distance field atlas (single-channel, 8-bit) covering printable ASCII and
	// Latin-1. Distance fields keep glyph edges sharp at any size and allow outlines. CPU-only: renderers upload
	// GetAtlasPixels() into an R8 texture.
	//
	// Font sizes are pixel heights (ascender to descender), as in stb_truetype and most UI toolkits.
	// Font files are trusted project assets: stb_truetype rejects files that are not fonts, but does not fully
	// validate the tables of a damaged font file.
	class Font
	{
	public:
		~Font();

		Font(const Font&) = delete;
		Font& operator=(const Font&) = delete;

		// Returns nullptr (and logs) if the data is not a usable TrueType/OpenType font.
		static Ref<Font> LoadFromMemory(std::vector<uint8_t> data, const std::string& name);
		static Ref<Font> LoadFromFile(const std::filesystem::path& path);
		// The built-in font (Roboto Medium), embedded in the engine. Baked once per process.
		static Ref<Font> GetDefault();

		// Lays out UTF-8 text at the given pixel size. '\n' starts a new line; characters without a glyph are drawn
		// as '?'. Each line is aligned horizontally within the width of the widest line.
		TextLayout Layout(std::string_view text, float size, TextAlignment alignment = TextAlignment::Left, float lineSpacing = 1.0f) const;

		const std::string& GetName() const { return m_Name; }
		uint32_t GetAtlasWidth() const { return m_AtlasWidth; }
		uint32_t GetAtlasHeight() const { return m_AtlasHeight; }
		std::span<const uint8_t> GetAtlasPixels() const { return m_AtlasPixels; }
		bool HasGlyph(uint32_t codepoint) const { return m_Glyphs.contains(codepoint); }

		// Pixel size the atlas was baked at, and the distance (in atlas pixels) encoded by the full 0..1 range of the
		// distance field around the 0.5 edge value: value = 0.5 + distance / (2 * GetDistanceRange()).
		static constexpr float c_BakeSize = 48.0f;
		static constexpr int c_DistanceRange = 8;

	private:
		struct Glyph
		{
			int Index = 0;              // glyph index inside the font file
			glm::vec2 PlaneMin{ 0.0f }; // quad relative to the pen position on the baseline, in bake-size pixels
			glm::vec2 PlaneMax{ 0.0f };
			glm::vec2 UVMin{ 0.0f };
			glm::vec2 UVMax{ 0.0f };
			float Advance = 0.0f;       // bake-size pixels
			bool Visible = false;       // false for whitespace
		};

		struct Impl;

		Font();
		bool Bake();
		const Glyph* FindGlyph(uint32_t codepoint) const;

	private:
		Scope<Impl> m_Impl;
		std::string m_Name;
		std::unordered_map<uint32_t, Glyph> m_Glyphs;
		float m_Ascent = 0.0f;  // bake-size pixels above the baseline
		float m_Descent = 0.0f; // bake-size pixels below the baseline (positive)
		float m_LineGap = 0.0f;
		uint32_t m_AtlasWidth = 0;
		uint32_t m_AtlasHeight = 0;
		std::vector<uint8_t> m_AtlasPixels;
	};

	// Decodes UTF-8 into code points. Invalid sequences decode to U+FFFD.
	std::vector<uint32_t> DecodeUTF8(std::string_view text);

}
