#include "Engine/Core/Application.hpp"

int main()
{
	Engine::Application application;

	application.Initialize();

	application.Run();

	application.Shutdown();

	return 0;
}