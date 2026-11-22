#include "Ember/Renderer/Font.h"

#include "Ember/Core/FileSystem.h"
#include "Ember/Core/Log.h"

#include <EmberResources.h>
#include <stb_truetype.h>

#include <algorithm>
#include <bit>

namespace Ember {

	namespace {

		constexpr uint32_t c_ReplacementCharacter = 0xFFFD;
		constexpr uint32_t c_FallbackCharacter = '?';
		constexpr uint32_t c_AtlasWidth = 1024;
		constexpr uint32_t c_GlyphSpacing = 1;
		constexpr unsigned char c_OnEdgeValue = 128;

		struct CodepointRange
		{
			uint32_t First;
			uint32_t Last;
		};
		// Printable ASCII and Latin-1 Supplement.
		constexpr CodepointRange c_Charset[] = { { 0x20, 0x7E }, { 0xA0, 0xFF } };

	}

	struct Font::Impl
	{
		std::vector<uint8_t> Data; // must outlive Info
		stbtt_fontinfo Info{};
		float Scale = 1.0f;        // font units -> bake-size pixels
	};

	Font::Font()
		: m_Impl(CreateScope<Impl>())
	{
	}

	Font::~Font() = default;

	Ref<Font> Font::LoadFromMemory(std::vector<uint8_t> data, const std::string& name)
	{
		// The smallest valid font starts with a 12-byte table directory header; stb_truetype reads it unchecked.
		constexpr size_t minimumSize = 12;
		if (data.size() < minimumSize)
		{
			EMBER_CORE_ERROR("Font '{}': not a TrueType/OpenType font", name);
			return nullptr;
		}
		const int offset = stbtt_GetFontOffsetForIndex(data.data(), 0);
		if (offset < 0 || static_cast<size_t>(offset) >= data.size())
		{
			EMBER_CORE_ERROR("Font '{}': not a TrueType/OpenType font", name);
			return nullptr;
		}

		Ref<Font> font(new Font());
		font->m_Name = name;
		font->m_Impl->Data = std::move(data);
		if (!stbtt_InitFont(&font->m_Impl->Info, font->m_Impl->Data.data(), offset))
		{
			EMBER_CORE_ERROR("Font '{}': not a TrueType/OpenType font", name);
			return nullptr;
		}
		if (!font->Bake())
		{
			EMBER_CORE_ERROR("Font '{}': contains no usable glyphs", name);
			return nullptr;
		}
		return font;
	}

	Ref<Font> Font::LoadFromFile(const std::filesystem::path& path)
	{
		std::optional<Buffer> bytes = FileSystem::ReadBinary(path);
		if (!bytes)
		{
			EMBER_CORE_ERROR("Font: cannot read '{}'", path.string());
			return nullptr;
		}
		return LoadFromMemory(std::vector<uint8_t>(bytes->Data(), bytes->Data() + bytes->Size()), path.filename().string());
	}

	Ref<Font> Font::GetDefault()
	{
		static const Ref<Font> s_Default = []
		{
			const std::span<const uint8_t> data = EmbeddedResources::Find("DefaultFont");
			return LoadFromMemory(std::vector<uint8_t>(data.begin(), data.end()), "Default");
		}();
		return s_Default;
	}

	bool Font::Bake()
	{
		stbtt_fontinfo& info = m_Impl->Info;
		const float scale = stbtt_ScaleForPixelHeight(&info, c_BakeSize);
		m_Impl->Scale = scale;

		int ascent = 0, descent = 0, lineGap = 0;
		stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
		m_Ascent = static_cast<float>(ascent) * scale;
		m_Descent = static_cast<float>(-descent) * scale;
		m_LineGap = static_cast<float>(lineGap) * scale;

		struct Bitmap
		{
			uint32_t Codepoint = 0;
			unsigned char* Pixels = nullptr;
			int Width = 0, Height = 0;
			uint32_t X = 0, Y = 0;
		};
		std::vector<Bitmap> bitmaps;

		const float distanceScale = static_cast<float>(c_OnEdgeValue) / static_cast<float>(c_DistanceRange);
		for (const CodepointRange& range : c_Charset)
		{
			for (uint32_t codepoint = range.First; codepoint <= range.Last; codepoint++)
			{
				const int index = stbtt_FindGlyphIndex(&info, static_cast<int>(codepoint));
				if (index == 0 && codepoint != ' ')
					continue; // not in this font

				Glyph glyph;
				glyph.Index = index;
				int advance = 0, leftBearing = 0;
				stbtt_GetGlyphHMetrics(&info, index, &advance, &leftBearing);
				glyph.Advance = static_cast<float>(advance) * scale;

				Bitmap bitmap;
				bitmap.Codepoint = codepoint;
				int xOffset = 0, yOffset = 0;
				bitmap.Pixels = stbtt_GetGlyphSDF(&info, scale, index, c_DistanceRange, c_OnEdgeValue, distanceScale,
					&bitmap.Width, &bitmap.Height, &xOffset, &yOffset);
				if (bitmap.Pixels && bitmap.Width > 0 && bitmap.Height > 0)
				{
					glyph.Visible = true;
					glyph.PlaneMin = { static_cast<float>(xOffset), static_cast<float>(yOffset) };
					glyph.PlaneMax = glyph.PlaneMin + glm::vec2(static_cast<float>(bitmap.Width), static_cast<float>(bitmap.Height));
					bitmaps.push_back(bitmap);
				}
				else if (bitmap.Pixels)
				{
					stbtt_FreeSDF(bitmap.Pixels, nullptr);
				}
				m_Glyphs[codepoint] = glyph;
			}
		}
		if (bitmaps.empty())
			return false;

		// Shelf packing, tallest glyphs first.
		std::vector<Bitmap*> order;
		for (Bitmap& bitmap : bitmaps)
			order.push_back(&bitmap);
		std::stable_sort(order.begin(), order.end(), [](const Bitmap* a, const Bitmap* b) { return a->Height > b->Height; });
		uint32_t x = c_GlyphSpacing, y = c_GlyphSpacing, shelfHeight = 0;
		for (Bitmap* bitmap : order)
		{
			const uint32_t width = static_cast<uint32_t>(bitmap->Width);
			const uint32_t height = static_cast<uint32_t>(bitmap->Height);
			if (x + width + c_GlyphSpacing > c_AtlasWidth)
			{
				x = c_GlyphSpacing;
				y += shelfHeight + c_GlyphSpacing;
				shelfHeight = 0;
			}
			bitmap->X = x;
			bitmap->Y = y;
			x += width + c_GlyphSpacing;
			shelfHeight = std::max(shelfHeight, height);
		}
		m_AtlasWidth = c_AtlasWidth;
		m_AtlasHeight = std::bit_ceil(y + shelfHeight + c_GlyphSpacing);
		m_AtlasPixels.assign(size_t(m_AtlasWidth) * m_AtlasHeight, 0);

		const glm::vec2 atlasSize(static_cast<float>(m_AtlasWidth), static_cast<float>(m_AtlasHeight));
		for (const Bitmap& bitmap : bitmaps)
		{
			for (int row = 0; row < bitmap.Height; row++)
			{
				std::copy_n(bitmap.Pixels + size_t(row) * size_t(bitmap.Width), bitmap.Width,
					m_AtlasPixels.begin() + static_cast<std::ptrdiff_t>((size_t(bitmap.Y) + size_t(row)) * m_AtlasWidth + bitmap.X));
			}
			Glyph& glyph = m_Glyphs[bitmap.Codepoint];
			glyph.UVMin = glm::vec2(static_cast<float>(bitmap.X), static_cast<float>(bitmap.Y)) / atlasSize;
			glyph.UVMax = glm::vec2(static_cast<float>(bitmap.X + static_cast<uint32_t>(bitmap.Width)),
				static_cast<float>(bitmap.Y + static_cast<uint32_t>(bitmap.Height))) / atlasSize;
			stbtt_FreeSDF(bitmap.Pixels, nullptr);
		}
		return true;
	}

	const Font::Glyph* Font::FindGlyph(uint32_t codepoint) const
	{
		if (auto it = m_Glyphs.find(codepoint); it != m_Glyphs.end())
			return &it->second;
		if (auto it = m_Glyphs.find(c_FallbackCharacter); it != m_Glyphs.end())
			return &it->second;
		return nullptr;
	}

	TextLayout Font::Layout(std::string_view text, float size, TextAlignment alignment, float lineSpacing) const
	{
		TextLayout layout;
		if (text.empty() || !(size > 0.0f))
			return layout;

		const float k = size / c_BakeSize;
		const float lineAdvance = (m_Ascent + m_Descent + m_LineGap) * k * std::max(lineSpacing, 0.0f);
		const Glyph* space = FindGlyph(' ');
		const float spaceAdvance = space ? space->Advance : c_BakeSize * 0.25f;

		struct Line
		{
			size_t FirstQuad = 0;
			float Width = 0.0f;
		};
		std::vector<Line> lines(1);
		float penX = 0.0f;
		int previousIndex = -1;

		for (uint32_t codepoint : DecodeUTF8(text))
		{
			if (codepoint == '\n')
			{
				lines.back().Width = penX * k;
				lines.push_back(Line{ layout.Quads.size(), 0.0f });
				penX = 0.0f;
				previousIndex = -1;
				continue;
			}
			if (codepoint == '\t')
			{
				penX += spaceAdvance * 4.0f;
				previousIndex = -1;
				continue;
			}
			if (codepoint < 0x20 || codepoint == 0x7F)
				continue;

			const Glyph* glyph = FindGlyph(codepoint);
			if (!glyph)
				continue;
			if (previousIndex >= 0)
				penX += static_cast<float>(stbtt_GetGlyphKernAdvance(&m_Impl->Info, previousIndex, glyph->Index)) * m_Impl->Scale;

			if (glyph->Visible)
			{
				const float baseline = m_Ascent * k + static_cast<float>(lines.size() - 1) * lineAdvance;
				TextQuad quad;
				quad.Min = glm::vec2(penX * k + glyph->PlaneMin.x * k, baseline + glyph->PlaneMin.y * k);
				quad.Max = glm::vec2(penX * k + glyph->PlaneMax.x * k, baseline + glyph->PlaneMax.y * k);
				quad.UVMin = glyph->UVMin;
				quad.UVMax = glyph->UVMax;
				layout.Quads.push_back(quad);
			}
			penX += glyph->Advance;
			previousIndex = glyph->Index;
		}
		lines.back().Width = penX * k;

		for (const Line& line : lines)
			layout.Size.x = std::max(layout.Size.x, line.Width);
		layout.LineCount = static_cast<uint32_t>(lines.size());
		layout.Size.y = (m_Ascent + m_Descent) * k + static_cast<float>(lines.size() - 1) * lineAdvance;

		if (alignment != TextAlignment::Left)
		{
			for (size_t i = 0; i < lines.size(); i++)
			{
				const size_t end = i + 1 < lines.size() ? lines[i + 1].FirstQuad : layout.Quads.size();
				const float shift = (layout.Size.x - lines[i].Width) * (alignment == TextAlignment::Center ? 0.5f : 1.0f);
				for (size_t q = lines[i].FirstQuad; q < end; q++)
				{
					layout.Quads[q].Min.x += shift;
					layout.Quads[q].Max.x += shift;
				}
			}
		}
		return layout;
	}

	std::vector<uint32_t> DecodeUTF8(std::string_view text)
	{
		std::vector<uint32_t> codepoints;
		codepoints.reserve(text.size());
		size_t i = 0;
		while (i < text.size())
		{
			const uint8_t lead = static_cast<uint8_t>(text[i]);
			uint32_t codepoint = 0;
			size_t length = 0;
			uint32_t minimum = 0;
			if (lead < 0x80)
			{
				codepoints.push_back(lead);
				i++;
				continue;
			}
			if ((lead & 0xE0) == 0xC0) { codepoint = lead & 0x1Fu; length = 2; minimum = 0x80; }
			else if ((lead & 0xF0) == 0xE0) { codepoint = lead & 0x0Fu; length = 3; minimum = 0x800; }
			else if ((lead & 0xF8) == 0xF0) { codepoint = lead & 0x07u; length = 4; minimum = 0x10000; }
			else
			{
				codepoints.push_back(c_ReplacementCharacter);
				i++;
				continue;
			}

			size_t consumed = 1;
			bool valid = i + length <= text.size();
			while (valid && consumed < length)
			{
				const uint8_t next = static_cast<uint8_t>(text[i + consumed]);
				if ((next & 0xC0) != 0x80)
				{
					valid = false;
					break;
				}
				codepoint = (codepoint << 6) | (next & 0x3Fu);
				consumed++;
			}
			if (valid && (codepoint < minimum || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)))
				valid = false;
			codepoints.push_back(valid ? codepoint : c_ReplacementCharacter);
			i += valid ? length : consumed;
		}
		return codepoints;
	}

}
