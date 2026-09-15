#include "Engine/Core/Application.hpp"

#include "Engine/Core/Log.hpp"

#include <exception>
#include <iostream>
#include <memory>

int main(int argumentCount, char** arguments)
{
	std::unique_ptr<Engine::Application> l_Application;

	int l_ExitCode = 0;

	try
	{
		Engine::Log::Initialize();

		l_Application = Engine::CreateApplication(argumentCount, arguments);

		if (!l_Application)
		{
			PT_CORE_CRITICAL("CreateApplication returned null");

			l_ExitCode = 1;
		}
		else if (l_Application->IsInitialized())
		{
			l_Application->Run();
		}
		else
		{
			PT_CORE_CRITICAL("Application failed to initialize");

			l_ExitCode = 1;
		}
	}
	catch (const std::exception& exception)
	{
		if (Engine::Log::IsInitialized())
		{
			PT_CORE_CRITICAL("Fatal error: {}", exception.what());
		}
		else
		{
			std::cerr << "Fatal error: " << exception.what() << '\n';
		}

		l_ExitCode = 1;
	}
	catch (...)
	{
		if (Engine::Log::IsInitialized())
		{
			PT_CORE_CRITICAL("Unknown exception");
		}
		else
		{
			std::cerr << "Unknown exception\n";
		}

		l_ExitCode = 1;
	}

	if (l_Application)
	{
		l_Application->Shutdown();
		l_Application.reset();
	}

	Engine::Log::Shutdown();

	return l_ExitCode;
}