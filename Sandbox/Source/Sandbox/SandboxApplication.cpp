#include "Engine/Engine.hpp"

#include "Sandbox/SandboxClient.hpp"
#include "Sandbox/SandboxOptions.hpp"

namespace Engine
{
	std::unique_ptr<Application> CreateApplication(int argumentCount, char** arguments)
	{
		PT_APP_INFO("------- CREATING APPLICATION -------");

		Sandbox::SandboxOptions l_Options;
		if (!Sandbox::ParseSandboxOptions(argumentCount, arguments, l_Options))
		{
			return nullptr;
		}

		ApplicationSpecification l_Specification;
		l_Specification.Name = "Pathtracer";
		l_Specification.WindowWidth = 1080;
		l_Specification.WindowHeight = 720;
		l_Specification.WindowResizable = true;

		PT_APP_TRACE("Window Title: {}", l_Specification.Name);
		PT_APP_TRACE("Window Resolution: {}x{}", l_Specification.WindowWidth, l_Specification.WindowHeight);
		PT_APP_TRACE("Window Resizable: {}", l_Specification.WindowResizable);
		PT_APP_TRACE("Scene: {}", l_Options.ScenePath.empty() ? "<demo scene>" : l_Options.ScenePath.string());
		PT_APP_TRACE("Asset root: {}", l_Options.AssetRoot.empty() ? "<next to the executable>" : l_Options.AssetRoot.string());

		auto l_Application = std::make_unique<Application>();
		l_Application->Initialize(l_Specification, std::make_unique<Sandbox::SandboxClient>(std::move(l_Options)));

		if (l_Application->IsInitialized())
		{
			PT_APP_INFO("------- APPLICATION CREATED -------");
		}
		else
		{
			PT_APP_ERROR("------- APPLICATION CREATION FAILED -------");
		}

		return l_Application;
	}
}