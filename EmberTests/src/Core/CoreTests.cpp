#include <doctest/doctest.h>

#include "Ember/Core/Buffer.h"
#include "Ember/Core/Events.h"
#include "Ember/Core/FileSystem.h"
#include "Ember/Core/Input.h"
#include "Ember/Core/KeyCodes.h"
#include "Ember/Core/Layer.h"
#include "Ember/Core/Log.h"
#include "Ember/Core/UUID.h"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

using namespace Ember;

TEST_SUITE("Core")
{
	TEST_CASE("UUID: generated values are non-zero and unique")
	{
		std::set<uint64_t> seen;
		for (int i = 0; i < 10000; i++)
		{
			UUID id;
			CHECK(id.IsValid());
			seen.insert(static_cast<uint64_t>(id));
		}
		CHECK(seen.size() == 10000);

		UUID explicitId(42);
		CHECK(static_cast<uint64_t>(explicitId) == 42);
		CHECK_FALSE(UUID(0).IsValid());
		CHECK(std::hash<UUID>()(explicitId) == std::hash<uint64_t>()(42));
	}

	TEST_CASE("Buffer: construction, copy-in and move semantics")
	{
		const uint8_t bytes[] = { 1, 2, 3, 4 };
		Buffer buffer(bytes, sizeof(bytes));
		REQUIRE(buffer.Size() == 4);
		CHECK(buffer.Data()[2] == 3);
		CHECK(static_cast<bool>(buffer));

		Buffer moved = std::move(buffer);
		CHECK(moved.Size() == 4);
		CHECK(moved.As<uint8_t>()[3] == 4);

		moved.Release();
		CHECK(moved.Empty());
		CHECK_FALSE(static_cast<bool>(moved));
	}

	TEST_CASE("FileSystem: atomic text and binary round trip")
	{
		const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ember_tests_fs";
		std::filesystem::remove_all(dir);
		const std::filesystem::path file = dir / "nested" / "file.txt";

		REQUIRE(FileSystem::WriteText(file, "hello ember"));
		CHECK(FileSystem::Exists(file));
		CHECK_FALSE(FileSystem::Exists(file.string() + ".tmp"));

		auto text = FileSystem::ReadText(file);
		REQUIRE(text.has_value());
		CHECK(*text == "hello ember");

		// Overwrite replaces the content entirely.
		REQUIRE(FileSystem::WriteText(file, "x"));
		CHECK(*FileSystem::ReadText(file) == "x");

		// Empty file
		REQUIRE(FileSystem::WriteBinary(dir / "empty.bin", nullptr, 0));
		auto empty = FileSystem::ReadBinary(dir / "empty.bin");
		REQUIRE(empty.has_value());
		CHECK(empty->Empty());

		CHECK_FALSE(FileSystem::ReadText(dir / "does_not_exist.txt").has_value());

		CHECK(FileSystem::IsSafeRelativePath("Scripts/Player.lua"));
		CHECK_FALSE(FileSystem::IsSafeRelativePath("a/b/../../c"));
		CHECK_FALSE(FileSystem::IsSafeRelativePath(""));
		CHECK_FALSE(FileSystem::IsSafeRelativePath("../secret"));
		CHECK_FALSE(FileSystem::IsSafeRelativePath("/etc/passwd"));
		CHECK_FALSE(FileSystem::IsSafeRelativePath("a\\..\\..\\b"));
		CHECK(FileSystem::GetExecutableDirectory().is_absolute());

		std::filesystem::remove_all(dir);
	}

	TEST_CASE("KeyCodes: string conversion round trip and case insensitivity")
	{
		CHECK(KeyCodeToString(KeyCode::W) == "W");
		CHECK(KeyCodeFromString("w") == KeyCode::W);
		CHECK(KeyCodeFromString("SPACE") == KeyCode::Space);
		CHECK(KeyCodeFromString("LeftShift") == KeyCode::LeftShift);
		CHECK(KeyCodeFromString("1") == KeyCode::D1);
		CHECK(KeyCodeFromString("not a key") == KeyCode::Unknown);
		CHECK(KeyCodeToString(KeyCode::Unknown) == "Unknown");

		for (int code = 0; code < static_cast<int>(KeyCode::Count); code++)
		{
			const KeyCode key = static_cast<KeyCode>(code);
			const std::string_view name = KeyCodeToString(key);
			if (name != "Unknown")
				CHECK(KeyCodeFromString(name) == key);
		}

		bool valid = false;
		CHECK(MouseButtonFromString("right", &valid) == MouseButton::Right);
		CHECK(valid);
		MouseButtonFromString("nope", &valid);
		CHECK_FALSE(valid);
		CHECK(MouseButtonToString(MouseButton::Middle) == "Middle");
	}

	TEST_CASE("Input: event-driven state, per-frame edges and deltas")
	{
		Input::Reset();
		Input::EndFrame();

		KeyPressedEvent press(KeyCode::A, false);
		Input::OnEvent(press);
		CHECK(Input::IsKeyPressed(KeyCode::A));
		CHECK(Input::IsKeyJustPressed(KeyCode::A));
		CHECK_FALSE(Input::IsKeyJustReleased(KeyCode::A));

		Input::EndFrame();
		CHECK(Input::IsKeyPressed(KeyCode::A));
		CHECK_FALSE(Input::IsKeyJustPressed(KeyCode::A));

		KeyReleasedEvent release(KeyCode::A);
		Input::OnEvent(release);
		CHECK_FALSE(Input::IsKeyPressed(KeyCode::A));
		CHECK(Input::IsKeyJustReleased(KeyCode::A));
		Input::EndFrame();
		CHECK_FALSE(Input::IsKeyJustReleased(KeyCode::A));

		MouseButtonPressedEvent mouseDown(MouseButton::Right);
		Input::OnEvent(mouseDown);
		CHECK(Input::IsMouseButtonPressed(MouseButton::Right));
		CHECK(Input::IsMouseButtonJustPressed(MouseButton::Right));
		MouseButtonReleasedEvent mouseUp(MouseButton::Right);
		Input::OnEvent(mouseUp);
		CHECK(Input::IsMouseButtonJustReleased(MouseButton::Right) == false); // pressed and released in the same frame
		CHECK_FALSE(Input::IsMouseButtonPressed(MouseButton::Right));

		Input::EndFrame();
		MouseMovedEvent move1(10.0f, 20.0f);
		Input::OnEvent(move1);
		Input::EndFrame();
		MouseMovedEvent move2(15.0f, 18.0f);
		Input::OnEvent(move2);
		CHECK(Input::GetMousePosition().x == doctest::Approx(15.0f));
		CHECK(Input::GetMouseDelta().x == doctest::Approx(5.0f));
		CHECK(Input::GetMouseDelta().y == doctest::Approx(-2.0f));

		MouseScrolledEvent scroll(0.0f, 1.5f);
		Input::OnEvent(scroll);
		Input::OnEvent(scroll);
		CHECK(Input::GetScrollDelta().y == doctest::Approx(3.0f));
		Input::EndFrame();
		CHECK(Input::GetScrollDelta().y == doctest::Approx(0.0f));

		// Focus loss releases everything.
		KeyPressedEvent pressW(KeyCode::W, false);
		Input::OnEvent(pressW);
		WindowFocusEvent unfocus(false);
		Input::OnEvent(unfocus);
		CHECK_FALSE(Input::IsKeyPressed(KeyCode::W));

		// Unknown keys are ignored.
		KeyPressedEvent unknown(KeyCode::Unknown, false);
		Input::OnEvent(unknown);
		CHECK_FALSE(Input::IsKeyPressed(KeyCode::Unknown));

		Input::Reset();
	}

	TEST_CASE("EventDispatcher: dispatches by type and marks handled")
	{
		WindowResizeEvent resize(640, 480);
		EventDispatcher dispatcher(resize);

		bool closeCalled = false;
		CHECK_FALSE(dispatcher.Dispatch<WindowCloseEvent>([&](WindowCloseEvent&) { closeCalled = true; return true; }));
		CHECK_FALSE(closeCalled);
		CHECK_FALSE(resize.Handled);

		uint32_t width = 0;
		CHECK(dispatcher.Dispatch<WindowResizeEvent>([&](WindowResizeEvent& e) { width = e.GetWidth(); return true; }));
		CHECK(width == 640);
		CHECK(resize.Handled);
		CHECK(resize.IsInCategory(EventCategoryApplication));
		CHECK_FALSE(resize.IsInCategory(EventCategoryInput));
		CHECK(std::string(resize.GetName()) == "WindowResize");
	}

	namespace {

		struct RecordingLayer : public Layer
		{
			RecordingLayer(std::string name, std::vector<std::string>& log) : Layer(std::move(name)), Log(log) {}
			~RecordingLayer() override { Log.push_back("dtor:" + GetName()); }
			void OnAttach() override { Log.push_back("attach:" + GetName()); }
			void OnDetach() override { Log.push_back("detach:" + GetName()); }
			std::vector<std::string>& Log;
		};

	}

	TEST_CASE("LayerStack: overlays stay above layers; clear detaches in reverse order")
	{
		std::vector<std::string> log;
		{
			LayerStack stack;
			auto* overlay = new RecordingLayer("overlay", log);
			stack.PushOverlay(overlay);
			stack.PushLayer(new RecordingLayer("a", log));
			stack.PushLayer(new RecordingLayer("b", log));

			std::vector<std::string> order;
			for (Layer* layer : stack)
				order.push_back(layer->GetName());
			CHECK(order == std::vector<std::string>{ "a", "b", "overlay" });

			// Popping an overlay through PopLayer must fail.
			CHECK_FALSE(stack.PopLayer(overlay));
			CHECK(stack.PopOverlay(overlay));
			delete overlay;
			CHECK(stack.Size() == 2);
			log.clear();
		}
		CHECK(log == std::vector<std::string>{ "detach:b", "dtor:b", "detach:a", "dtor:a" });
	}

	TEST_CASE("LogHistory: ring buffer keeps newest messages in order")
	{
		LogHistory history(3);
		for (int i = 0; i < 5; i++)
			history.Push(LogLevel::Info, "T", std::to_string(i));

		auto all = history.GetSince(0);
		REQUIRE(all.size() == 3);
		CHECK(all[0].Text == "2");
		CHECK(all[2].Text == "4");
		CHECK(history.GetLatestSequence() == 5);

		auto newer = history.GetSince(4);
		REQUIRE(newer.size() == 1);
		CHECK(newer[0].Text == "4");

		history.Clear();
		CHECK(history.GetSince(0).empty());
	}

	TEST_CASE("Log: engine messages are captured in the global history")
	{
		const uint64_t before = Log::GetHistory().GetLatestSequence();
		EMBER_CORE_ERROR("history capture test {}", 123);
		auto messages = Log::GetHistory().GetSince(before);
		REQUIRE_FALSE(messages.empty());
		CHECK(messages.back().Level == LogLevel::Error);
		CHECK(messages.back().Text == "history capture test 123");
		CHECK(messages.back().Logger == "EMBER");
	}
}
