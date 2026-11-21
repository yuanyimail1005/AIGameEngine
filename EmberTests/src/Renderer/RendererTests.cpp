#include <doctest/doctest.h>

#include "GpuTestContext.h"
#include "TestUtils.h"

#include "Ember/Renderer/MaterialSerializer.h"
#include "Ember/Renderer/Mesh.h"
#include "Ember/Renderer/RenderAssets.h"
#include "Ember/Renderer/SceneRenderer.h"
#include "Ember/Scene/Entity.h"
#include "Ember/Scene/Scene.h"

#include <glm/gtc/matrix_transform.hpp>
#include <stb_image_write.h>

#include <cstdlib>

using namespace Ember;

namespace {

	constexpr uint32_t c_Size = 128;

	struct RenderFixture
	{
		Ember::GraphicsDevice* Gpu;
		Scope<RenderAssets> Assets;
		Scope<SceneRenderer> Renderer;
		Ref<Scene> SceneRef = CreateRef<Scene>("RenderTest");
		std::filesystem::path AssetDirectory;

		explicit RenderFixture(Ember::GraphicsDevice* gpu, const std::string& name)
			: Gpu(gpu), AssetDirectory(EmberTest::MakeTempDirectory(name))
		{
			Assets = CreateScope<RenderAssets>(gpu->GetDevice(), RenderSettings::LowQuality());
			Assets->SetAssetDirectory(AssetDirectory);
			Renderer = CreateScope<SceneRenderer>(*Assets);
			Renderer->SetViewportSize(c_Size, c_Size);
			SceneRef->SetAssetDirectory(AssetDirectory);
			SceneRef->OnViewportResize(c_Size, c_Size);
		}

		Entity AddCamera(const glm::vec3& position = { 0.0f, 0.0f, 5.0f }, const glm::vec3& rotation = glm::vec3(0.0f))
		{
			Entity camera = SceneRef->CreateEntity("Camera");
			camera.AddComponent<CameraComponent>().Camera.SetPerspective(glm::radians(45.0f), 0.1f, 100.0f);
			camera.GetComponent<TransformComponent>().Translation = position;
			camera.GetComponent<TransformComponent>().Rotation = rotation;
			SceneRef->OnViewportResize(c_Size + 1, c_Size); // force aspect update
			SceneRef->OnViewportResize(c_Size, c_Size);
			return camera;
		}

		Entity AddMesh(const std::string& name, const std::string& mesh, const glm::vec3& position, const glm::vec4& color)
		{
			Entity entity = SceneRef->CreateEntity(name);
			auto& renderer = entity.AddComponent<MeshRendererComponent>();
			renderer.Mesh = mesh;
			renderer.OverrideMaterial = true;
			renderer.MaterialOverride.BaseColor = color;
			renderer.MaterialOverride.Roughness = 0.8f;
			entity.GetComponent<TransformComponent>().Translation = position;
			return entity;
		}

		Entity AddSun(const glm::vec3& rotation, float intensity = 3.0f, bool shadows = true)
		{
			Entity sun = SceneRef->CreateEntity("Sun");
			auto& light = sun.AddComponent<DirectionalLightComponent>();
			light.Intensity = intensity;
			light.CastShadows = shadows;
			sun.GetComponent<TransformComponent>().Rotation = rotation;
			return sun;
		}

		void SetSky(float intensity, bool drawSkybox = true, const std::string& hdr = {})
		{
			Entity sky = SceneRef->FindEntityByName("Sky");
			if (!sky)
				sky = SceneRef->CreateEntity("Sky");
			auto& component = sky.HasComponent<SkyLightComponent>() ? sky.GetComponent<SkyLightComponent>() : sky.AddComponent<SkyLightComponent>();
			component.Intensity = intensity;
			component.DrawSkybox = drawSkybox;
			component.EnvironmentMap = hdr;
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
				image->SavePNG(std::filesystem::path(dir) / (SceneRef->GetName() + "_" + std::to_string(counter++) + ".png"));
			}
			return *image;
		}
	};

	glm::vec3 PixelColor(const Image& image, uint32_t x, uint32_t y)
	{
		const uint8_t* p = image.GetPixel(x, y);
		return glm::vec3(p[0], p[1], p[2]) / 255.0f;
	}

	float Luminance(const glm::vec3& c)
	{
		return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
	}

}

TEST_SUITE("Renderer")
{
	TEST_CASE("MeshFactory: all built-in primitives are valid with unit-sized bounds")
	{
		for (const char* name : { "Cube", "Plane", "Sphere", "Cylinder", "Capsule" })
		{
			CAPTURE(name);
			MeshData data = MeshFactory::CreateByName(name);
			REQUIRE(data.IsValid());
			const AABB bounds = data.ComputeBounds();
			CHECK(bounds.Max.x == doctest::Approx(0.5f).epsilon(0.01));
			CHECK(bounds.Min.x == doctest::Approx(-0.5f).epsilon(0.01));
			for (const Vertex& v : data.Vertices)
			{
				CHECK(glm::length(v.Normal) == doctest::Approx(1.0f).epsilon(0.001));
				CHECK(glm::length(glm::vec3(v.Tangent)) == doctest::Approx(1.0f).epsilon(0.001));
				CHECK(std::abs(glm::dot(v.Normal, glm::vec3(v.Tangent))) < 0.01f);
			}
			// Triangles wind counter-clockwise when seen from outside: normals agree with the face normal.
			for (size_t i = 0; i + 2 < data.Indices.size(); i += 3)
			{
				const Vertex& a = data.Vertices[data.Indices[i]];
				const Vertex& b = data.Vertices[data.Indices[i + 1]];
				const Vertex& c = data.Vertices[data.Indices[i + 2]];
				const glm::vec3 face = glm::cross(b.Position - a.Position, c.Position - a.Position);
				if (glm::length(face) > 1e-8f)
					CHECK(glm::dot(face, a.Normal + b.Normal + c.Normal) > 0.0f);
			}
		}
		CHECK_FALSE(MeshFactory::CreateByName("Teapot").IsValid());
		CHECK(MeshFactory::CreateByName("Capsule").ComputeBounds().Max.y == doctest::Approx(1.0f).epsilon(0.01));
	}

	TEST_CASE("AABB: transformed bounds contain all transformed corners")
	{
		const AABB box{ glm::vec3(-1.0f, -2.0f, -3.0f), glm::vec3(1.0f, 2.0f, 3.0f) };
		const glm::mat4 m = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f)), 0.7f, glm::vec3(0.3f, 1.0f, 0.2f));
		const AABB t = box.Transformed(m);
		for (int i = 0; i < 8; i++)
		{
			const glm::vec3 corner((i & 1) ? box.Max.x : box.Min.x, (i & 2) ? box.Max.y : box.Min.y, (i & 4) ? box.Max.z : box.Min.z);
			const glm::vec3 p = glm::vec3(m * glm::vec4(corner, 1.0f));
			CHECK(p.x >= t.Min.x - 1e-4f);
			CHECK(p.x <= t.Max.x + 1e-4f);
			CHECK(p.y >= t.Min.y - 1e-4f);
			CHECK(p.z <= t.Max.z + 1e-4f);
		}
	}

	TEST_CASE("RenderAssets: built-in models, default material, environment and caching")
	{
		EMBER_REQUIRE_GPU(gpu);
		RenderAssets assets(gpu->GetDevice(), RenderSettings::LowQuality());
		CHECK(assets.GetBRDFLut() != nullptr);
		for (const char* name : { "builtin://Cube", "builtin://Plane", "builtin://Sphere", "builtin://Cylinder", "builtin://Capsule" })
		{
			CAPTURE(name);
			Ref<Model> model = assets.GetModel(name);
			REQUIRE(model);
			CHECK(model->MeshData->IndexCount > 0);
			CHECK(model->Materials.size() == 1);
			CHECK(assets.GetModel(name) == model); // cached
		}
		CHECK_FALSE(assets.GetModel("builtin://Teapot"));
		CHECK_FALSE(assets.GetModel("Models/missing.gltf"));
		CHECK_FALSE(assets.GetModel("../outside.gltf"));
		CHECK(assets.GetTexture("missing.png", true) == nullptr);

		const GpuMaterial* material = assets.GetDefaultMaterial();
		REQUIRE(material);
		CHECK(material->BindingSet);
		MaterialParameters red;
		red.BaseColor = { 1.0f, 0.0f, 0.0f, 1.0f };
		CHECK(assets.GetMaterial(red) == assets.GetMaterial(red));
		CHECK(assets.GetMaterial(red) != assets.GetDefaultMaterial());

		Ref<Environment> procedural = assets.GetEnvironment("");
		REQUIRE(procedural);
		CHECK(procedural->PrefilteredMipCount >= 1);
		// A missing HDRI falls back to the procedural sky.
		CHECK(assets.GetEnvironment("HDRI/missing.hdr") == procedural);
		CHECK(gpu->GetErrorCount() == 0);
	}

	TEST_CASE("SceneRenderer: lit cube over procedural sky, culling winding, picking")
	{
		EMBER_REQUIRE_GPU(gpu);
		RenderFixture f(gpu, "render_basic");
		f.AddCamera();
		Entity cube = f.AddMesh("Cube", "builtin://Cube", glm::vec3(0.0f), { 1.0f, 0.1f, 0.1f, 1.0f });
		cube.GetComponent<TransformComponent>().Rotation = { 0.3f, 0.5f, 0.0f };
		f.AddSun({ glm::radians(-50.0f), glm::radians(30.0f), 0.0f });

		const Image image = f.Render();
		const glm::vec3 center = PixelColor(image, c_Size / 2, c_Size / 2);
		CHECK(center.r > 0.2f);
		CHECK(center.r > center.g * 2.0f);
		CHECK(center.r > center.b * 2.0f);

		// Top-left corner shows the procedural sky (blue-ish, not the background color).
		const glm::vec3 sky = PixelColor(image, 2, 2);
		CHECK(sky.b > sky.r);
		CHECK(sky.b > 0.3f);

		CHECK(f.Renderer->PickEntity(c_Size / 2, c_Size / 2) == cube.GetUUID());
		CHECK(f.Renderer->PickEntity(2, 2) == UUID(0));
		CHECK(f.Renderer->PickEntity(c_Size + 10, 0) == UUID(0));
		CHECK(f.Renderer->GetStats().DrawCalls >= 2); // prepass + forward
		CHECK(gpu->GetErrorCount() == 0);

		// A single-sided plane seen from its back is culled; seen from the front it is visible.
		RenderFixture g(gpu, "render_winding");
		g.AddCamera({ 0.0f, 0.0f, 3.0f });
		g.SetSky(1.0f, false);
		Entity plane = g.AddMesh("Plane", "builtin://Plane", glm::vec3(0.0f), { 1.0f, 1.0f, 1.0f, 1.0f });
		plane.GetComponent<TransformComponent>().Rotation = { glm::radians(90.0f), 0.0f, 0.0f }; // normal now faces +Z (camera)
		CHECK(g.Renderer->PickEntity(c_Size / 2, c_Size / 2) == UUID(0)); // nothing rendered yet
		g.Render();
		CHECK(g.Renderer->PickEntity(c_Size / 2, c_Size / 2) == plane.GetUUID());
		plane.GetComponent<TransformComponent>().Rotation = { glm::radians(-90.0f), 0.0f, 0.0f }; // facing away
		g.Render();
		CHECK(g.Renderer->PickEntity(c_Size / 2, c_Size / 2) == UUID(0));
		// Double-sided materials are visible from both sides.
		plane.GetComponent<MeshRendererComponent>().MaterialOverride.DoubleSided = true;
		g.Render();
		CHECK(g.Renderer->PickEntity(c_Size / 2, c_Size / 2) == plane.GetUUID());
		CHECK(gpu->GetErrorCount() == 0);
	}

	TEST_CASE("SceneRenderer: directional shadows darken occluded ground")
	{
		EMBER_REQUIRE_GPU(gpu);
		RenderFixture f(gpu, "render_shadows");
		f.AddCamera({ 0.0f, 6.0f, 6.0f }, { glm::radians(-45.0f), 0.0f, 0.0f });
		f.SetSky(0.2f, false);
		Entity ground = f.AddMesh("Ground", "builtin://Plane", glm::vec3(0.0f), { 0.8f, 0.8f, 0.8f, 1.0f });
		ground.GetComponent<TransformComponent>().Scale = { 20.0f, 1.0f, 20.0f };
		Entity blocker = f.AddMesh("Blocker", "builtin://Cube", { 0.0f, 1.5f, 0.0f }, { 0.8f, 0.8f, 0.8f, 1.0f });
		blocker.GetComponent<TransformComponent>().Scale = { 2.0f, 0.2f, 2.0f };
		f.AddSun({ glm::radians(-90.0f), 0.0f, 0.0f }, 4.0f, true); // straight down

		// Project world points to pixels using the camera matrices.
		const RenderCamera camera = *SceneRenderer::GetPrimaryCamera(*f.SceneRef);
		auto toPixel = [&](const glm::vec3& world)
		{
			const glm::vec4 clip = camera.Projection * camera.View * glm::vec4(world, 1.0f);
			const glm::vec2 ndc = glm::vec2(clip) / clip.w;
			return glm::uvec2(static_cast<uint32_t>((ndc.x * 0.5f + 0.5f) * c_Size), static_cast<uint32_t>((0.5f - ndc.y * 0.5f) * c_Size));
		};

		const Image withShadows = f.Render();
		// A point under the blocker (footprint z in [-1, 1]) that the camera still sees past the blocker's front
		// edge, and a point far from it.
		const glm::uvec2 shadowed = toPixel({ 0.0f, 0.0f, 0.5f });
		const glm::uvec2 lit = toPixel({ 3.5f, 0.0f, 0.5f });
		REQUIRE(f.Renderer->PickEntity(shadowed.x, shadowed.y) == ground.GetUUID());
		const float shadowedLum = Luminance(PixelColor(withShadows, shadowed.x, shadowed.y));
		const float litLum = Luminance(PixelColor(withShadows, lit.x, lit.y));
		CHECK(litLum > shadowedLum * 1.8f);

		// Disabling shadows removes the difference.
		f.Renderer->GetSettings().ShadowsEnabled = false;
		const Image noShadows = f.Render();
		const float unshadowedLum = Luminance(PixelColor(noShadows, shadowed.x, shadowed.y));
		CHECK(unshadowedLum > shadowedLum * 1.8f);
		CHECK(gpu->GetErrorCount() == 0);
	}

	TEST_CASE("SceneRenderer: SSAO darkens creases")
	{
		EMBER_REQUIRE_GPU(gpu);
		RenderFixture f(gpu, "render_ssao");
		f.AddCamera({ 0.0f, 1.0f, 4.0f });
		f.SetSky(1.0f, false);
		Entity floor = f.AddMesh("Floor", "builtin://Cube", { 0.0f, -0.5f, 0.0f }, { 0.9f, 0.9f, 0.9f, 1.0f });
		floor.GetComponent<TransformComponent>().Scale = { 10.0f, 1.0f, 10.0f };
		Entity wall = f.AddMesh("Wall", "builtin://Cube", { 0.0f, 1.0f, -1.0f }, { 0.9f, 0.9f, 0.9f, 1.0f });
		wall.GetComponent<TransformComponent>().Scale = { 10.0f, 4.0f, 1.0f };

		const RenderCamera camera = *SceneRenderer::GetPrimaryCamera(*f.SceneRef);
		const glm::vec4 clip = camera.Projection * camera.View * glm::vec4(0.0f, 0.02f, -0.48f, 1.0f); // in the crease
		const uint32_t px = static_cast<uint32_t>((clip.x / clip.w * 0.5f + 0.5f) * c_Size);
		const uint32_t py = static_cast<uint32_t>((0.5f - clip.y / clip.w * 0.5f) * c_Size);

		f.Renderer->GetSettings().SSAOEnabled = true;
		const float withAO = Luminance(PixelColor(f.Render(), px, py));
		f.Renderer->GetSettings().SSAOEnabled = false;
		const float withoutAO = Luminance(PixelColor(f.Render(), px, py));
		CHECK(withAO < withoutAO * 0.9f);
		CHECK(gpu->GetErrorCount() == 0);
	}

	TEST_CASE("SceneRenderer: point and spot lights, emissive and transparent materials")
	{
		EMBER_REQUIRE_GPU(gpu);
		RenderFixture f(gpu, "render_lights");
		f.AddCamera({ 0.0f, 0.0f, 6.0f });
		f.SetSky(0.0f, false); // dark: only local lights
		Entity wall = f.AddMesh("Wall", "builtin://Plane", glm::vec3(0.0f), { 0.9f, 0.9f, 0.9f, 1.0f });
		wall.GetComponent<TransformComponent>().Rotation = { glm::radians(90.0f), 0.0f, 0.0f };
		wall.GetComponent<TransformComponent>().Scale = { 8.0f, 1.0f, 8.0f };

		Entity point = f.SceneRef->CreateEntity("Point");
		auto& pointLight = point.AddComponent<PointLightComponent>();
		pointLight.Color = { 1.0f, 0.2f, 0.2f };
		pointLight.Intensity = 1.0f;
		pointLight.Range = 3.0f;
		point.GetComponent<TransformComponent>().Translation = { -1.5f, 0.0f, 0.5f };

		Entity spot = f.SceneRef->CreateEntity("Spot");
		auto& spotLight = spot.AddComponent<SpotLightComponent>();
		spotLight.Color = { 0.2f, 0.2f, 1.0f };
		spotLight.Intensity = 3.0f;
		spotLight.Range = 5.0f;
		spotLight.InnerConeAngle = 0.15f;
		spotLight.OuterConeAngle = 0.25f;
		spot.GetComponent<TransformComponent>().Translation = { 1.5f, 0.0f, 2.0f }; // points down -Z at the wall

		Image image = f.Render();
		const glm::vec3 nearPoint = PixelColor(image, c_Size / 2 - 32, c_Size / 2);
		const glm::vec3 inSpot = PixelColor(image, c_Size / 2 + 32, c_Size / 2);
		const glm::vec3 dark = PixelColor(image, c_Size / 2, 5);
		CHECK(nearPoint.r > 0.2f);
		CHECK(nearPoint.r > nearPoint.b * 2.0f);
		CHECK(inSpot.b > 0.2f);
		CHECK(inSpot.b > inSpot.r * 2.0f);
		CHECK(Luminance(dark) < 0.05f);

		// Emissive object glows without any light.
		Entity glow = f.AddMesh("Glow", "builtin://Sphere", { 0.0f, 1.2f, 1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f });
		auto& glowMaterial = glow.GetComponent<MeshRendererComponent>().MaterialOverride;
		glowMaterial.EmissiveColor = { 0.0f, 1.0f, 0.0f };
		glowMaterial.EmissiveStrength = 2.0f;
		// Transparent blue quad in front of a red emissive quad -> mixed color.
		Entity back = f.AddMesh("Back", "builtin://Plane", { 0.0f, -1.5f, 1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f });
		back.GetComponent<TransformComponent>().Rotation = { glm::radians(90.0f), 0.0f, 0.0f };
		back.GetComponent<MeshRendererComponent>().MaterialOverride.EmissiveColor = { 1.0f, 0.0f, 0.0f };
		Entity front = f.AddMesh("Front", "builtin://Plane", { 0.0f, -1.5f, 1.5f }, { 0.0f, 0.0f, 1.0f, 0.5f });
		front.GetComponent<TransformComponent>().Rotation = { glm::radians(90.0f), 0.0f, 0.0f };
		auto& frontMaterial = front.GetComponent<MeshRendererComponent>().MaterialOverride;
		frontMaterial.Alpha = AlphaMode::Blend;
		frontMaterial.EmissiveColor = { 0.0f, 0.0f, 1.0f };

		image = f.Render();
		const RenderCamera camera = *SceneRenderer::GetPrimaryCamera(*f.SceneRef);
		auto toPixel = [&](const glm::vec3& world)
		{
			const glm::vec4 clip = camera.Projection * camera.View * glm::vec4(world, 1.0f);
			return glm::uvec2(static_cast<uint32_t>((clip.x / clip.w * 0.5f + 0.5f) * c_Size), static_cast<uint32_t>((0.5f - clip.y / clip.w * 0.5f) * c_Size));
		};
		const glm::uvec2 glowPixel = toPixel({ 0.0f, 1.2f, 1.5f });
		const glm::vec3 glowColor = PixelColor(image, glowPixel.x, glowPixel.y);
		CHECK(glowColor.g > 0.5f);
		const glm::uvec2 mixPixel = toPixel({ 0.0f, -1.5f, 1.5f });
		const glm::vec3 mixed = PixelColor(image, mixPixel.x, mixPixel.y);
		CHECK(mixed.r > 0.25f);
		CHECK(mixed.b > 0.25f);
		// Transparent objects are not pickable: the opaque one behind is.
		CHECK(f.Renderer->PickEntity(mixPixel.x, mixPixel.y) == back.GetUUID());
		CHECK(gpu->GetErrorCount() == 0);
	}

	TEST_CASE("SceneRenderer: HDRI skybox orientation and image-based lighting")
	{
		EMBER_REQUIRE_GPU(gpu);
		RenderFixture f(gpu, "render_hdri");
		// 64x32 equirect: top half red, bottom half green, a blue marker at the center (forward = -Z).
		std::vector<float> pixels(64 * 32 * 3);
		for (int y = 0; y < 32; y++)
		{
			for (int x = 0; x < 64; x++)
			{
				float* p = &pixels[(y * 64 + x) * 3];
				const bool marker = std::abs(x - 32) <= 3 && std::abs(y - 16) <= 3;
				p[0] = marker ? 0.0f : (y < 16 ? 2.0f : 0.0f);
				p[1] = marker ? 0.0f : (y < 16 ? 0.0f : 2.0f);
				p[2] = marker ? 4.0f : 0.0f;
			}
		}
		std::filesystem::create_directories(f.AssetDirectory / "HDRI");
		REQUIRE(stbi_write_hdr((f.AssetDirectory / "HDRI/test.hdr").string().c_str(), 64, 32, 3, pixels.data()) != 0);

		f.AddCamera();
		f.SetSky(1.0f, true, "HDRI/test.hdr");
		Image image = f.Render();
		const glm::vec3 center = PixelColor(image, c_Size / 2, c_Size / 2);
		CHECK(center.b > center.r);
		CHECK(center.b > center.g);
		const glm::vec3 top = PixelColor(image, c_Size / 2, 4);
		CHECK(top.r > top.g);
		const glm::vec3 bottom = PixelColor(image, c_Size / 2, c_Size - 4);
		CHECK(bottom.g > bottom.r);

		// IBL: a white sphere lit only by this sky is red on top and green underneath.
		f.SetSky(1.0f, false, "HDRI/test.hdr");
		Entity sphere = f.AddMesh("Sphere", "builtin://Sphere", glm::vec3(0.0f), { 1.0f, 1.0f, 1.0f, 1.0f });
		sphere.GetComponent<TransformComponent>().Scale = glm::vec3(2.5f);
		image = f.Render();
		const glm::vec3 upper = PixelColor(image, c_Size / 2, c_Size / 2 - 30);
		const glm::vec3 lower = PixelColor(image, c_Size / 2, c_Size / 2 + 30);
		CHECK(upper.r > upper.g);
		CHECK(lower.g > lower.r);

		// Sky rotation by 180 degrees moves the marker behind the camera.
		f.SetSky(1.0f, true, "HDRI/test.hdr");
		sphere.GetComponent<MeshRendererComponent>().Visible = false;
		f.SceneRef->FindEntityByName("Sky").GetComponent<SkyLightComponent>().Rotation = glm::radians(180.0f);
		image = f.Render();
		const glm::vec3 rotatedCenter = PixelColor(image, c_Size / 2, c_Size / 2);
		CHECK(rotatedCenter.b < 0.3f);
		CHECK(gpu->GetErrorCount() == 0);
	}

	TEST_CASE("SceneRenderer: glTF model with external texture and material asset")
	{
		EMBER_REQUIRE_GPU(gpu);
		RenderFixture f(gpu, "render_gltf");

		// 2x2 texture: all green.
		Image texture;
		texture.Width = 2;
		texture.Height = 2;
		texture.Pixels = { 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255 };
		REQUIRE(texture.SavePNG(f.AssetDirectory / "Models/textures/green.png"));

		// A quad in the XY plane facing +Z: 4 positions, 4 normals, 4 UVs, 6 indices (embedded base64 buffer).
		struct Packed
		{
			float Positions[12] = { -1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0 };
			float Normals[12] = { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 };
			float UVs[8] = { 0, 1, 1, 1, 1, 0, 0, 0 };
			uint16_t Indices[6] = { 0, 1, 2, 0, 2, 3 };
		} packed;
		const auto* bytes = reinterpret_cast<const uint8_t*>(&packed);
		static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string base64;
		for (size_t i = 0; i < sizeof(packed); i += 3)
		{
			const uint32_t chunk = (bytes[i] << 16) | ((i + 1 < sizeof(packed) ? bytes[i + 1] : 0) << 8) | (i + 2 < sizeof(packed) ? bytes[i + 2] : 0);
			base64 += alphabet[(chunk >> 18) & 63];
			base64 += alphabet[(chunk >> 12) & 63];
			base64 += i + 1 < sizeof(packed) ? alphabet[(chunk >> 6) & 63] : '=';
			base64 += i + 2 < sizeof(packed) ? alphabet[chunk & 63] : '=';
		}
		const std::string gltf = R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0, "translation": [0, 0, 0]}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0}]}],
  "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0, "roughnessFactor": 1},
                 "emissiveFactor": [0, 0, 0]}],
  "textures": [{"source": 0}],
  "images": [{"uri": "textures/green.png"}],
  "buffers": [{"byteLength": 124, "uri": "data:application/octet-stream;base64,)" + base64 + R"("}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 48},
    {"buffer": 0, "byteOffset": 48, "byteLength": 48},
    {"buffer": 0, "byteOffset": 96, "byteLength": 32},
    {"buffer": 0, "byteOffset": 128, "byteLength": 12}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [-1, -1, 0], "max": [1, 1, 0]},
    {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"}
  ]
})";
		// Fix the declared byte length to the actual struct size (padding included).
		std::string fixed = gltf;
		const std::string declared = "\"byteLength\": 124";
		fixed.replace(fixed.find(declared), declared.size(), "\"byteLength\": " + std::to_string(sizeof(packed)));
		EmberTest::WriteFile(f.AssetDirectory / "Models/quad.gltf", fixed);

		f.AddCamera({ 0.0f, 0.0f, 3.0f });
		f.SetSky(1.0f, false);
		Entity quad = f.SceneRef->CreateEntity("Quad");
		quad.AddComponent<MeshRendererComponent>().Mesh = "Models/quad.gltf";

		Ref<Model> model = f.Assets->GetModel("Models/quad.gltf");
		REQUIRE(model);
		CHECK(model->MeshData->IndexCount == 6);
		REQUIRE(model->Materials.size() == 1);
		CHECK(model->Materials[0].BaseColorTexture == "Models/textures/green.png");

		Image image = f.Render();
		glm::vec3 center = PixelColor(image, c_Size / 2, c_Size / 2);
		CHECK(center.g > 0.2f);
		CHECK(center.g > center.r * 2.0f);
		CHECK(f.Renderer->PickEntity(c_Size / 2, c_Size / 2) == quad.GetUUID());

		// Material asset overrides the model's own material.
		MaterialParameters purple;
		purple.BaseColor = { 0.0f, 0.0f, 0.0f, 1.0f };
		purple.EmissiveColor = { 1.0f, 0.0f, 1.0f };
		REQUIRE(MaterialSerializer::Save(purple, f.AssetDirectory / "Materials/purple.ematerial"));
		quad.GetComponent<MeshRendererComponent>().Material = "Materials/purple.ematerial";
		image = f.Render();
		center = PixelColor(image, c_Size / 2, c_Size / 2);
		CHECK(center.r > 0.5f);
		CHECK(center.b > 0.5f);
		CHECK(center.g < 0.2f);
		CHECK(gpu->GetErrorCount() == 0);
	}

	TEST_CASE("SceneRenderer: orthographic camera, exposure and resizing")
	{
		EMBER_REQUIRE_GPU(gpu);
		RenderFixture f(gpu, "render_ortho");
		Entity camera = f.AddCamera({ 0.0f, 0.0f, 10.0f });
		camera.GetComponent<CameraComponent>().Camera.SetOrthographic(4.0f, 0.1f, 50.0f);
		f.SetSky(1.0f, false);
		Entity cube = f.AddMesh("Cube", "builtin://Cube", { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f });
		f.Render();
		// Orthographic size 4 -> 1 world unit = 32 pixels; cube at x=1 covers pixels ~[80, 112].
		CHECK(f.Renderer->PickEntity(96, 64) == cube.GetUUID());
		CHECK(f.Renderer->PickEntity(70, 64) == UUID(0));

		f.Renderer->GetSettings().Exposure = 0.0f;
		const Image black = f.Render();
		CHECK(Luminance(PixelColor(black, 96, 64)) < 0.01f);

		f.Renderer->SetViewportSize(64, 32);
		f.SceneRef->OnViewportResize(64, 32);
		f.Renderer->GetSettings().Exposure = 1.0f;
		std::optional<RenderCamera> renderCamera = SceneRenderer::GetPrimaryCamera(*f.SceneRef);
		f.Renderer->Render(*f.SceneRef, *renderCamera);
		std::optional<Image> small = f.Renderer->CaptureImage();
		REQUIRE(small.has_value());
		CHECK(small->Width == 64);
		CHECK(small->Height == 32);
		CHECK(gpu->GetErrorCount() == 0);
	}
}
