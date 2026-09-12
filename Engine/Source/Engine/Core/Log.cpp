#include "Engine/Core/Log.hpp"

#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Engine
{
	namespace
	{
		std::shared_ptr<spdlog::logger> s_CoreLogger;
		std::shared_ptr<spdlog::logger> s_ClientLogger;

		LogLevel s_Level = LogLevel::Trace;

		bool s_Initialized = false;

		spdlog::level::level_enum ToBackendLevel(LogLevel level)
		{
			switch (level)
			{
				case LogLevel::Trace:
				{
					return spdlog::level::trace;
				}
				case LogLevel::Info:
				{
					return spdlog::level::info;
				}
				case LogLevel::Warn:
				{
					return spdlog::level::warn;
				}
				case LogLevel::Error:
				{
					return spdlog::level::err;
				}
				case LogLevel::Critical:
				{
					return spdlog::level::critical;
				}
				case LogLevel::Off:
				{
					return spdlog::level::off;
				}
			}

			return spdlog::level::info;
		}
	}

	void Log::Initialize()
	{
		if (s_Initialized)
		{
			return;
		}

		auto l_ConsoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		l_ConsoleSink->set_pattern("%^[%T] [%n] %v%$");

		s_CoreLogger = std::make_shared<spdlog::logger>("CORE", l_ConsoleSink);
		s_CoreLogger->set_level(spdlog::level::trace);
		s_CoreLogger->flush_on(spdlog::level::warn);
		spdlog::register_logger(s_CoreLogger);

		s_ClientLogger = std::make_shared<spdlog::logger>("APP", l_ConsoleSink);
		s_ClientLogger->set_level(spdlog::level::trace);
		s_ClientLogger->flush_on(spdlog::level::warn);
		spdlog::register_logger(s_ClientLogger);

#ifdef PT_DEBUG
		s_Level = LogLevel::Trace;
#else
		s_Level = LogLevel::Info;
#endif
		s_Initialized = true;
	}

	void Log::Shutdown()
	{
		if (!s_Initialized)
		{
			return;
		}

		s_ClientLogger.reset();
		s_CoreLogger.reset();
		spdlog::drop_all();

		s_Initialized = false;
	}

	bool Log::IsInitialized()
	{
		return s_Initialized;
	}

	void Log::SetLevel(LogLevel level)
	{
		s_Level = level;
	}

	LogLevel Log::GetLevel()
	{
		return s_Level;
	}

	void Log::Dispatch(LogCategory category, LogLevel level, std::string message)
	{
		const std::shared_ptr<spdlog::logger>& l_Logger = (category == LogCategory::Core) ? s_CoreLogger : s_ClientLogger;

		l_Logger->log(ToBackendLevel(level), message);
	}
}