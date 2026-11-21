#include "Ember/Core/Log.h"

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <algorithm>

namespace Ember {

	Ref<spdlog::logger> Log::s_CoreLogger;
	Ref<spdlog::logger> Log::s_ClientLogger;
	LogHistory Log::s_History;

	LogHistory::LogHistory(size_t capacity)
		: m_Capacity(std::max<size_t>(capacity, 1))
	{
		m_Messages.reserve(m_Capacity);
	}

	void LogHistory::Push(LogLevel level, std::string logger, std::string text)
	{
		std::scoped_lock lock(m_Mutex);
		LogMessage message{ level, std::move(logger), std::move(text), m_NextSequence++ };
		if (m_Messages.size() < m_Capacity)
		{
			m_Messages.push_back(std::move(message));
		}
		else
		{
			m_Messages[m_Head] = std::move(message);
			m_Head = (m_Head + 1) % m_Capacity;
		}
	}

	std::vector<LogMessage> LogHistory::GetSince(uint64_t afterSequence) const
	{
		std::scoped_lock lock(m_Mutex);
		std::vector<LogMessage> result;
		const size_t count = m_Messages.size();
		for (size_t i = 0; i < count; i++)
		{
			// When full, m_Head is the oldest element.
			const LogMessage& message = m_Messages[(m_Head + i) % count];
			if (message.Sequence > afterSequence)
				result.push_back(message);
		}
		return result;
	}

	void LogHistory::Clear()
	{
		std::scoped_lock lock(m_Mutex);
		m_Messages.clear();
		m_Head = 0;
	}

	uint64_t LogHistory::GetLatestSequence() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_NextSequence - 1;
	}

	namespace {

		LogLevel ToLogLevel(spdlog::level::level_enum level)
		{
			switch (level)
			{
				case spdlog::level::trace:
				case spdlog::level::debug:    return LogLevel::Trace;
				case spdlog::level::info:     return LogLevel::Info;
				case spdlog::level::warn:     return LogLevel::Warn;
				case spdlog::level::err:      return LogLevel::Error;
				case spdlog::level::critical: return LogLevel::Critical;
				default:                      return LogLevel::Info;
			}
		}

		class HistorySink final : public spdlog::sinks::base_sink<std::mutex>
		{
		public:
			explicit HistorySink(LogHistory& history)
				: m_History(history) {}

		protected:
			void sink_it_(const spdlog::details::log_msg& msg) override
			{
				m_History.Push(ToLogLevel(msg.level),
					std::string(msg.logger_name.data(), msg.logger_name.size()),
					std::string(msg.payload.data(), msg.payload.size()));
			}

			void flush_() override {}

		private:
			LogHistory& m_History;
		};

	}

	void Log::Init()
	{
		if (IsInitialized())
			return;

		auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		consoleSink->set_pattern("%^[%T] %n: %v%$");
		auto historySink = std::make_shared<HistorySink>(s_History);

		std::vector<spdlog::sink_ptr> sinks{ consoleSink, historySink };

		s_CoreLogger = std::make_shared<spdlog::logger>("EMBER", sinks.begin(), sinks.end());
		s_CoreLogger->set_level(spdlog::level::trace);
		s_CoreLogger->flush_on(spdlog::level::warn);

		s_ClientLogger = std::make_shared<spdlog::logger>("APP", sinks.begin(), sinks.end());
		s_ClientLogger->set_level(spdlog::level::trace);
		s_ClientLogger->flush_on(spdlog::level::warn);
	}

	void Log::Shutdown()
	{
		if (s_CoreLogger)
			s_CoreLogger->flush();
		if (s_ClientLogger)
			s_ClientLogger->flush();
		s_CoreLogger.reset();
		s_ClientLogger.reset();
	}

}
