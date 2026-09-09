#pragma once

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace Engine
{
	enum class LogCategory
	{
		Core,
		Client
	};

	enum class LogLevel
	{
		Trace,
		Info,
		Warn,
		Error,
		Critical,
		Off
	};

	class Log
	{
		Log();
		~Log();

	public:
		static void Initialize();
		static void Shutdown();

		static bool IsInitialized();

		// Messages below this level are discarded
		static void SetLevel(LogLevel level);
		static LogLevel GetLevel();

		template<typename... Arguments>
		static void Write(LogCategory category, LogLevel level, std::format_string<Arguments...> format, Arguments&&... values)
		{
			if (!IsInitialized() || level < GetLevel())
			{
				return;
			}

			Dispatch(category, level, std::format(format, std::forward<Arguments>(values)...));
		}

		static void Write(LogCategory category, LogLevel level, std::string_view message)
		{
			Dispatch(category, level, std::string(message));
		}

	private:
		// Out of line, keeps the logging backend out of the public API
		static void Dispatch(LogCategory category, LogLevel level, std::string message);
	};
}

#define PT_CORE_TRACE(...) ::Engine::Log::Write(::Engine::LogCategory::Core, ::Engine::LogLevel::Trace, __VA_ARGS__)
#define PT_CORE_INFO(...) ::Engine::Log::Write(::Engine::LogCategory::Core, ::Engine::LogLevel::Info, __VA_ARGS__)
#define PT_CORE_WARN(...) ::Engine::Log::Write(::Engine::LogCategory::Core, ::Engine::LogLevel::Warn, __VA_ARGS__)
#define PT_CORE_ERROR(...) ::Engine::Log::Write(::Engine::LogCategory::Core, ::Engine::LogLevel::Error, __VA_ARGS__)
#define PT_CORE_CRITICAL(...) ::Engine::Log::Write(::Engine::LogCategory::Core, ::Engine::LogLevel::Critical, __VA_ARGS__)

#define PT_APP_TRACE(...) ::Engine::Log::Write(::Engine::LogCategory::Client, ::Engine::LogLevel::Trace, __VA_ARGS__)
#define PT_APP_INFO(...) ::Engine::Log::Write(::Engine::LogCategory::Client, ::Engine::LogLevel::Info, __VA_ARGS__)
#define PT_APP_WARN(...) ::Engine::Log::Write(::Engine::LogCategory::Client, ::Engine::LogLevel::Warn, __VA_ARGS__)
#define PT_APP_ERROR(...) ::Engine::Log::Write(::Engine::LogCategory::Client, ::Engine::LogLevel::Error, __VA_ARGS__)
#define PT_APP_CRITICAL(...) ::Engine::Log::Write(::Engine::LogCategory::Client, ::Engine::LogLevel::Critical, __VA_ARGS__)