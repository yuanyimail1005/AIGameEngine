#pragma once

#include "Ember/Core/Base.h"
#include "Ember/Core/Timestep.h"
#include "Ember/Core/UUID.h"

#include <string>
#include <vector>

namespace Ember {

	class Entity;
	class Scene;

	struct ScriptTestResult
	{
		std::string Name;
		bool Passed = false;
		std::string Message;
	};

	// Lua scripting for one running scene.
	//
	// A script file returns a table ("class"). Each entity with a ScriptComponent gets its own instance table
	// whose metatable indexes the class, with `self.entity` bound to the entity. Supported callbacks (all optional):
	//   OnCreate(self), OnUpdate(self, dt), OnLateUpdate(self, dt), OnDestroy(self),
	//   OnCollisionBegin(self, other), OnCollisionEnd(self, other), OnTriggerBegin(self, other), OnTriggerEnd(self, other)
	//
	// Scripts run sandboxed (no io / os.execute / arbitrary file loading); `require` loads modules from the
	// project's asset directory. The full API is documented in docs/ScriptingAPI.md.
	class ScriptEngine
	{
	public:
		explicit ScriptEngine(Scene* scene);
		~ScriptEngine();

		ScriptEngine(const ScriptEngine&) = delete;
		ScriptEngine& operator=(const ScriptEngine&) = delete;

		void OnRuntimeStart();
		void OnRuntimeStop();
		void OnUpdate(Timestep ts);
		void OnLateUpdate(Timestep ts);
		void DispatchPhysicsEvents();
		void OnEntityDestroyed(Entity entity);

		// Runs a chunk of Lua in the scene's script environment (used by the editor console and AI interface).
		// Returns false and sets outError on failure; outResult receives the stringified return values.
		bool ExecuteString(const std::string& code, std::string* outResult = nullptr, std::string* outError = nullptr);

		// Number of script errors (load failures, runtime errors in callbacks) since start.
		uint32_t GetErrorCount() const;
		const std::vector<std::string>& GetErrors() const;
		// Results recorded by scripts through the Test API (Test.Check / Test.Pass / Test.Fail).
		const std::vector<ScriptTestResult>& GetTestResults() const;
		// Names declared with Test.Expect(name) that have no passing result yet. Automated runs fail if any remain,
		// so a test scene cannot pass by silently stopping early.
		std::vector<std::string> GetMissingExpectedTests() const;
		bool HasInstance(Entity entity) const;

	private:
		struct Impl;
		Scope<Impl> m_Impl;
	};

}
