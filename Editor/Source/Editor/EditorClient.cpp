#include "Editor/EditorClient.hpp"

#include "Editor/EditorCommands.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <system_error>
#include <utility>

namespace Editor
{
	namespace
	{
		// Created spheres and quads cycle through these so the first few objects tell apart at a glance
		constexpr std::array<Engine::Math::Vector3, 4> k_Palette
		{
			Engine::Math::Vector3(0.8f, 0.3f, 0.25f),
			Engine::Math::Vector3(0.3f, 0.7f, 0.35f),
			Engine::Math::Vector3(0.3f, 0.45f, 0.85f),
			Engine::Math::Vector3(0.85f, 0.75f, 0.3f),
		};

		constexpr const char* k_SceneFilterName = "Scene files";
		constexpr const char* k_SceneFilterPattern = "json";

		// Built next to the Editor: both executables share the output directory with Shaders/ and Assets/
#ifdef _WIN32
		constexpr const char* k_SandboxExecutable = "Sandbox.exe";
#else
		constexpr const char* k_SandboxExecutable = "Sandbox";
#endif
	}

	EditorClient::EditorClient(EditorOptions options) : m_Options(std::move(options))
	{

	}

	void EditorClient::OnStart(Engine::ApplicationServices& services)
	{
		m_Services = &services;

		PT_APP_INFO("Editor client started, {}x{} window, {}x{} framebuffer, UI {}", m_Services->GetWindowWidth(), m_Services->GetWindowHeight(), m_Services->GetFramebufferWidth(), m_Services->GetFramebufferHeight(), m_Services->IsUIEnabled() ? "enabled" : "disabled");
		PT_APP_INFO("Viewport: hold the right mouse button over it to look around, W A S D fly, Q and E move down and up, Shift is faster, the keys work while the cursor is over the viewport or it has the focus");
		PT_APP_INFO("Viewport: left click selects what is under the cursor and the background clears the selection, 1 2 3 switch the gizmo to translate, rotate and scale, 4 hides it, Ctrl snaps while dragging a handle");
		PT_APP_INFO("Panels: Scene lists the entities, creates, renames, duplicates and deletes them, Inspector edits the selection or the scene settings, Render Settings edits the mode, bounce limit, render scale, seed and exposure");
		PT_APP_INFO("Keys: Ctrl+Z undo, Ctrl+Y redo, Ctrl+D duplicate, Delete, Ctrl+N new, Ctrl+O open, Ctrl+S save, Ctrl+Shift+S save as, Ctrl+P opens the saved scene in the Sandbox");

		if (!m_Services->IsUIEnabled())
		{
			PT_APP_WARN("The UI is disabled, the Editor has no panels and no viewport without it");
		}

		// Content is anchored at the executable, not at wherever the process was started from, so a shortcut or a debugger with another working directory finds the same files
		if (m_Options.AssetRoot.empty())
		{
			m_AssetRoot = m_Services->GetExecutableDirectory() / "Assets";
		}
		else
		{
			std::error_code l_Error;
			m_AssetRoot = std::filesystem::absolute(m_Options.AssetRoot, l_Error);
			if (l_Error)
			{
				PT_APP_WARN("Cannot resolve the asset root {}: {}, using it as given", m_Options.AssetRoot.string(), l_Error.message());
				m_AssetRoot = m_Options.AssetRoot;
			}
		}

		PT_APP_INFO("Asset root: {}", m_AssetRoot.string());

		// The built-in meshes are loaded up front, so the Inspector's mesh list is never empty and Create finds them by source
		m_Assets.SetAssetRoot(m_AssetRoot);
		m_Assets.LoadMesh(Engine::AssetManager::k_CubeSource);
		m_Assets.LoadMesh(Engine::AssetManager::k_IcosphereSource);

		m_RenderSettings.Integrator.SamplesPerFrame = 1;
		m_RenderSettings.Integrator.MaxBounces = 4;
		m_RenderSettings.Integrator.Seed = 0;

		// The lens first, loading places the camera at the spawn
		m_Camera.SetVerticalFieldOfView(k_VerticalFieldOfView);

		// A scene from the command line, or an empty scene when there is none or it fails. The Editor builds no demo content, that is what the scene file and the Create menu are for
		if (m_Options.ScenePath.empty() || !LoadScene(ResolveScenePath(m_Options.ScenePath)))
		{
			if (!m_Options.ScenePath.empty())
			{
				PT_APP_ERROR("Starting with an empty scene instead of the file that failed");
			}

			BuildEmptyScene();
		}
	}

	void EditorClient::OnStop() noexcept
	{
		PT_APP_TRACE("Editor client stopped");

		m_Services = nullptr;
	}

	void EditorClient::OnEvent(const Engine::InputEvent& event)
	{
		// The UI backend sees the raw events through the host's hook and the camera reads the input snapshot in Update, so only the events worth a trace are handled here
		switch (event.Type)
		{
			case Engine::InputEventType::FocusLost:
			{
				// The host already released held keys and mouse capture before this arrives
				PT_APP_TRACE("Focus lost");
				break;
			}
			case Engine::InputEventType::WindowResized:
			{
				PT_APP_TRACE("Framebuffer resized to {}x{}", static_cast<int>(event.X), static_cast<int>(event.Y));
				break;
			}
			default:
			{
				break;
			}
		}
	}

	bool EditorClient::OnCloseRequested()
	{
		// Nothing to lose, or already answered: the loop may end
		if (m_ExitConfirmed || !m_History.IsDirty() || m_Services == nullptr || !m_Services->IsUIEnabled())
		{
			return true;
		}

		// The prompt opens in the next BuildUI, a second close gesture while it is up changes nothing
		if (!m_PromptOpen && m_PendingAction == PendingAction::None)
		{
			m_PendingAction = PendingAction::Exit;
			m_OpenPromptRequested = true;
		}

		return false;
	}

	void EditorClient::BuildUI()
	{
		ImGuiIO& l_IO = ImGui::GetIO();

		// While the viewport holds the mouse in relative mode the cursor position is frozen, NoMouse keeps that position from hovering or clicking a panel. The next NewFrame reads the flag, one frame late is fine because the position does not move while captured
		const bool l_Captured = m_Services->IsMouseCaptured();
		if (l_Captured)
		{
			l_IO.ConfigFlags |= ImGuiConfigFlags_NoMouse;
		}
		else
		{
			l_IO.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
		}

		// The menu bar takes its strip off the work area, the dockspace below fills what is left
		DrawMainMenuBar();

		// Docking inside the one native window, the panels dock into this space and the swapchain is cleared underneath
		const ImGuiID l_DockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

		if (!m_LayoutChecked)
		{
			m_LayoutChecked = true;

			// Settings were loaded by the first NewFrame. A Viewport entry means a layout file exists and wins, only a fresh workspace gets the default arrangement
			if (ImGui::FindWindowSettingsByID(ImHashStr("Viewport")) == nullptr)
			{
				BuildDefaultLayout(l_DockspaceId);
			}
		}

		// The texture id is the previous frame's, the retired display texture keeps it valid across a resize. The gizmo edits the selection live and records its command through the history, like an Inspector drag
		m_ViewportPanel.Draw(m_Services->GetViewTextureId(), l_Captured, m_Camera.GetCamera(), m_Scene, m_Assets, m_SelectedEntity, m_History);
		m_SceneHierarchyPanel.Draw(m_Scene, m_SelectedEntity, *this, m_History.IsDirty(), m_ScenePath, m_FileStatus);
		m_InspectorPanel.Draw(m_Scene, m_Assets, m_SelectedEntity, m_History, *this);

		const Engine::RenderRequest l_Request = GetRenderRequest();
		m_RenderSettingsPanel.Draw(m_RenderSettings, l_Request.View.Width, l_Request.View.Height);

		DrawUnsavedChangesPrompt();
	}

	void EditorClient::DrawMainMenuBar()
	{
		// A native dialog or the prompt holds every action, so nothing runs twice or behind the question
		const bool l_Blocked = m_Services->IsFileDialogOpen() || m_PromptOpen;
		const bool l_HasSelection = m_Scene.FindEntity(m_SelectedEntity) != nullptr;

		if (!ImGui::BeginMainMenuBar())
		{
			return;
		}

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New", "Ctrl+N", false, !l_Blocked))
			{
				RequestNewScene();
			}

			if (ImGui::MenuItem("Open...", "Ctrl+O", false, !l_Blocked))
			{
				RequestOpenScene();
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Save", "Ctrl+S", false, !l_Blocked))
			{
				SaveScene();
			}

			if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, !l_Blocked))
			{
				ShowSaveAsDialog();
			}

			ImGui::Separator();

			// The Sandbox reads the file, so the item opens what is saved and asks first when the scene is ahead of it. One Sandbox at a time
			const bool l_SandboxRunning = m_Services->IsProcessRunning();
			if (ImGui::MenuItem(l_SandboxRunning ? "Sandbox is running" : "Open in Sandbox", "Ctrl+P", false, !l_Blocked && !l_SandboxRunning))
			{
				RequestLaunchSandbox();
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Exit", nullptr, false, !l_Blocked))
			{
				RequestExit();
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit"))
		{
			const std::string l_UndoLabel = m_History.CanUndo() ? std::format("Undo {}", m_History.GetUndoName()) : std::string("Undo");
			if (ImGui::MenuItem(l_UndoLabel.c_str(), "Ctrl+Z", false, m_History.CanUndo() && !l_Blocked))
			{
				Undo();
			}

			const std::string l_RedoLabel = m_History.CanRedo() ? std::format("Redo {}", m_History.GetRedoName()) : std::string("Redo");
			if (ImGui::MenuItem(l_RedoLabel.c_str(), "Ctrl+Y", false, m_History.CanRedo() && !l_Blocked))
			{
				Redo();
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, l_HasSelection && !l_Blocked))
			{
				DuplicateEntity(m_SelectedEntity);
			}

			if (ImGui::MenuItem("Delete", "Del", false, l_HasSelection && !l_Blocked))
			{
				DeleteEntity(m_SelectedEntity);
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Create", !l_Blocked))
		{
			DrawCreateMenuItems(*this);
			ImGui::EndMenu();
		}

		// The scene and its dirty marker at the right end of the bar
		const std::string l_Title = std::format("{}{}", m_Scene.GetName(), m_History.IsDirty() ? "*" : "");
		const float l_TitleWidth = ImGui::CalcTextSize(l_Title.c_str()).x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
		ImGui::SameLine(std::max(ImGui::GetWindowWidth() - l_TitleWidth, ImGui::GetCursorPosX()));
		ImGui::TextDisabled("%s", l_Title.c_str());

		if (!l_Blocked)
		{
			HandleShortcuts();
		}

		ImGui::EndMainMenuBar();
	}

	void EditorClient::HandleShortcuts()
	{
		// A text field keeps its own keys, and a widget or a gizmo handle being dragged must finish its one undo step before anything changes the scene under it
		if (ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive() || m_ViewportPanel.GetState().GizmoActive)
		{
			return;
		}

		constexpr ImGuiInputFlags k_Flags = ImGuiInputFlags_RouteGlobal;

		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, k_Flags))
		{
			RequestNewScene();
		}

		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, k_Flags))
		{
			RequestOpenScene();
		}

		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, k_Flags))
		{
			ShowSaveAsDialog();
		}

		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, k_Flags))
		{
			SaveScene();
		}

		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_P, k_Flags))
		{
			RequestLaunchSandbox();
		}

		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, k_Flags | ImGuiInputFlags_Repeat))
		{
			Undo();
		}

		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, k_Flags | ImGuiInputFlags_Repeat) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, k_Flags | ImGuiInputFlags_Repeat))
		{
			Redo();
		}

		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D, k_Flags))
		{
			DuplicateEntity(m_SelectedEntity);
		}

		if (ImGui::Shortcut(ImGuiKey_Delete, k_Flags))
		{
			DeleteEntity(m_SelectedEntity);
		}
	}

	void EditorClient::DrawUnsavedChangesPrompt()
	{
		if (m_OpenPromptRequested)
		{
			ImGui::OpenPopup("Unsaved changes");
			m_OpenPromptRequested = false;
		}

		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

		m_PromptOpen = false;
		if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			return;
		}

		m_PromptOpen = true;

		const char* l_Verb = "closing";
		switch (m_PendingAction)
		{
			case PendingAction::NewScene:
			{
				l_Verb = "starting a new scene";
				break;
			}
			case PendingAction::OpenScene:
			{
				l_Verb = "opening another scene";
				break;
			}
			case PendingAction::LaunchSandbox:
			{
				l_Verb = "opening it in the Sandbox";
				break;
			}
			default:
			{
				break;
			}
		}

		// The launch has its own third answer: the Sandbox reads the file, so declining the save opens the scene as it was last saved
		const bool l_Launch = m_PendingAction == PendingAction::LaunchSandbox;

		ImGui::Text("Save the changes to '%s' before %s?", m_Scene.GetName().c_str(), l_Verb);
		if (l_Launch)
		{
			ImGui::TextDisabled("The Sandbox reads the file, launching without saving opens it as last saved");
		}

		ImGui::Separator();

		if (ImGui::Button(l_Launch ? "Save and launch" : "Save"))
		{
			ImGui::CloseCurrentPopup();

			// The pending action follows a successful save, which may first need a Save As dialog
			m_RunPendingAfterSave = true;
			SaveScene();
		}

		ImGui::SameLine();

		if (ImGui::Button(l_Launch ? "Launch last saved" : "Don't save"))
		{
			ImGui::CloseCurrentPopup();
			RunPendingAction();
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel"))
		{
			ImGui::CloseCurrentPopup();
			m_PendingAction = PendingAction::None;
		}

		ImGui::EndPopup();
	}

	void EditorClient::BuildDefaultLayout(uint32_t dockspaceId)
	{
		// The node DockSpaceOverViewport created this frame is rebuilt: the viewport takes the centre, the scene list the left, the inspector and the render settings share the right
		ImGui::DockBuilderRemoveNode(dockspaceId);
		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

		ImGuiID l_Centre = dockspaceId;
		ImGuiID l_Left = 0;
		ImGuiID l_Right = 0;
		ImGuiID l_RightBottom = 0;

		ImGui::DockBuilderSplitNode(l_Centre, ImGuiDir_Left, 0.2f, &l_Left, &l_Centre);
		ImGui::DockBuilderSplitNode(l_Centre, ImGuiDir_Right, 0.25f, &l_Right, &l_Centre);
		ImGui::DockBuilderSplitNode(l_Right, ImGuiDir_Down, 0.35f, &l_RightBottom, &l_Right);

		ImGui::DockBuilderDockWindow("Scene", l_Left);
		ImGui::DockBuilderDockWindow("Inspector", l_Right);
		ImGui::DockBuilderDockWindow("Render Settings", l_RightBottom);
		ImGui::DockBuilderDockWindow("Viewport", l_Centre);
		ImGui::DockBuilderFinish(dockspaceId);

		PT_APP_INFO("Default panel layout built, the layout file records every change from here on");
	}

	void EditorClient::Update(const Engine::FrameTime& time, const Engine::InputState& input)
	{
		// A finished dialog first, so a loaded scene renders in this frame, then the Sandbox's exit so the menu item frees up in the same frame
		PollFileDialog();
		PollSandbox();

		const ViewportState& l_Viewport = m_ViewportPanel.GetState();

		// The snapshot says what the frame started with. SetMouseCaptured updates the same state at once, so the look gate is taken before the gesture runs: the press frame's cursor travel is never a turn, and the release frame's relative motion still is
		const bool l_CapturedAtStart = input.MouseCaptured;

		// The look gesture: a right press over the viewport takes the mouse into relative mode through the services, letting go of the button gives it back. The host drops it on focus loss by itself, Escape is the second way out
		if (l_CapturedAtStart)
		{
			if (!input.IsMouseButtonDown(Engine::MouseButton::Right) || input.WasKeyPressed(Engine::Key::Escape))
			{
				m_Services->SetMouseCaptured(false);
			}
		}
		else if (l_Viewport.Hovered && input.WasMouseButtonPressed(Engine::MouseButton::Right))
		{
			m_Services->SetMouseCaptured(true);
		}

		// Look while captured, fly while the viewport owns the keyboard as BuildUI decided. Panel interaction reaches neither
		m_Camera.Update(time, input, l_CapturedAtStart, l_Viewport.KeyboardOwned);

		// The press that starts the look gesture is never a pick, and the frozen cursor of a captured frame points at nothing
		if (!l_CapturedAtStart)
		{
			if (l_Viewport.PickReady && input.WasMouseButtonPressed(Engine::MouseButton::Left))
			{
				PickEntity(input.MouseX, input.MouseY);
			}

			// The gizmo keys take the camera's route: the viewport's keys while it owns the keyboard, which a drag in it never does
			if (l_Viewport.KeyboardOwned && !l_Viewport.GizmoActive)
			{
				if (input.WasKeyPressed(Engine::Key::Number1))
				{
					m_ViewportPanel.SetGizmoOperation(GizmoOperation::Translate);
				}

				if (input.WasKeyPressed(Engine::Key::Number2))
				{
					m_ViewportPanel.SetGizmoOperation(GizmoOperation::Rotate);
				}

				if (input.WasKeyPressed(Engine::Key::Number3))
				{
					m_ViewportPanel.SetGizmoOperation(GizmoOperation::Scale);
				}

				if (input.WasKeyPressed(Engine::Key::Number4))
				{
					m_ViewportPanel.SetGizmoOperation(GizmoOperation::None);
				}
			}
		}

		m_StatisticsElapsed += time.ElapsedSeconds;
		m_StatisticsFrames += 1;

		if (m_StatisticsElapsed >= 1.0f)
		{
			const float l_AverageMilliseconds = (m_StatisticsElapsed / static_cast<float>(m_StatisticsFrames)) * 1000.0f;

			const Engine::RenderRequest l_Request = GetRenderRequest();

			const Engine::Camera& l_Camera = m_Camera.GetCamera();
			const Engine::YawPitch& l_Angles = m_Camera.GetAngles();

			PT_APP_TRACE("Frame {}: {} frames in {:.2f}s, {:.2f} ms average, focus {}, capture {}, viewport hovered {} focused {}, camera ({:.2f}, {:.2f}, {:.2f}) yaw {:.1f} pitch {:.1f}, view {}x{} at scale {:.2f}, {} bounces, {} entities, {} materials, scene revision {}, selected {}, {} to undo, {} to redo, {}", time.FrameIndex, m_StatisticsFrames, m_StatisticsElapsed, l_AverageMilliseconds, input.HasFocus, input.MouseCaptured, l_Viewport.Hovered, l_Viewport.Focused, l_Camera.Position.x, l_Camera.Position.y, l_Camera.Position.z, Engine::Math::ToDegrees(l_Angles.Yaw), Engine::Math::ToDegrees(l_Angles.Pitch), l_Request.View.Width, l_Request.View.Height, m_RenderSettings.RenderScale, m_RenderSettings.Integrator.MaxBounces, m_Scene.GetEntities().size(), m_Scene.GetMaterials().size(), m_Scene.GetRadianceRevision(), std::to_underlying(m_SelectedEntity), m_History.GetUndoCount(), m_History.GetRedoCount(), m_History.IsDirty() ? "unsaved changes" : "clean");

			m_StatisticsElapsed = 0.0f;
			m_StatisticsFrames = 0;
		}
	}

	void EditorClient::PollFileDialog()
	{
		const std::optional<Engine::FileDialogResult> l_Result = m_Services->PollFileDialog();
		if (!l_Result)
		{
			return;
		}

		if (!l_Result->Error.empty())
		{
			Engine::SceneFileResult l_Failure;
			l_Failure.Error = l_Result->Error;
			SetFileStatus("The file dialog could not be shown", l_Failure);

			PT_APP_ERROR("File dialog failed: {}", l_Result->Error);

			m_PendingAction = PendingAction::None;
			m_RunPendingAfterSave = false;

			return;
		}

		if (!l_Result->Accepted)
		{
			PT_APP_TRACE("File dialog cancelled");

			m_PendingAction = PendingAction::None;
			m_RunPendingAfterSave = false;

			return;
		}

		if (l_Result->Kind == Engine::FileDialogKind::Open)
		{
			LoadScene(l_Result->Path);

			return;
		}

		const bool l_Saved = SaveSceneTo(NormalizeScenePath(l_Result->Path));
		if (l_Saved && m_RunPendingAfterSave)
		{
			RunPendingAction();
		}
		else if (!l_Saved)
		{
			m_PendingAction = PendingAction::None;
		}

		m_RunPendingAfterSave = false;
	}

	void EditorClient::RequestNewScene()
	{
		if (m_History.IsDirty())
		{
			m_PendingAction = PendingAction::NewScene;
			m_OpenPromptRequested = true;

			return;
		}

		NewScene();
	}

	void EditorClient::RequestOpenScene()
	{
		if (m_History.IsDirty())
		{
			m_PendingAction = PendingAction::OpenScene;
			m_OpenPromptRequested = true;

			return;
		}

		ShowOpenDialog();
	}

	void EditorClient::RequestLaunchSandbox()
	{
		if (m_Services->IsProcessRunning())
		{
			PT_APP_WARN("The Sandbox is still running, close it before opening the scene in it again");

			return;
		}

		// Never saved: the Sandbox reads a file, so a Save As comes first and the launch follows a successful save. Cancelling the dialog cancels the launch
		if (m_ScenePath.empty())
		{
			m_PendingAction = PendingAction::LaunchSandbox;
			m_RunPendingAfterSave = true;
			SaveScene();

			return;
		}

		// Saved before but changed since: save first or open the file as last saved, the prompt asks
		if (m_History.IsDirty())
		{
			m_PendingAction = PendingAction::LaunchSandbox;
			m_OpenPromptRequested = true;

			return;
		}

		LaunchSandbox();
	}

	void EditorClient::RequestExit()
	{
		if (m_History.IsDirty())
		{
			m_PendingAction = PendingAction::Exit;
			m_OpenPromptRequested = true;

			return;
		}

		m_ExitConfirmed = true;
		m_Services->RequestClose();
	}

	void EditorClient::RunPendingAction()
	{
		const PendingAction l_Action = m_PendingAction;
		m_PendingAction = PendingAction::None;

		switch (l_Action)
		{
			case PendingAction::NewScene:
			{
				NewScene();
				break;
			}
			case PendingAction::OpenScene:
			{
				ShowOpenDialog();
				break;
			}
			case PendingAction::LaunchSandbox:
			{
				LaunchSandbox();
				break;
			}
			case PendingAction::Exit:
			{
				m_ExitConfirmed = true;
				m_Services->RequestClose();
				break;
			}
			default:
			{
				break;
			}
		}
	}

	void EditorClient::NewScene()
	{
		BuildEmptyScene();
		m_FileStatus = SceneFileStatus{};
	}

	void EditorClient::ShowOpenDialog()
	{
		// Starts in the scene folder when there is one, the asset root otherwise
		Engine::FileDialogRequest l_Request;
		l_Request.Kind = Engine::FileDialogKind::Open;
		l_Request.FilterName = k_SceneFilterName;
		l_Request.FilterPattern = k_SceneFilterPattern;

		std::error_code l_Error;
		const std::filesystem::path l_Scenes = m_AssetRoot / "Scenes";
		l_Request.DefaultLocation = std::filesystem::is_directory(l_Scenes, l_Error) ? l_Scenes : m_AssetRoot;

		if (!m_Services->ShowFileDialog(l_Request))
		{
			PT_APP_WARN("Cannot show the open dialog, another dialog is still open");
			m_PendingAction = PendingAction::None;
		}
	}

	void EditorClient::ShowSaveAsDialog()
	{
		// Starts at the current file, or at a name from the scene inside the scene folder
		Engine::FileDialogRequest l_Request;
		l_Request.Kind = Engine::FileDialogKind::Save;
		l_Request.FilterName = k_SceneFilterName;
		l_Request.FilterPattern = k_SceneFilterPattern;

		if (!m_ScenePath.empty())
		{
			l_Request.DefaultLocation = m_ScenePath;
		}
		else
		{
			const std::string l_Name = m_Scene.GetName().empty() ? std::string("Untitled") : m_Scene.GetName();
			l_Request.DefaultLocation = NormalizeScenePath(m_AssetRoot / "Scenes" / l_Name);
		}

		if (!m_Services->ShowFileDialog(l_Request))
		{
			PT_APP_WARN("Cannot show the save dialog, another dialog is still open");
			m_PendingAction = PendingAction::None;
			m_RunPendingAfterSave = false;
		}
	}

	void EditorClient::SaveScene()
	{
		// No path yet is a Save As, the pending action then waits for the dialog
		if (m_ScenePath.empty())
		{
			ShowSaveAsDialog();

			return;
		}

		const bool l_Saved = SaveSceneTo(m_ScenePath);
		if (l_Saved && m_RunPendingAfterSave)
		{
			RunPendingAction();
		}
		else if (!l_Saved)
		{
			// A failed save leaves the prompted action unrun, the status line says why
			m_PendingAction = PendingAction::None;
		}

		m_RunPendingAfterSave = false;
	}

	void EditorClient::LaunchSandbox()
	{
		// The Sandbox opens the file as last saved, so it must exist; the absolute form is what its ResolveScenePath takes as given, wherever the process starts from
		std::error_code l_Error;
		const std::filesystem::path l_ScenePath = std::filesystem::absolute(m_ScenePath, l_Error);
		if (l_Error || !std::filesystem::is_regular_file(l_ScenePath, l_Error))
		{
			Engine::SceneFileResult l_Failure;
			l_Failure.Error = l_Error ? l_Error.message() : "the file does not exist, save the scene first";
			SetFileStatus(std::format("Could not open {} in the Sandbox", m_ScenePath.string()), l_Failure);

			PT_APP_ERROR("Sandbox launch failed for {}: {}", m_ScenePath.string(), l_Failure.Error);

			return;
		}

		// The same asset root the Editor resolved, so a relative path inside the scene means the same file in both applications
		Engine::ProcessLaunchRequest l_Request;
		l_Request.Executable = m_Services->GetExecutableDirectory() / k_SandboxExecutable;
		l_Request.Arguments = { "--scene", l_ScenePath, "--assets", m_AssetRoot };

		if (!std::filesystem::is_regular_file(l_Request.Executable, l_Error))
		{
			Engine::SceneFileResult l_Failure;
			l_Failure.Error = std::format("{} was not found, build the Sandbox target", l_Request.Executable.string());
			SetFileStatus("Could not start the Sandbox", l_Failure);

			PT_APP_ERROR("Sandbox launch failed: {}", l_Failure.Error);

			return;
		}

		const Engine::ProcessLaunchResult l_Result = m_Services->LaunchProcess(l_Request);
		if (!l_Result.Started)
		{
			Engine::SceneFileResult l_Failure;
			l_Failure.Error = l_Result.Error;
			SetFileStatus("Could not start the Sandbox", l_Failure);

			PT_APP_ERROR("Sandbox launch failed: {}", l_Result.Error);

			return;
		}

		Engine::SceneFileResult l_Success;
		l_Success.Succeeded = true;
		SetFileStatus(std::format("Opened {} in the Sandbox", l_ScenePath.string()), l_Success);

		PT_APP_INFO("Sandbox started on {} with the asset root {}", l_ScenePath.string(), m_AssetRoot.string());
	}

	void EditorClient::PollSandbox()
	{
		const std::optional<Engine::ProcessExit> l_Exit = m_Services->PollProcess();
		if (!l_Exit)
		{
			return;
		}

		if (l_Exit->ExitCode == 0)
		{
			PT_APP_INFO("Sandbox exited");

			return;
		}

		// A non-zero code is the Sandbox's fatal path, its own log above says why
		Engine::SceneFileResult l_Failure;
		l_Failure.Error = std::format("exit code {}", l_Exit->ExitCode);
		SetFileStatus("The Sandbox exited with an error", l_Failure);

		PT_APP_WARN("Sandbox exited with code {}", l_Exit->ExitCode);
	}

	void EditorClient::Undo()
	{
		if (m_History.Undo(m_Scene))
		{
			// A restored entity is worth looking at, a removed one leaves nothing selected through the hierarchy's rule
			PT_APP_INFO("Undo, {} left", m_History.GetUndoCount());
		}
	}

	void EditorClient::Redo()
	{
		if (m_History.Redo(m_Scene))
		{
			PT_APP_INFO("Redo, {} left", m_History.GetRedoCount());
		}
	}

	void EditorClient::PickEntity(float mouseX, float mouseY)
	{
		const ViewportState& l_Viewport = m_ViewportPanel.GetState();
		if (l_Viewport.ImageWidth <= 0.0f || l_Viewport.ImageHeight <= 0.0f || l_Viewport.ContentWidth == 0 || l_Viewport.ContentHeight == 0)
		{
			return;
		}

		// Window coordinates to the view's own pixels, so the ray comes from the same camera and extent the image was rendered with. Unjittered, the CPU twin of the shader's ray, so the pick does not move with the seed
		const Engine::Camera l_Camera = m_ViewportPanel.GetViewCamera(m_Camera.GetCamera());
		const float l_PixelX = (mouseX - l_Viewport.ImageX) * static_cast<float>(l_Viewport.ContentWidth) / l_Viewport.ImageWidth;
		const float l_PixelY = (mouseY - l_Viewport.ImageY) * static_cast<float>(l_Viewport.ContentHeight) / l_Viewport.ImageHeight;

		const Engine::Ray l_Ray = Engine::GenerateCameraRay(l_Camera, l_PixelX, l_PixelY);
		const Engine::ScenePick l_Pick = Engine::PickClosest(m_Scene, &m_Assets, l_Ray);

		// Selection is workspace state: not a command, nothing is dirtied, and the background clears it
		m_SelectedEntity = l_Pick.Entity;

		if (const Engine::Entity* l_Entity = m_Scene.FindEntity(l_Pick.Entity); l_Entity != nullptr)
		{
			PT_APP_TRACE("Picked '{}' ({}) at {:.2f} m through pixel ({:.0f}, {:.0f})", l_Entity->Name, std::to_underlying(l_Entity->Id), l_Pick.Distance, l_PixelX, l_PixelY);
		}
		else
		{
			PT_APP_TRACE("Picked nothing through pixel ({:.0f}, {:.0f}), selection cleared", l_PixelX, l_PixelY);
		}
	}

	// EditorActions --------------

	void EditorClient::CreateEntity(CreateEntityKind kind)
	{
		m_CreatedEntities += 1;

		const Engine::Math::Vector3 l_Position = GetCreatePosition();

		Engine::Entity l_Entity;
		Engine::Material l_Material;

		switch (kind)
		{
			case CreateEntityKind::Sphere:
			{
				l_Entity.Name = std::format("Sphere {}", m_CreatedEntities);
				l_Entity.Geometry.Type = Engine::GeometryType::Sphere;
				l_Entity.Geometry.Radius = 0.5f;
				l_Entity.Transform.Translation = l_Position;

				l_Material.BaseColor = k_Palette[m_CreatedEntities % k_Palette.size()];
				break;
			}
			case CreateEntityKind::Quad:
			{
				// Lying flat, a quad's normal is object +Z and -90 degrees about X turns it up
				l_Entity.Name = std::format("Quad {}", m_CreatedEntities);
				l_Entity.Geometry.Type = Engine::GeometryType::Quad;
				l_Entity.Geometry.Width = 2.0f;
				l_Entity.Geometry.Height = 2.0f;
				l_Entity.Transform.Translation = l_Position;
				l_Entity.Transform.Rotation = glm::angleAxis(Engine::Math::ToRadians(-90.0f), Engine::Math::k_Right);

				l_Material.BaseColor = k_Palette[m_CreatedEntities % k_Palette.size()];
				break;
			}
			case CreateEntityKind::Cube:
			case CreateEntityKind::Icosphere:
			{
				// The built-in mesh by source, the same Id every time so every cube shares one mesh. Both fit the unit cube, the transform's scale sizes them
				const bool l_Cube = kind == CreateEntityKind::Cube;

				l_Entity.Name = std::format("{} {}", l_Cube ? "Cube" : "Icosphere", m_CreatedEntities);
				l_Entity.Geometry.Type = Engine::GeometryType::Mesh;
				l_Entity.Geometry.Mesh = m_Assets.LoadMesh(l_Cube ? Engine::AssetManager::k_CubeSource : Engine::AssetManager::k_IcosphereSource);
				l_Entity.Transform.Translation = l_Position;

				l_Material.BaseColor = k_Palette[m_CreatedEntities % k_Palette.size()];
				break;
			}
			case CreateEntityKind::AreaLight:
			{
				// Emissive geometry above the point ahead, facing down: +90 degrees about X turns object +Z into -Y. The radiance lives on the material and nowhere else
				l_Entity.Name = std::format("Area light {}", m_CreatedEntities);
				l_Entity.Geometry.Type = Engine::GeometryType::Quad;
				l_Entity.Geometry.Width = 2.0f;
				l_Entity.Geometry.Height = 2.0f;
				l_Entity.Transform.Translation = l_Position + Engine::Math::k_Up * k_AreaLightHeight;
				l_Entity.Transform.Rotation = glm::angleAxis(Engine::Math::ToRadians(90.0f), Engine::Math::k_Right);

				l_Material.Type = Engine::MaterialType::Emissive;
				l_Material.BaseColor = Engine::Math::Vector3(0.0f, 0.0f, 0.0f);
				l_Material.EmissionColor = Engine::Math::Vector3(1.0f, 0.95f, 0.9f);
				l_Material.EmissionStrength = 8.0f;
				break;
			}
		}

		l_Material.Name = std::format("{} material", l_Entity.Name);

		auto l_Command = std::make_unique<CreateEntityCommand>(std::format("Create '{}'", l_Entity.Name), std::move(l_Entity), std::move(l_Material));
		const CreateEntityCommand* l_Created = l_Command.get();

		m_History.Execute(std::move(l_Command), m_Scene);
		m_SelectedEntity = l_Created->GetEntityId();
	}

	void EditorClient::RenameEntity(Engine::EntityId id, std::string name)
	{
		const Engine::Entity* l_Entity = m_Scene.FindEntity(id);
		if (l_Entity == nullptr || name.empty() || name == l_Entity->Name)
		{
			return;
		}

		Engine::Entity l_After = *l_Entity;
		l_After.Name = std::move(name);

		m_History.Execute(std::make_unique<EditEntityCommand>(std::format("Rename '{}'", l_Entity->Name), *l_Entity, std::move(l_After)), m_Scene);
	}

	void EditorClient::DuplicateEntity(Engine::EntityId id)
	{
		std::unique_ptr<CreateEntityCommand> l_Command = MakeDuplicateCommand(m_Scene, id);
		if (!l_Command)
		{
			return;
		}

		const CreateEntityCommand* l_Created = l_Command.get();

		m_History.Execute(std::move(l_Command), m_Scene);
		m_SelectedEntity = l_Created->GetEntityId();
	}

	void EditorClient::DeleteEntity(Engine::EntityId id)
	{
		if (m_Scene.FindEntity(id) == nullptr)
		{
			return;
		}

		m_History.Execute(std::make_unique<DeleteEntityCommand>(m_Scene, id), m_Scene);

		if (m_SelectedEntity == id)
		{
			m_SelectedEntity = Engine::EntityId::Invalid;
		}
	}

	void EditorClient::PlaceSpawnAtCamera()
	{
		// The controller's orientation is yaw and pitch only, which is what the Sandbox's controller reads back
		const Engine::Camera& l_Camera = m_Camera.GetCamera();

		const SceneSettings l_Before = GetSceneSettings(m_Scene);
		SceneSettings l_After = l_Before;
		l_After.Spawn.Position = l_Camera.Position;
		l_After.Spawn.Orientation = l_Camera.Orientation;

		m_History.Execute(std::make_unique<EditSceneSettingsCommand>("Place spawn at camera", l_Before, std::move(l_After)), m_Scene);
	}

	// Files --------------

	std::filesystem::path EditorClient::ResolveScenePath(const std::filesystem::path& path) const
	{
		if (path.is_absolute())
		{
			return path;
		}

		return m_AssetRoot / path;
	}

	std::filesystem::path EditorClient::NormalizeScenePath(std::filesystem::path path) const
	{
		// "Room.json" and "Room.scene.json" both stay, "Room" becomes "Room.scene.json"
		if (path.extension() == ".json")
		{
			return path;
		}

		path += std::string(Engine::SceneSerializer::k_Extension);

		return path;
	}

	bool EditorClient::LoadScene(const std::filesystem::path& path)
	{
		// The serializer already logged every warning and the error with its field context, the panel shows them again
		const Engine::SceneFileResult l_Result = Engine::SceneSerializer::Load(path, m_Scene, m_Assets);
		if (!l_Result.Succeeded)
		{
			PT_APP_ERROR("Scene load failed, the current scene is unchanged: {}", l_Result.Error);
			SetFileStatus(std::format("Could not open {}", path.string()), l_Result);

			return false;
		}

		m_ScenePath = path;

		// The Ids in the file replace the ones that were selected, so the selection starts over at the first entity, and nothing from before can be undone into this scene
		m_SelectedEntity = m_Scene.GetEntities().empty() ? Engine::EntityId::Invalid : m_Scene.GetEntities().front().Id;
		m_History.Clear();
		m_CreatedEntities = 0;

		// The camera starts where the Sandbox would, reading the spawn once. From here on it is workspace state and the spawn is scene content, neither follows the other
		m_Camera.Reset(m_Scene.GetPlayerSpawn().Position, m_Scene.GetPlayerSpawn().Orientation);

		SetFileStatus(std::format("Opened {}", path.string()), l_Result);

		PT_APP_INFO("Loaded scene '{}' from {}: {} entities, {} materials, {} warning(s), revision {}", m_Scene.GetName(), path.string(), m_Scene.GetEntities().size(), m_Scene.GetMaterials().size(), l_Result.Warnings.size(), m_Scene.GetRadianceRevision());

		return true;
	}

	bool EditorClient::SaveSceneTo(const std::filesystem::path& path)
	{
		const Engine::SceneFileResult l_Result = Engine::SceneSerializer::Save(m_Scene, path, m_Assets);
		if (!l_Result.Succeeded)
		{
			PT_APP_ERROR("Scene save failed: {}", l_Result.Error);
			SetFileStatus(std::format("Could not save {}", path.string()), l_Result);

			return false;
		}

		m_ScenePath = path;
		m_History.MarkSaved();

		SetFileStatus(std::format("Saved {}", path.string()), l_Result);

		PT_APP_INFO("Saved scene '{}' to {}", m_Scene.GetName(), path.string());

		return true;
	}

	void EditorClient::BuildEmptyScene()
	{
		// A fresh scene carries a new revision, so the renderer drops whatever it had extracted. The default spawn and environment are the Scene's own, the camera stays where it is
		m_Scene = Engine::Scene{};
		m_Scene.SetName("Untitled");

		m_ScenePath.clear();
		m_SelectedEntity = Engine::EntityId::Invalid;
		m_History.Clear();
		m_CreatedEntities = 0;

		PT_APP_INFO("Empty scene '{}' ready, revision {}", m_Scene.GetName(), m_Scene.GetRadianceRevision());
	}

	void EditorClient::SetFileStatus(std::string summary, const Engine::SceneFileResult& result)
	{
		m_FileStatus.Summary = std::move(summary);
		m_FileStatus.Error = result.Error;
		m_FileStatus.Warnings = result.Warnings;
		m_FileStatus.Failed = !result.Succeeded;
	}

	Engine::Math::Vector3 EditorClient::GetCreatePosition() const
	{
		const Engine::Camera& l_Camera = m_Camera.GetCamera();

		return l_Camera.Position + Engine::GetCameraBasis(l_Camera).Forward * k_CreateDistance;
	}

	Engine::RenderRequest EditorClient::GetRenderRequest() const
	{
		const ViewportState& l_Viewport = m_ViewportPanel.GetState();

		// The view is the viewport's content at the render scale, never the window: BuildUI measured it this frame, so the images resize in this Render and the aspect follows the panel. Before the first layout the content is zero and the view is one pixel
		const float l_Scale = std::clamp(m_RenderSettings.RenderScale, 0.05f, 1.0f);

		Engine::RenderRequest l_Request;
		l_Request.View.Width = std::max(static_cast<uint32_t>(static_cast<float>(l_Viewport.ContentWidth) * l_Scale), 1u);
		l_Request.View.Height = std::max(static_cast<uint32_t>(static_cast<float>(l_Viewport.ContentHeight) * l_Scale), 1u);
		l_Request.View.ActiveCamera = m_Camera.GetCamera();
		l_Request.View.Settings = m_RenderSettings.Integrator;
		l_Request.View.Mode = m_RenderSettings.Mode;
		l_Request.ActiveScene = &m_Scene;
		l_Request.Assets = &m_Assets;
		l_Request.Exposure = std::exp2(m_RenderSettings.ExposureStops);

		// The UI shows the view through its texture id, the swapchain is cleared under the panels instead of taking the fullscreen pass
		l_Request.DrawViewToWindow = false;

		return l_Request;
	}
}