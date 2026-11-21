#pragma once

#include <cstdint>
#include <memory>
#include <utility>

// ---------------------------------------------------------------------------------------------------
// Platform detection
#if defined(_WIN32)
	#define EMBER_PLATFORM_WINDOWS
#elif defined(__APPLE__)
	#define EMBER_PLATFORM_MACOS
#elif defined(__linux__)
	#define EMBER_PLATFORM_LINUX
#else
	#error "Ember: unsupported platform"
#endif

#if defined(_MSC_VER)
	#define EMBER_DEBUGBREAK() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
	#define EMBER_DEBUGBREAK() __builtin_trap()
#endif

#if defined(EMBER_DEBUG)
	#define EMBER_ENABLE_ASSERTS
#endif

#if !defined(EMBER_DIST)
	#define EMBER_ENABLE_VERIFY
	#define EMBER_HAS_EDITOR_TOOLING
#endif

#define EMBER_BIT(x) (1u << (x))

#define EMBER_EXPAND_MACRO(x) x
#define EMBER_STRINGIFY_MACRO(x) #x

#define EMBER_BIND_EVENT_FN(fn) [this](auto&&... args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }

namespace Ember {

	template<typename T>
	using Scope = std::unique_ptr<T>;

	template<typename T, typename... Args>
	constexpr Scope<T> CreateScope(Args&&... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	template<typename T>
	using Ref = std::shared_ptr<T>;

	template<typename T, typename... Args>
	constexpr Ref<T> CreateRef(Args&&... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}

	using byte = uint8_t;

}
