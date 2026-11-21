#pragma once

#include "Ember/Core/Base.h"
#include "Ember/Core/Log.h"

#include <filesystem>

// EMBER_CORE_ASSERT / EMBER_ASSERT: debug-only invariant checks (compiled out in Release/Dist).
// EMBER_CORE_VERIFY / EMBER_VERIFY: checks that remain in Release builds (compiled out in Dist only).
// Both log the failure and break into the debugger.

#define EMBER_INTERNAL_ASSERT_IMPL(type, check, msg, ...) \
	do { \
		if (!(check)) \
		{ \
			if (::Ember::Log::IsInitialized()) \
				EMBER##type##ERROR(msg, __VA_ARGS__); \
			EMBER_DEBUGBREAK(); \
		} \
	} while (0)

#define EMBER_INTERNAL_ASSERT_WITH_MSG(type, check, ...) \
	EMBER_INTERNAL_ASSERT_IMPL(type, check, "Assertion failed: {0}", __VA_ARGS__)
#define EMBER_INTERNAL_ASSERT_NO_MSG(type, check) \
	EMBER_INTERNAL_ASSERT_IMPL(type, check, "Assertion '{0}' failed at {1}:{2}", EMBER_STRINGIFY_MACRO(check), \
		std::filesystem::path(__FILE__).filename().string(), __LINE__)

#define EMBER_INTERNAL_ASSERT_GET_MACRO_NAME(arg1, arg2, macro, ...) macro
#define EMBER_INTERNAL_ASSERT_GET_MACRO(...) \
	EMBER_EXPAND_MACRO(EMBER_INTERNAL_ASSERT_GET_MACRO_NAME(__VA_ARGS__, EMBER_INTERNAL_ASSERT_WITH_MSG, EMBER_INTERNAL_ASSERT_NO_MSG))

#if defined(EMBER_ENABLE_ASSERTS)
	#define EMBER_ASSERT(...) EMBER_EXPAND_MACRO(EMBER_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(_, __VA_ARGS__))
	#define EMBER_CORE_ASSERT(...) EMBER_EXPAND_MACRO(EMBER_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(_CORE_, __VA_ARGS__))
#else
	#define EMBER_ASSERT(...) ((void)0)
	#define EMBER_CORE_ASSERT(...) ((void)0)
#endif

#if defined(EMBER_ENABLE_VERIFY)
	#define EMBER_VERIFY(...) EMBER_EXPAND_MACRO(EMBER_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(_, __VA_ARGS__))
	#define EMBER_CORE_VERIFY(...) EMBER_EXPAND_MACRO(EMBER_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(_CORE_, __VA_ARGS__))
#else
	#define EMBER_VERIFY(...) ((void)0)
	#define EMBER_CORE_VERIFY(...) ((void)0)
#endif
