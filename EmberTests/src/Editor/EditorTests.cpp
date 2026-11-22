#include <doctest/doctest.h>

#include "GpuTestContext.h"
#include "TestUtils.h"

#include "AIServer.h"
#include "EditorCommands.h"
#include "EditorContext.h"

#include "Ember/Core/FileSystem.h"
#include "Ember/Core/Input.h"
#include "Ember/Renderer/GraphicsDevice.h"

#if !defined(_WIN32)
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <unistd.h>
#endif

using namespace Ember;

namespace {

	struct EditorFixture
	{
		Scope<EditorContext> Context;
		Scope<EditorCommands> Commands;
		std::filesystem::path Root;

		EditorFixture(Ember::GraphicsDevice* gpu, const std::string& name)
			: Root(EmberTest::MakeTempDirectory(name))
		{
			Context = CreateScope<EditorContext>(*gpu);
			Commands = CreateScope<EditorCommands>(*Context);
		}

		Json Call(const std::string& method, const Json& params = Json::object())
		{
			const Json response = Commands->HandleRequest({ { "id", 1 }, { "method", method }, { "params", params } });
			if (response.contains("error"))
				FAIL_CHECK("command '" << method << "' failed: " << response["error"]["message"].get<std::string>());
			return response.contains("result") ? response["result"] : Json();
		}

		std::string CallError(const std::string& method, const Json& params = Json::object())
		{
			const Json response = Commands->HandleRequest({ { "id", 1 }, { "method", method }, { "params", params } });
			REQUIRE_MESSAGE(response.contains("error"), "expected '" << method << "' to fail");
			return response["error"]["message"].get<std::string>();
		}
	};

}

TEST_SUITE("Editor")
{
	TEST_CASE("Editor commands: protocol, discovery and error handling")
	{
		EMBER_REQUIRE_GPU(gpu);
		EditorFixture f(gpu, "editor_protocol");

		CHECK(f.Call("ping") == "pong");
		const Json commands = f.Call("list_commands");
		CHECK(commands.size() > 40);
		for (const Json& command : commands)
		{
			CHECK_FALSE(command["description"].get<std::string>().empty());
			CHECK(command["params"].is_object());
		}
		CHECK(f.Call("list_commands", { { "category", "Entity" } }).size() >= 5);

		CHECK(f.CallError("no_such_command").find("unknown command") != std::string::npos);
		CHECK(f.Commands->HandleLine("not json").find("malformed JSON") != std::string::npos);
		CHECK(f.Commands->HandleLine("{\"id\": 7}").find("\"id\":7") != std::string::npos);
		CHECK(f.CallError("create_entity", { { "components", { { "Bogus", Json::object() } } } }).find("unknown component") != std::string::npos);
		CHECK(f.CallError("get_entity", { { "entity", "Nobody" } }).find("no entity named") != std::string::npos);
		CHECK(f.CallError("save_scene").find("no project") != std::string::npos);
		CHECK(f.Call("get_info")["project"].is_null());
		CHECK(gpu->GetErrorCount() == 0);
	}

	TEST_CASE("Editor commands: project, scene and entity workflow with undo/redo")
	{
		EMBER_REQUIRE_GPU(gpu);
		EditorFixture f(gpu, "editor_workflow");
		const std::string projectDir = (f.Root / "Game").string();

		const Json created = f.Call("new_project", { { "path", projectDir }, { "name", "Game" } });
		CHECK(created["scene_path"] == "Scenes/Main.escene");
		CHECK(f.CallError("new_project", { { "path", projectDir }, { "name", "Game" } }).find("already exists") != std::string::npos);
		const size_t starterEntities = f.Call("list_entities").size();
		CHECK(starterEntities == 3); // camera, sun, sky

		const Json player = f.Call("create_entity", { { "name", "Player" }, { "components", {
			{ "MeshRenderer", { { "Mesh", "builtin://Capsule" } } },
			{ "Transform", { { "Translation", { 1, 2, 3 } } } } } } });
		const std::string playerId = player["id"];
		CHECK(player["components"].size() == 2);

		const Json weapon = f.Call("create_entity", { { "name", "Weapon" }, { "parent", "Player" } });
		CHECK(weapon["parent"] == playerId);

		// Component edits (by name or id), patch semantics, validation.
		Json mesh = f.Call("set_component", { { "entity", playerId }, { "component", "MeshRenderer" },
			{ "values", { { "MaterialOverride", { { "BaseColor", { 0.2, 0.4, 1.0 } } } } } } });
		CHECK(mesh["OverrideMaterial"] == true);
		CHECK(mesh["Mesh"] == "builtin://Capsule");
		CHECK(f.CallError("set_component", { { "entity", "Player" }, { "component", "RigidBody" }, { "values", { { "Type", "Floaty" } } } })
			.find("invalid") != std::string::npos);
		f.Call("add_component", { { "entity", "Player" }, { "component", "CapsuleCollider" } });
		CHECK(f.Call("get_component", { { "entity", "Player" }, { "component", "CapsuleCollider" } })["Radius"] == 0.5);
		f.Call("remove_component", { { "entity", "Player" }, { "component", "CapsuleCollider" } });
		CHECK(f.CallError("remove_component", { { "entity", "Player" }, { "component", "Transform" } }).find("cannot be removed") != std::string::npos);

		const Json transform = f.Call("set_transform", { { "entity", "Player" }, { "rotation_degrees", { 0, 90, 0 } }, { "scale", 2 } });
		CHECK(transform["Scale"][0] == 2.0);
		const Json details = f.Call("get_entity", { { "entity", "Weapon" } });
		CHECK(details["world_position"][1].get<double>() == doctest::Approx(2.0));

		CHECK(f.Call("find_entities", { { "component", "MeshRenderer" } }).size() == 1);
		CHECK(f.Call("find_entities", { { "name", "Wea" } }).size() == 1);
		f.Call("rename_entity", { { "entity", "Weapon" }, { "name", "Sword" } });
		CHECK(f.Call("duplicate_entity", { { "entity", "Sword" } })["name"] == "Sword");
		CHECK(f.CallError("set_parent", { { "entity", "Player" }, { "parent", "Sword" } }).find("descendants") != std::string::npos);

		// Undo / redo
		const size_t before = f.Call("list_entities").size();
		f.Call("destroy_entity", { { "entity", "Player" } });
		CHECK(f.Call("list_entities").size() == before - 3);
		CHECK(f.Call("undo")["undone"] == true);
		CHECK(f.Call("list_entities").size() == before);
		CHECK(f.Call("redo")["redone"] == true);
		CHECK(f.Call("list_entities").size() == before - 3);
		f.Call("undo");

		// Save, reload, scene data round trip
		f.Call("save_scene", { { "path", "Scenes/Level.escene" } });
		CHECK(f.Call("get_info")["scene_path"] == "Scenes/Level.escene");
		CHECK(f.CallError("save_scene", { { "path", "../outside.escene" } }).find("relative") != std::string::npos);
		const std::string yaml = f.Call("get_scene")["yaml"];
		f.Call("new_scene", { { "name", "Empty" } });
		CHECK(f.Call("list_entities").empty());
		f.Call("load_scene_data", { { "yaml", yaml } });
		CHECK(f.Call("list_entities").size() == before);
		f.Call("open_scene", { { "path", "Scenes/Main.escene" } });
		CHECK(f.Call("list_entities").size() == starterEntities);
		f.Call("open_scene", { { "path", "Scenes/Level.escene" } });
		CHECK(f.Call("find_entities", { { "name", "Player" } }).size() == 1);

		// Project settings
		f.Call("set_project", { { "StartScene", "Scenes/Level.escene" }, { "WindowWidth", 800 } });
		CHECK(f.Call("get_project")["StartScene"] == "Scenes/Level.escene");
		f.Call("open_project", { { "path", projectDir } });
		CHECK(f.Call("get_info")["scene_path"] == "Scenes/Level.escene");
		CHECK(f.Call("get_project")["WindowWidth"] == 800);
		CHECK(gpu->GetErrorCount() == 0);
	}

	TEST_CASE("Editor commands: assets, materials and prefabs")
	{
		EMBER_REQUIRE_GPU(gpu);
		EditorFixture f(gpu, "editor_assets");
		f.Call("new_project", { { "path", (f.Root / "P").string() }, { "name", "P" } });

		f.Call("write_asset", { { "path", "Scripts/Hello.lua" }, { "content", "return {}\n" } });
		CHECK(f.Call("read_asset", { { "path", "Scripts/Hello.lua" } })["content"] == "return {}\n");
		CHECK(f.CallError("write_asset", { { "path", "../../evil.txt" }, { "content", "x" } }).find("relative") != std::string::npos);
		CHECK(f.CallError("write_asset", { { "path", "/etc/evil.txt" }, { "content", "x" } }).find("relative") != std::string::npos);

		bool found = false;
		for (const Json& file : f.Call("list_assets"))
			found |= file["path"] == "Scripts/Hello.lua";
		CHECK(found);
		CHECK(f.Call("list_assets", { { "directory", "Scripts" }, { "recursive", false } }).size() == 1);
		f.Call("delete_asset", { { "path", "Scripts/Hello.lua" } });
		CHECK(f.CallError("read_asset", { { "path", "Scripts/Hello.lua" } }).find("cannot read") != std::string::npos);

		const Json material = f.Call("create_material", { { "path", "Materials/Gold.ematerial" },
			{ "values", { { "BaseColor", { 1.0, 0.8, 0.3 } }, { "Metallic", 1.0 }, { "Roughness", 0.25 } } } });
		CHECK(material["material"]["Metallic"] == 1.0);
		CHECK(f.CallError("create_material", { { "path", "Materials/Bad.ematerial" }, { "values", { { "AlphaMode", "Sparkly" } } } })
			.find("invalid") != std::string::npos);

		// External import
		EmberTest::WriteFile(f.Root / "external.txt", "data");
		f.Call("import_file", { { "source", (f.Root / "external.txt").string() }, { "destination", "Imported/external.txt" } });
		CHECK(f.Call("read_asset", { { "path", "Imported/external.txt" } })["content"] == "data");

		// Prefab round trip
		const Json root = f.Call("create_entity", { { "name", "Tower" }, { "components", { { "MeshRenderer", { { "Mesh", "builtin://Cylinder" },
			{ "Material", "Materials/Gold.ematerial" } } } } } });
		f.Call("create_entity", { { "name", "Flag" }, { "parent", root["id"] } });
		f.Call("create_prefab", { { "entity", "Tower" }, { "path", "Prefabs/Tower.eprefab" } });
		const Json instance = f.Call("instantiate_prefab", { { "path", "Prefabs/Tower.eprefab" }, { "position", { 5, 0, 0 } } });
		CHECK(instance["name"] == "Tower");
		CHECK(instance["id"] != root["id"]);
		CHECK(instance["children"].size() == 1);
		CHECK(f.Call("get_component", { { "entity", instance["id"] }, { "component", "Prefab" } })["Prefab"] == "Prefabs/Tower.eprefab");
		CHECK(gpu->GetErrorCount() == 0);
	}

	TEST_CASE("Editor commands: play mode, scripts, input, rendering and export")
	{
		EMBER_REQUIRE_GPU(gpu);
		Input::Reset();
		EditorFixture f(gpu, "editor_play");
		f.Call("new_project", { { "path", (f.Root / "G").string() }, { "name", "G" } });
		f.Call("write_asset", { { "path", "Scripts/Mover.lua" }, { "content", R"(
local M = {}
function M:OnCreate() self.frames = 0 end
function M:OnUpdate(dt)
	self.frames = self.frames + 1
	if Input.IsKeyDown("Right") then self.entity:Translate(vec3(1, 0, 0)) end
end
return M
)" } });
		const Json box = f.Call("create_entity", { { "name", "Box" }, { "components", {
			{ "MeshRenderer", { { "Mesh", "builtin://Cube" }, { "MaterialOverride", { { "BaseColor", { 1, 0, 0 } } } } } },
			{ "Script", { { "Script", "Scripts/Mover.lua" } } } } } });
		f.Call("save_scene");

		CHECK(f.CallError("execute_lua", { { "code", "1+1" } }).find("play mode") != std::string::npos);
		const Json state = f.Call("run_frames", { { "frames", 10 } });
		CHECK(state["time"].get<double>() == doctest::Approx(10.0 / 60.0));
		CHECK(f.Call("get_play_state")["playing"] == true);
		CHECK(f.Call("execute_lua", { { "code", "Scene.FindEntityByName('Box'):GetScript().frames" } })["result"] == "10");

		f.Call("send_input", { { "key", "Right" }, { "action", "press" } });
		f.Call("run_frames", { { "frames", 5 } });
		f.Call("send_input", { { "key", "Right" }, { "action", "release" } });
		f.Call("run_frames", { { "frames", 5 } });
		CHECK(f.Call("get_component", { { "entity", "Box" }, { "component", "Transform" } })["Translation"][0] == 5.0);
		CHECK(f.CallError("send_input", { { "key", "NotAKey" } }).find("unknown key") != std::string::npos);

		// Runtime changes are discarded on stop; edits during play do not enter the undo history.
		f.Call("stop");
		CHECK(f.Call("get_component", { { "entity", "Box" }, { "component", "Transform" } })["Translation"][0] == 0.0);

		// Rendering: capture with the scene camera, then pick the box at the image center.
		f.Call("set_transform", { { "entity", "Main Camera" }, { "position", { 0, 0, 5 } }, { "rotation_degrees", { 0, 0, 0 } } });
		const std::filesystem::path capture = f.Root / "capture.png";
		const Json captured = f.Call("capture", { { "path", capture.string() }, { "width", 160 }, { "height", 90 } });
		CHECK(captured["camera"] == "game");
		CHECK(FileSystem::Exists(capture));
		std::optional<Image> image = Image::LoadFromFile(capture);
		REQUIRE(image.has_value());
		CHECK(image->Width == 160);
		const uint8_t* center = image->GetPixel(80, 45);
		CHECK(center[0] > center[1] * 2);
		CHECK(f.Call("pick_entity", { { "x", 80 }, { "y", 45 } })["id"] == box["id"]);
		CHECK(f.Call("pick_entity", { { "x", 2 }, { "y", 2 } }).is_null());
		CHECK(captured["stats"]["draw_calls"].get<int>() > 0);
		CHECK(captured["stats"]["ui_elements"] == 0);

		// Screen-space UI created through the AI interface appears in captures and in the stats.
		const Json types = f.Call("list_component_types");
		CHECK(types.dump().find("\"UIText\"") != std::string::npos);
		CHECK(types.dump().find("\"UIPanel\"") != std::string::npos);
		f.Call("create_entity", { { "name", "HUD" }, { "components", {
			{ "UIPanel", { { "Size", { 160, 160 } }, { "Color", { 1, 1, 1, 1 } }, { "Anchor", { 0, 0 } }, { "Pivot", { 0, 0 } } } },
			{ "UIText", { { "Text", "42" }, { "FontSize", 64 }, { "Color", { 0, 0, 0, 1 } } } } } } });
		CHECK(f.Call("get_component", { { "entity", "HUD" }, { "component", "UIText" } })["Text"] == "42");
		CHECK(f.CallError("set_component", { { "entity", "HUD" }, { "component", "UIText" }, { "values", { { "Alignment", "Up" } } } })
			.find("UIText") != std::string::npos);
		const Json hudCapture = f.Call("capture", { { "path", (f.Root / "hud.png").string() }, { "width", 160 }, { "height", 90 } });
		CHECK(hudCapture["stats"]["ui_elements"] == 2);
		image = Image::LoadFromFile(f.Root / "hud.png");
		REQUIRE(image.has_value());
		const uint8_t* corner = image->GetPixel(5, 5); // white panel: 160x160 reference px = 20x20 screen px
		CHECK(corner[0] == 255);
		CHECK(corner[1] == 255);
		f.Call("destroy_entity", { { "entity", "HUD" } });

		f.Call("set_editor_camera", { { "position", { 3, 3, 3 } }, { "target", { 0, 0, 0 } } });
		f.Call("focus_entity", { { "entity", "Box" } });
		CHECK(f.Call("capture", { { "path", (f.Root / "editor.png").string() }, { "camera", "editor" } })["camera"] == "editor");

		const Json settings = f.Call("set_render_settings", { { "SSAOEnabled", false }, { "Exposure", 1.5 } });
		CHECK(settings["SSAOEnabled"] == false);
		CHECK(f.CallError("set_render_settings", { { "Tonemapper", 9 } }).find("Tonemapper") != std::string::npos);

		// Export (the runtime binary sits next to the test executable).
		const Json exported = f.Call("export_game", { { "output", (f.Root / "Build").string() } });
		CHECK(FileSystem::Exists(exported["executable"].get<std::string>()));
		CHECK(FileSystem::Exists(f.Root / "Build" / "G.eproject"));
		CHECK(FileSystem::Exists(f.Root / "Build" / "Assets" / "Scripts" / "Mover.lua"));
		CHECK(f.CallError("export_game", { { "output", (f.Root / "G" / "Inside").string() } }).find("outside") != std::string::npos);

		const Json logs = f.Call("get_logs", { { "since", 0 }, { "min_level", "error" } });
		CHECK(logs["last"].get<uint64_t>() > 0);
		CHECK(gpu->GetErrorCount() == 0);
		Input::Reset();
	}

#if !defined(_WIN32)
	TEST_CASE("AIServer: line-delimited JSON over TCP on localhost")
	{
		EMBER_REQUIRE_GPU(gpu);
		EditorFixture f(gpu, "editor_server");
		AIServer server;
		std::string error;
		REQUIRE(server.Start(0, [&](const std::string& line) { return f.Commands->HandleLine(line); }, &error));
		REQUIRE(server.GetPort() != 0);

		const int client = socket(AF_INET, SOCK_STREAM, 0);
		REQUIRE(client >= 0);
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = htons(server.GetPort());
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		REQUIRE(connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);

		// Two requests in one write, the second split across writes.
		const std::string requests = "{\"id\":1,\"method\":\"ping\"}\n{\"id\":2,\"method\":\"create_entity\",";
		REQUIRE(send(client, requests.data(), requests.size(), 0) == static_cast<ssize_t>(requests.size()));
		const std::string rest = "\"params\":{\"name\":\"Remote\"}}\r\n";
		REQUIRE(send(client, rest.data(), rest.size(), 0) == static_cast<ssize_t>(rest.size()));

		std::string received;
		char buffer[4096];
		while (std::count(received.begin(), received.end(), '\n') < 2)
		{
			const ssize_t n = recv(client, buffer, sizeof(buffer), 0);
			REQUIRE(n > 0);
			received.append(buffer, static_cast<size_t>(n));
		}
		const size_t split = received.find('\n');
		const Json first = Json::parse(received.substr(0, split));
		const Json second = Json::parse(received.substr(split + 1));
		CHECK(first["id"] == 1);
		CHECK(first["result"] == "pong");
		CHECK(second["id"] == 2);
		CHECK(second["result"]["name"] == "Remote");
		CHECK(f.Context->GetEditorScene()->FindEntityByName("Remote"));

		close(client);
		server.Stop();
		CHECK_FALSE(server.IsRunning());
	}
#endif
}
