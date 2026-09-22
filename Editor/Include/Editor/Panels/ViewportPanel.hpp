#pragma once

#include "Engine/Engine.hpp"

#include "Editor/EditorCommandHistory.hpp"

#include <cstdint>

namespace Editor
{
	// What the gizmo over the selection does. In key order: 1, 2 and 3 pick an operation and 4 hides it while the viewport owns the keyboard
	enum class GizmoOperation : uint8_t
	{
		Translate,
		Rotate,
		Scale,
		None,
	};

	// What the viewport decided this frame, read by the client's Update and GetRenderRequest
	struct ViewportState
	{
		uint32_t ContentWidth = 0; // Settled content size in framebuffer pixels, the view extent before the render scale. Zero until the panel has had an area
		uint32_t ContentHeight = 0;

		// The image's screen rectangle in window coordinates, the space InputState::MouseX and MouseY and the UI cursor share. Zero size while there is no image
		float ImageX = 0.0f;
		float ImageY = 0.0f;
		float ImageWidth = 0.0f;
		float ImageHeight = 0.0f;

		bool Hovered = false; // The cursor is over the image and no popup, modal or other window is in the way. False while captured, the frozen cursor hovers nothing
		bool Focused = false; // The viewport window has the focus, a click on it or on the image gives it

		bool MouseOwned = false; // Mouse buttons and wheel belong to the viewport: captured, or hovered
		bool KeyboardOwned = false; // Keys belong to the camera: captured, or hovered or focused while no text field, modal or other focused panel wants the keyboard

		bool PickReady = false; // A left press this frame is a pick: the image is hovered with no toolbar button or gizmo handle under the cursor and no widget elsewhere still active
		bool GizmoActive = false; // A gizmo drag is under way, the history must not move under it
	};

	// The panel that shows the view: it draws the display texture through its UI texture id, measures its content and decides input ownership. Over the image it draws the selection outline, the transform gizmo and the gizmo toolbar, and turns a gizmo drag into one command
	class ViewportPanel
	{
	public:
		// Called from BuildUI, so the texture id is the previous frame's and the size it measures reaches this frame's render request. The gizmo edits the selected entity live and records the command when the drag ends
		void Draw(uint64_t textureId, bool mouseCaptured, const Engine::Camera& camera, Engine::Scene& scene, Engine::EntityId selectedEntity, EditorCommandHistory& history);

		const ViewportState& GetState() const { return m_State; }

		// The camera looking through the settled content: the controller leaves the extent to whoever renders, and picking and the overlays need the aspect the image was rendered with
		Engine::Camera GetViewCamera(const Engine::Camera& camera) const;

		GizmoOperation GetGizmoOperation() const { return m_GizmoOperation; }
		void SetGizmoOperation(GizmoOperation operation) { m_GizmoOperation = operation; }

	private:
		// Every new extent recreates the view images behind a wait for every frame, so a size seen during a mouse drag is applied only once it held this many frames. Mouse up applies at once
		static constexpr int k_SettleFrames = 6;

		void DrawToolbar();

		// Both read the image rectangle from the state, which Draw fills before calling them
		void DrawSelectionOutline(const Engine::Camera& camera, const Engine::Entity& entity) const;
		bool DrawGizmo(const Engine::Camera& camera, Engine::Scene& scene, Engine::Entity& entity, EditorCommandHistory& history); // Whether the gizmo is under the cursor or being dragged, a press there is never a pick

		void EndGizmoEdit(Engine::Scene& scene, EditorCommandHistory& history);

		ViewportState m_State;

		uint32_t m_PendingWidth = 0;
		uint32_t m_PendingHeight = 0;
		int m_PendingFrames = 0;

		GizmoOperation m_GizmoOperation = GizmoOperation::Translate;
		bool m_GizmoWorldSpace = true;

		// One command per drag, from the entity as it was before the first changed frame. ImGuizmo owns the drag itself, the snapshot is retaken on every frame without one
		Engine::EntityId m_GizmoEntity = Engine::EntityId::Invalid;
		Engine::Entity m_GizmoBefore;
		GizmoOperation m_GizmoBeforeOperation = GizmoOperation::Translate;
		bool m_GizmoEditing = false;
	};
}