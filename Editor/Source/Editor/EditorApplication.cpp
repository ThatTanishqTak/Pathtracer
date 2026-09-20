#include "Engine/Engine.hpp"

#include "Editor/EditorClient.hpp"
#include "Editor/EditorOptions.hpp"

namespace Engine
{
	std::unique_ptr<Application> CreateApplication(int argumentCount, char** arguments)
	{
		PT_APP_INFO("------- CREATING APPLICATION -------");

		Editor::EditorOptions l_Options;
		if (!Editor::ParseEditorOptions(argumentCount, arguments, l_Options))
		{
			return nullptr;
		}

		// The name is also the layout file's, Editor.layout.ini, so it must differ from the Sandbox's
		ApplicationSpecification l_Specification;
		l_Specification.Name = "Editor";
		l_Specification.WindowWidth = 1600;
		l_Specification.WindowHeight = 900;
		l_Specification.WindowResizable = true;
		l_Specification.EnableUI = true;

		PT_APP_TRACE("Window Title: {}", l_Specification.Name);
		PT_APP_TRACE("Window Resolution: {}x{}", l_Specification.WindowWidth, l_Specification.WindowHeight);
		PT_APP_TRACE("Window Resizable: {}", l_Specification.WindowResizable);
		PT_APP_TRACE("UI: {}", l_Specification.EnableUI);
		PT_APP_TRACE("Scene: {}", l_Options.ScenePath.empty() ? "<empty scene>" : l_Options.ScenePath.string());
		PT_APP_TRACE("Asset root: {}", l_Options.AssetRoot.empty() ? "<next to the executable>" : l_Options.AssetRoot.string());

		auto l_Application = std::make_unique<Application>();
		l_Application->Initialize(l_Specification, std::make_unique<Editor::EditorClient>(std::move(l_Options)));

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