#include "Engine/Core/Application.hpp"

#include <exception>
#include <iostream>
#include <memory>

int main(int argumentCount, char** arguments)
{
	std::unique_ptr<Engine::Application> l_Application;

	int l_ExitCode = 0;

	try
	{
		l_Application = Engine::CreateApplication(argumentCount, arguments);

		if (!l_Application)
		{
			std::cerr << "Fatal error: CreateApplication returned null\n";

			return 1;
		}

		l_Application->Initialize();

		if (l_Application->IsInitialized())
		{
			l_Application->Run();
		}
		else
		{
			l_ExitCode = 1;
		}
	}
	catch (const std::exception& Exception)
	{
		std::cerr << "Fatal error: " << Exception.what() << '\n';

		l_ExitCode = 1;
	}
	catch (...)
	{
		std::cerr << "Fatal error: unknown exception\n";

		l_ExitCode = 1;
	}

	if (l_Application)
	{
		l_Application->Shutdown();
	}

	return l_ExitCode;
}