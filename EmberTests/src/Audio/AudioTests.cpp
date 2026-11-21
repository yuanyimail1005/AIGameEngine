#include <doctest/doctest.h>

#include "TestUtils.h"

#include "Ember/Audio/AudioEngine.h"
#include "Ember/Audio/SceneAudio.h"
#include "Ember/Scene/Entity.h"
#include "Ember/Scene/Scene.h"
#include "Ember/Scripting/ScriptEngine.h"

using namespace Ember;

TEST_SUITE("Audio")
{
	TEST_CASE("Audio: engine initializes (with or without a device) and loads sounds")
	{
		REQUIRE(AudioEngine::Init());
		CHECK(AudioEngine::IsInitialized());
		const auto dir = EmberTest::MakeTempDirectory("audio_engine");
		EmberTest::WriteSineWav(dir / "tone.wav", 0.25f);

		Sound sound;
		REQUIRE(sound.Load(dir / "tone.wav", false));
		CHECK(sound.IsLoaded());
		sound.SetVolume(0.5f);
		sound.SetPitch(1.5f);
		sound.SetLooping(true);
		sound.Play();
		CHECK(sound.IsPlaying());
		sound.Stop();
		CHECK_FALSE(sound.IsPlaying());

		Sound moved = std::move(sound);
		CHECK(moved.IsLoaded());
		CHECK_FALSE(sound.IsLoaded()); // NOLINT(bugprone-use-after-move): verifying moved-from state

		Sound missing;
		CHECK_FALSE(missing.Load(dir / "missing.wav", false));
		CHECK_FALSE(missing.IsLoaded());

		AudioEngine::SetMasterVolume(0.8f);
		CHECK(AudioEngine::GetMasterVolume() == doctest::Approx(0.8f));
		AudioEngine::SetMasterVolume(1.0f);
	}

	TEST_CASE("Audio: scene audio sources, PlayOnStart, one-shots and script API")
	{
		const auto dir = EmberTest::MakeTempDirectory("audio_scene");
		EmberTest::WriteSineWav(dir / "Audio/loop.wav", 0.5f);
		EmberTest::WriteSineWav(dir / "Audio/blip.wav", 0.1f, 880.0f);
		EmberTest::WriteFile(dir / "Scripts/Sfx.lua", R"(
local S = {}
function S:OnCreate()
	Test.Check(self.entity:PlaySound(), "PlaySound")
	Test.Check(self.entity:IsSoundPlaying(), "IsSoundPlaying")
	self.entity:StopSound()
	Test.Check(not self.entity:IsSoundPlaying(), "StopSound")
	Test.Check(Audio.PlayOneShot("Audio/blip.wav", 0.5, vec3(1, 0, 0)), "PlayOneShot")
	Test.Check(not Audio.PlayOneShot("Audio/nothing.wav"), "PlayOneShot missing")
	Audio.SetMasterVolume(0.9)
	Test.Check(math.abs(Audio.GetMasterVolume() - 0.9) < 1e-5, "master volume")
	Audio.SetMasterVolume(1.0)
end
return S
)");

		Scene scene;
		scene.SetAssetDirectory(dir);
		Entity camera = scene.CreateEntity("Camera");
		camera.AddComponent<CameraComponent>();
		camera.AddComponent<AudioListenerComponent>();
		Entity music = scene.CreateEntity("Music");
		auto& source = music.AddComponent<AudioSourceComponent>();
		source.Clip = "Audio/loop.wav";
		source.Loop = true;
		source.PlayOnStart = true;
		source.Spatial = false;
		Entity sfx = scene.CreateEntity("Sfx");
		sfx.AddComponent<AudioSourceComponent>().Clip = "Audio/blip.wav";
		sfx.AddComponent<ScriptComponent>().Script = "Scripts/Sfx.lua";
		Entity silent = scene.CreateEntity("Silent");
		silent.AddComponent<AudioSourceComponent>(); // no clip

		scene.OnRuntimeStart();
		SceneAudio& audio = *scene.GetAudio();
		CHECK(audio.IsPlaying(music));
		CHECK_FALSE(audio.Play(silent));
		CHECK_FALSE(audio.IsPlaying(silent));

		for (int i = 0; i < 5; i++)
			scene.OnUpdateRuntime(Timestep(1.0f / 60.0f));

		for (const ScriptTestResult& result : scene.GetScriptEngine()->GetTestResults())
		{
			CAPTURE(result.Name);
			CHECK(result.Passed);
		}

		// Destroying the entity stops and releases its voice.
		scene.DestroyEntity(music);
		scene.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK_FALSE(audio.IsPlaying(music));
		scene.OnRuntimeStop();
		CHECK(scene.GetAudio() == nullptr);
	}
}
