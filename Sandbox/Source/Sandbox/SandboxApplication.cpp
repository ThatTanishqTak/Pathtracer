#include "Engine/Engine.hpp"

namespace Engine
{
	std::unique_ptr<Application> CreateApplication(int argumentCount, char** arguments)
	{
		(void)argumentCount;
		(void)arguments;

		ApplicationSpecification l_Specification;
		l_Specification.Name = "Pathtracer";
		l_Specification.WindowWidth = 1920;
		l_Specification.WindowHeight = 1080;
		l_Specification.WindowResizable = false;

		return std::make_unique<Application>(l_Specification);
	}
}