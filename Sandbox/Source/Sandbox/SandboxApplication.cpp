#include "Engine/Engine.hpp"

namespace Engine
{
	std::unique_ptr<Application> CreateApplication(int argumentCount, char** arguments)
	{
		PT_APP_INFO("------- CREATING APPLICATION -------");

		(void)argumentCount;
		(void)arguments;

		ApplicationSpecification l_Specification;
		l_Specification.Name = "Pathtracer";
		l_Specification.WindowWidth = 1920;
		l_Specification.WindowHeight = 1080;
		l_Specification.WindowResizable = true;

		PT_APP_TRACE("Window Title: {}", l_Specification.Name);
		PT_APP_TRACE("Window Resolution: {}x{}", l_Specification.WindowWidth, l_Specification.WindowHeight);
		PT_APP_TRACE("Window Resizable: {}", l_Specification.WindowResizable);

		auto l_Application = std::make_unique<Application>();
		l_Application->Initialize(l_Specification);

		PT_APP_INFO("------- APPLICATION CREATED -------");

		return l_Application;
	}
}