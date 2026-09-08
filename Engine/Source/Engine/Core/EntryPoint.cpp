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
			PT_CORE_ERROR("Fatal error: CreateApplication returned null");

			return 1;
		}

		if (l_Application->IsInitialized())
		{
			l_Application->Run();
		}
		else
		{
			PT_CORE_ERROR("Fatal error: application failed to initialize");

			l_ExitCode = 1;
		}
	}
	catch (const std::exception& exception)
	{
		PT_CORE_ERROR("Fatal error: {}", exception.what());

		l_ExitCode = 1;
	}
	catch (...)
	{
		PT_CORE_ERROR("Fatal error: unknown exception");

		l_ExitCode = 1;
	}

	if (l_Application)
	{
		l_Application->Shutdown();
	}

	Engine::Log::Shutdown();

	return l_ExitCode;
}