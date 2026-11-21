#pragma once

#include "Ember/Core/Base.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace Ember {

	enum class LogLevel : uint8_t
	{
		Trace = 0, Info, Warn, Error, Critical
	};

	struct LogMessage
	{
		LogLevel Level = LogLevel::Info;
		std::string Logger;
		std::string Text;
		uint64_t Sequence = 0;
	};

	// Thread-safe ring buffer of recent log messages. Consumed by the editor console panel and the
	// AI control interface, so agents can inspect engine output without access to stdout.
	class LogHistory
	{
	public:
		explicit LogHistory(size_t capacity = 2048);

		void Push(LogLevel level, std::string logger, std::string text);
		// Returns all messages with Sequence > afterSequence (oldest first).
		std::vector<LogMessage> GetSince(uint64_t afterSequence) const;
		void Clear();
		uint64_t GetLatestSequence() const;
		size_t GetCapacity() const { return m_Capacity; }

	private:
		mutable std::mutex m_Mutex;
		std::vector<LogMessage> m_Messages;
		size_t m_Capacity;
		size_t m_Head = 0;
		uint64_t m_NextSequence = 1;
	};

	class Log
	{
	public:
		static void Init();
		static void Shutdown();
		static bool IsInitialized() { return s_CoreLogger != nullptr; }

		static Ref<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
		static Ref<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }
		static LogHistory& GetHistory() { return s_History; }

	private:
		static Ref<spdlog::logger> s_CoreLogger;
		static Ref<spdlog::logger> s_ClientLogger;
		static LogHistory s_History;
	};

}

// Core log macros
#define EMBER_CORE_TRACE(...)    ::Ember::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define EMBER_CORE_INFO(...)     ::Ember::Log::GetCoreLogger()->info(__VA_ARGS__)
#define EMBER_CORE_WARN(...)     ::Ember::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define EMBER_CORE_ERROR(...)    ::Ember::Log::GetCoreLogger()->error(__VA_ARGS__)
#define EMBER_CORE_CRITICAL(...) ::Ember::Log::GetCoreLogger()->critical(__VA_ARGS__)

// Client (game / editor / script) log macros
#define EMBER_TRACE(...)         ::Ember::Log::GetClientLogger()->trace(__VA_ARGS__)
#define EMBER_INFO(...)          ::Ember::Log::GetClientLogger()->info(__VA_ARGS__)
#define EMBER_WARN(...)          ::Ember::Log::GetClientLogger()->warn(__VA_ARGS__)
#define EMBER_ERROR(...)         ::Ember::Log::GetClientLogger()->error(__VA_ARGS__)
#define EMBER_CRITICAL(...)      ::Ember::Log::GetClientLogger()->critical(__VA_ARGS__)
