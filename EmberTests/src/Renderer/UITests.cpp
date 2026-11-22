#include <doctest/doctest.h>

#include "GpuTestContext.h"
#include "TestUtils.h"

#include "Ember/Renderer/Font.h"
#include "Ember/Renderer/RenderAssets.h"
#include "Ember/Renderer/SceneRenderer.h"
#include "Ember/Renderer/UIRenderer.h"
#include "Ember/Scene/Entity.h"
#include "Ember/Scene/Scene.h"

#include <cstdlib>
#include <functional>

using namespace Ember;

namespace {

	// 16:9 viewport: one reference pixel is 0.25 screen pixels (180 / 720).
	constexpr uint32_t c_Width = 320;
	constexpr uint32_t c_Height = 180;

	struct UIFixture
	{
		Scope<RenderAssets> Assets;
		Scope<SceneRenderer> Renderer;
		Ref<Scene> SceneRef = CreateRef<Scene>("UITest");
		std::filesystem::path AssetDirectory;

		UIFixture(Ember::GraphicsDevice* gpu, const std::string& name)
			: AssetDirectory(EmberTest::MakeTempDirectory(name))
		{
			Assets = CreateScope<RenderAssets>(gpu->GetDevice(), RenderSettings::LowQuality());
			Assets->SetAssetDirectory(AssetDirectory);
			Renderer = CreateScope<SceneRenderer>(*Assets);
			Renderer->SetViewportSize(c_Width, c_Height);
			SceneRef->SetAssetDirectory(AssetDirectory);

			// Camera looking at an unlit, sky-less void: the background is the renderer's flat clear color.
			Entity camera = SceneRef->CreateEntity("Camera");
			camera.AddComponent<CameraComponent>();
			Entity sky = SceneRef->CreateEntity("Sky");
			auto& skyLight = sky.AddComponent<SkyLightComponent>();
			skyLight.Intensity = 0.0f;
			skyLight.DrawSkybox = false;
			SceneRef->OnViewportResize(c_Width, c_Height);
		}

		UIPanelComponent& AddPanel(const std::string& name, const glm::vec2& offset, const glm::vec2& size, const glm::vec4& color)
		{
			Entity entity = SceneRef->CreateEntity(name);
			auto& panel = entity.AddComponent<UIPanelComponent>();
			panel.Anchor = glm::vec2(0.0f);
			panel.Pivot = glm::vec2(0.0f);
			panel.Offset = offset;
			panel.Size = size;
			panel.Color = color;
			return panel;
		}

		Image Render()
		{
			std::optional<RenderCamera> camera = SceneRenderer::GetPrimaryCamera(*SceneRef);
			REQUIRE(camera.has_value());
			Renderer->Render(*SceneRef, *camera);
			std::optional<Image> image = Renderer->CaptureImage();
			REQUIRE(image.has_value());
			if (const char* dir = std::getenv("EMBER_TEST_SAVE_IMAGES"))
			{
				static int counter = 0;
				image->SavePNG(std::filesystem::path(dir) / ("UI_" + std::to_string(counter++) + ".png"));
			}
			return *image;
		}
	};

	glm::ivec3 Pixel(const Image& image, uint32_t x, uint32_t y)
	{
		const uint8_t* p = image.GetPixel(x, y);
		return glm::ivec3(p[0], p[1], p[2]);
	}

	uint32_t CountPixels(const Image& image, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
		const std::function<bool(const glm::ivec3&)>& predicate)
	{
		uint32_t count = 0;
		for (uint32_t y = y0; y < y1; y++)
		{
			for (uint32_t x = x0; x < x1; x++)
			{
				if (predicate(Pixel(image, x, y)))
					count++;
			}
		}
		return count;
	}

	bool IsBright(const glm::ivec3& c) { return c.r > 200 && c.g > 200 && c.b > 200; }

}

TEST_SUITE("UI")
{
	TEST_CASE("Font: default font bakes printable ASCII and Latin-1 into a distance field atlas")
	{
		Ref<Font> font = Font::GetDefault();
		REQUIRE(font != nullptr);
		CHECK(Font::GetDefault() == font); // baked once
		for (uint32_t codepoint : { uint32_t('A'), uint32_t('z'), uint32_t('0'), uint32_t('?'), uint32_t(' '), 0xE9u, 0xFCu })
		{
			CAPTURE(codepoint);
			CHECK(font->HasGlyph(codepoint));
		}
		CHECK_FALSE(font->HasGlyph(0x4E2D));

		CHECK(font->GetAtlasWidth() == 1024);
		CHECK(font->GetAtlasHeight() >= 64);
		CHECK((font->GetAtlasHeight() & (font->GetAtlasHeight() - 1)) == 0);
		REQUIRE(font->GetAtlasPixels().size() == size_t(font->GetAtlasWidth()) * font->GetAtlasHeight());
		// A distance field has inside (> edge), outside (< edge) and transition values.
		size_t inside = 0, outside = 0, edge = 0;
		for (uint8_t value : font->GetAtlasPixels())
		{
			if (value > 160)
				inside++;
			else if (value < 50)
				outside++;
			else if (value > 110 && value < 146)
				edge++;
		}
		CHECK(inside > 500);
		CHECK(outside > 1000);
		CHECK(edge > 1000);
	}

	TEST_CASE("Font: layout measures, breaks lines, aligns and substitutes missing glyphs")
	{
		Ref<Font> font = Font::GetDefault();
		REQUIRE(font != nullptr);

		CHECK(font->Layout("", 32.0f).Quads.empty());
		CHECK(font->Layout("", 32.0f).LineCount == 0);
		CHECK(font->Layout("Hello", 0.0f).Quads.empty());

		const TextLayout a = font->Layout("A", 32.0f);
		REQUIRE(a.Quads.size() == 1);
		CHECK(a.LineCount == 1);
		CHECK(a.Size.y == doctest::Approx(32.0f).epsilon(0.02));
		CHECK(a.Size.x > 10.0f);
		CHECK(a.Size.x < 32.0f);
		// Glyph quads include the distance field padding but stay near the text block.
		CHECK(a.Quads[0].Min.x < a.Quads[0].Max.x);
		CHECK(a.Quads[0].Min.y < a.Quads[0].Max.y);
		CHECK(a.Quads[0].Min.y > -10.0f);
		CHECK(a.Quads[0].Max.y < 32.0f + 10.0f);
		CHECK(a.Quads[0].UVMin.x < a.Quads[0].UVMax.x);

		// Width scales with size and grows with text; whitespace has no quads but advances.
		const TextLayout big = font->Layout("A", 64.0f);
		CHECK(big.Size.x == doctest::Approx(a.Size.x * 2.0f).epsilon(0.01));
		const TextLayout spaced = font->Layout("A A", 32.0f);
		CHECK(spaced.Quads.size() == 2);
		CHECK(spaced.Size.x > a.Size.x * 2.0f);
		const TextLayout tab = font->Layout("A\tA", 32.0f);
		CHECK(tab.Size.x > spaced.Size.x);

		// Line breaks and line spacing.
		const TextLayout twoLines = font->Layout("AAAA\nA", 32.0f);
		CHECK(twoLines.LineCount == 2);
		CHECK(twoLines.Quads.size() == 5);
		CHECK(twoLines.Size.x == doctest::Approx(font->Layout("AAAA", 32.0f).Size.x));
		CHECK(twoLines.Size.y > 60.0f);
		CHECK(font->Layout("A\nA", 32.0f, TextAlignment::Left, 2.0f).Size.y > font->Layout("A\nA", 32.0f).Size.y + 30.0f);
		CHECK(font->Layout("A\n", 32.0f).LineCount == 2);

		// Alignment moves the short second line; the long first line stays put.
		const TextLayout left = font->Layout("AAAA\nA", 32.0f, TextAlignment::Left);
		const TextLayout center = font->Layout("AAAA\nA", 32.0f, TextAlignment::Center);
		const TextLayout right = font->Layout("AAAA\nA", 32.0f, TextAlignment::Right);
		CHECK(center.Quads[0].Min.x == doctest::Approx(left.Quads[0].Min.x));
		CHECK(right.Quads[0].Min.x == doctest::Approx(left.Quads[0].Min.x));
		const float shortWidth = font->Layout("A", 32.0f).Size.x;
		CHECK(center.Quads[4].Min.x == doctest::Approx(left.Quads[4].Min.x + (left.Size.x - shortWidth) * 0.5f));
		CHECK(right.Quads[4].Min.x == doctest::Approx(left.Quads[4].Min.x + (left.Size.x - shortWidth)));

		// Characters outside the atlas render as '?'; control characters are skipped.
		const TextLayout missing = font->Layout("\xE4\xB8\xAD", 32.0f);
		const TextLayout question = font->Layout("?", 32.0f);
		REQUIRE(missing.Quads.size() == 1);
		CHECK(missing.Quads[0].UVMin == question.Quads[0].UVMin);
		CHECK(font->Layout("A\rB\x01", 32.0f).Quads.size() == 2);
		// Latin-1 characters have their own glyphs.
		CHECK(font->Layout("\xC3\xA9", 32.0f).Quads[0].UVMin != question.Quads[0].UVMin);
	}

	TEST_CASE("Font: UTF-8 decoding handles multi-byte and invalid sequences")
	{
		CHECK(DecodeUTF8("Ab") == std::vector<uint32_t>{ 'A', 'b' });
		CHECK(DecodeUTF8("\xC3\xA9") == std::vector<uint32_t>{ 0xE9 });
		CHECK(DecodeUTF8("\xE2\x82\xAC") == std::vector<uint32_t>{ 0x20AC });
		CHECK(DecodeUTF8("\xF0\x9F\x98\x80") == std::vector<uint32_t>{ 0x1F600 });
		CHECK(DecodeUTF8("\xFF" "A") == std::vector<uint32_t>{ 0xFFFD, 'A' });          // invalid lead byte
		CHECK(DecodeUTF8("\xC3" "A") == std::vector<uint32_t>{ 0xFFFD, 'A' });          // missing continuation
		CHECK(DecodeUTF8("\xE2\x82") == std::vector<uint32_t>{ 0xFFFD, 0xFFFD });        // truncated at the end
		CHECK(DecodeUTF8("\xC0\xAF") == std::vector<uint32_t>{ 0xFFFD });               // overlong encoding
		CHECK(DecodeUTF8("\xED\xA0\x80") == std::vector<uint32_t>{ 0xFFFD });           // UTF-16 surrogate
		CHECK(DecodeUTF8("").empty());
	}

	TEST_CASE("Font: invalid data and missing files fail cleanly")
	{
		CHECK(Font::LoadFromMemory({}, "Empty") == nullptr);
		CHECK(Font::LoadFromMemory({ 0, 1, 0 }, "Truncated") == nullptr);
		CHECK(Font::LoadFromMemory({ 'n', 'o', 't', ' ', 'a', ' ', 'f', 'o', 'n', 't' }, "Garbage") == nullptr);
		const std::filesystem::path directory = EmberTest::MakeTempDirectory("font_missing");
		CHECK(Font::LoadFromFile(directory / "Missing.ttf") == nullptr);
		EmberTest::WriteFile(directory / "Fake.ttf", "this is not a font file at all");
		CHECK(Font::LoadFromFile(directory / "Fake.ttf") == nullptr);
	}

	TEST_CASE("UIRenderer: anchor, pivot and offset place elements in reference pixels")
	{
		const glm::vec2 viewport(1280.0f, 720.0f);
		CHECK(UIRenderer::GetScale(viewport) == doctest::Approx(1.0f));
		CHECK(UIRenderer::GetScale(glm::vec2(640.0f, 360.0f)) == doctest::Approx(0.5f));

		// Top-left anchored, top-left pivot.
		UIRect rect = UIRenderer::ComputeRect({ 0.0f, 0.0f }, { 10.0f, 20.0f }, { 0.0f, 0.0f }, { 100.0f, 50.0f }, viewport);
		CHECK(rect.Min == glm::vec2(10.0f, 20.0f));
		CHECK(rect.Max == glm::vec2(110.0f, 70.0f));
		// Centered.
		rect = UIRenderer::ComputeRect({ 0.5f, 0.5f }, { 0.0f, 0.0f }, { 0.5f, 0.5f }, { 100.0f, 50.0f }, viewport);
		CHECK(rect.Min == glm::vec2(590.0f, 335.0f));
		CHECK(rect.Max == glm::vec2(690.0f, 385.0f));
		// Bottom-right corner, pulled in by a negative offset.
		rect = UIRenderer::ComputeRect({ 1.0f, 1.0f }, { -10.0f, -10.0f }, { 1.0f, 1.0f }, { 100.0f, 50.0f }, viewport);
		CHECK(rect.Max == glm::vec2(1270.0f, 710.0f));
		// Half resolution halves sizes and offsets but anchors stay proportional.
		rect = UIRenderer::ComputeRect({ 1.0f, 0.0f }, { -20.0f, 20.0f }, { 1.0f, 0.0f }, { 100.0f, 50.0f }, glm::vec2(640.0f, 360.0f));
		CHECK(rect.Min == glm::vec2(580.0f, 10.0f));
		CHECK(rect.Max == glm::vec2(630.0f, 35.0f));
	}

	TEST_CASE("UIRenderer: panels, text, outlines, sort order, visibility and textures")
	{
		EMBER_REQUIRE_GPU(gpu);
		UIFixture fixture(gpu, "ui_render");

		// Without UI the image is a flat, dark background.
		Image image = fixture.Render();
		CHECK(fixture.Renderer->GetStats().UIElements == 0);
		const glm::ivec3 background = Pixel(image, 160, 90);
		CHECK(background.r < 100);
		CHECK(Pixel(image, 0, 0) == background);
		auto IsBackground = [&](const glm::ivec3& c) { return c == background; };

		// Red panel at (10,10)-(110,60) screen pixels; green panel overlapping it on the right, drawn later.
		UIPanelComponent& red = fixture.AddPanel("Red", { 40.0f, 40.0f }, { 400.0f, 200.0f }, { 1.0f, 0.0f, 0.0f, 1.0f });
		UIPanelComponent& green = fixture.AddPanel("Green", { 280.0f, 40.0f }, { 400.0f, 200.0f }, { 0.0f, 1.0f, 0.0f, 1.0f });
		image = fixture.Render();
		CHECK(fixture.Renderer->GetStats().UIElements == 2);
		CHECK(Pixel(image, 40, 35) == glm::ivec3(255, 0, 0));
		CHECK(Pixel(image, 100, 35) == glm::ivec3(0, 255, 0)); // overlap: later entity on top
		CHECK(Pixel(image, 150, 35) == glm::ivec3(0, 255, 0));
		CHECK(IsBackground(Pixel(image, 5, 5)));
		CHECK(IsBackground(Pixel(image, 40, 65)));
		CHECK(Pixel(image, 10, 10) == glm::ivec3(255, 0, 0)); // exact edges
		CHECK(IsBackground(Pixel(image, 9, 10)));
		CHECK(IsBackground(Pixel(image, 10, 9)));

		// SortOrder overrides creation order.
		red.SortOrder = 1;
		image = fixture.Render();
		CHECK(Pixel(image, 100, 35) == glm::ivec3(255, 0, 0));

		// Translucent panels blend; invisible and fully transparent panels are skipped.
		green.Color = { 0.0f, 1.0f, 0.0f, 0.5f };
		red.Visible = false;
		image = fixture.Render();
		CHECK(fixture.Renderer->GetStats().UIElements == 1);
		const glm::ivec3 blended = Pixel(image, 150, 35);
		CHECK(blended.g > 100);
		CHECK(blended.g < 160);
		CHECK(IsBackground(Pixel(image, 40, 35)));
		green.Color.a = 0.0f;
		image = fixture.Render();
		CHECK(fixture.Renderer->GetStats().UIElements == 0);

		// Textured panel: the texture (left blue, right white) is multiplied by the color.
		Image texture;
		texture.Width = 2;
		texture.Height = 1;
		texture.Pixels = { 0, 0, 255, 255, 255, 255, 255, 255 };
		REQUIRE(texture.SavePNG(fixture.AssetDirectory / "Textures/Icon.png"));
		UIPanelComponent& icon = fixture.AddPanel("Icon", { 40.0f, 400.0f }, { 400.0f, 200.0f }, glm::vec4(1.0f));
		icon.Texture = "Textures/Icon.png";
		image = fixture.Render();
		CHECK(Pixel(image, 14, 125) == glm::ivec3(0, 0, 255));
		CHECK(Pixel(image, 106, 125) == glm::ivec3(255, 255, 255));
		icon.Visible = false;

		// White text in the right half of the screen; nothing outside its block.
		Entity label = fixture.SceneRef->CreateEntity("Label");
		auto& text = label.AddComponent<UITextComponent>();
		text.Text = "HI";
		text.FontSize = 240.0f; // 60 screen pixels
		text.Anchor = { 0.75f, 0.5f };
		image = fixture.Render();
		CHECK(fixture.Renderer->GetStats().UIElements == 1);
		const uint32_t lit = CountPixels(image, 160, 0, 320, 180, IsBright);
		CHECK(lit > 300);
		CHECK(CountPixels(image, 0, 0, 160, 180, IsBright) == 0);
		CHECK(CountPixels(image, 160, 0, 320, 50, IsBright) == 0);
		CHECK(CountPixels(image, 160, 130, 320, 180, IsBright) == 0);
		// Antialiased edges: some partially covered pixels.
		CHECK(CountPixels(image, 160, 0, 320, 180, [](const glm::ivec3& c) { return c.r > 40 && c.r < 200; }) > 20);

		// The text block is centered on its anchor (240, 90).
		uint32_t minX = c_Width, maxX = 0;
		for (uint32_t y = 0; y < c_Height; y++)
		{
			for (uint32_t x = 160; x < c_Width; x++)
			{
				if (IsBright(Pixel(image, x, y)))
				{
					minX = std::min(minX, x);
					maxX = std::max(maxX, x);
				}
			}
		}
		CHECK(std::abs(static_cast<int>(minX + maxX) / 2 - 240) <= 4);

		// Text color and an outline: the outline adds yellow pixels around the red glyphs.
		text.Color = { 1.0f, 0.0f, 0.0f, 1.0f };
		image = fixture.Render();
		auto isYellow = [](const glm::ivec3& c) { return c.r > 200 && c.g > 200 && c.b < 60; };
		auto isRed = [](const glm::ivec3& c) { return c.r > 200 && c.g < 60 && c.b < 60; };
		CHECK(CountPixels(image, 160, 0, 320, 180, isRed) > 300);
		CHECK(CountPixels(image, 160, 0, 320, 180, isYellow) == 0);
		text.OutlineWidth = 12.0f; // 3 screen pixels
		text.OutlineColor = { 1.0f, 1.0f, 0.0f, 1.0f };
		image = fixture.Render();
		CHECK(CountPixels(image, 160, 0, 320, 180, isYellow) > 150);
		CHECK(CountPixels(image, 160, 0, 320, 180, isRed) > 150);

		// A missing font falls back to the built-in one; empty or hidden text draws nothing.
		text.Font = "Fonts/Missing.ttf";
		text.OutlineWidth = 0.0f;
		image = fixture.Render();
		CHECK(CountPixels(image, 160, 0, 320, 180, isRed) > 300);
		text.Text.clear();
		image = fixture.Render();
		CHECK(fixture.Renderer->GetStats().UIElements == 0);
		CHECK(CountPixels(image, 0, 0, 320, 180, [&](const glm::ivec3& c) { return !IsBackground(c); }) == 0);

		CHECK(gpu->GetErrorCount() == 0);
	}

	TEST_CASE("UIRenderer: UI scales with the viewport height")
	{
		EMBER_REQUIRE_GPU(gpu);
		UIFixture fixture(gpu, "ui_scale");
		const glm::ivec3 background = Pixel(fixture.Render(), 0, 0);
		// Reference rect (0,0)-(720,360) is the top half of the screen at any 16:9 resolution.
		fixture.AddPanel("Half", { 0.0f, 0.0f }, { 1280.0f, 360.0f }, { 1.0f, 1.0f, 1.0f, 1.0f });
		for (const glm::uvec2 size : { glm::uvec2(320, 180), glm::uvec2(160, 90) })
		{
			CAPTURE(size.x);
			fixture.Renderer->SetViewportSize(size.x, size.y);
			fixture.SceneRef->OnViewportResize(size.x, size.y);
			const Image image = fixture.Render();
			REQUIRE(image.Width == size.x);
			CHECK(IsBright(Pixel(image, size.x - 1, size.y / 2 - 1)));
			CHECK(Pixel(image, 0, size.y / 2) == background);
		}
		CHECK(gpu->GetErrorCount() == 0);
	}
}
