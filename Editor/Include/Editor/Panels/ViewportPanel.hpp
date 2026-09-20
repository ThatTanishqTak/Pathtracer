#pragma once

#include <cstdint>

namespace Editor
{
	// What the viewport decided this frame, read by the client's Update and GetRenderRequest
	struct ViewportState
	{
		uint32_t ContentWidth = 0; // Settled content size in framebuffer pixels, the view extent before the render scale. Zero until the panel has had an area
		uint32_t ContentHeight = 0;

		bool Hovered = false; // The cursor is over the image and no popup, modal or other window is in the way. False while captured, the frozen cursor hovers nothing
		bool Focused = false; // The viewport window has the focus, a click on it or on the image gives it

		bool MouseOwned = false; // Mouse buttons and wheel belong to the viewport: captured, or hovered. Step 11 picks with this
		bool KeyboardOwned = false; // Keys belong to the camera: captured, or hovered or focused while no text field, modal or other focused panel wants the keyboard
	};

	// The panel that shows the view: it draws the display texture through its UI texture id, measures its content and decides input ownership. Read-only towards the scene until Step 11 adds picking and gizmos
	class ViewportPanel
	{
	public:
		// Called from BuildUI, so the texture id is the previous frame's and the size it measures reaches this frame's render request
		void Draw(uint64_t textureId, bool mouseCaptured);

		const ViewportState& GetState() const { return m_State; }

	private:
		// Every new extent recreates the view images behind a wait for every frame, so a size seen during a mouse drag is applied only once it held this many frames. Mouse up applies at once
		static constexpr int k_SettleFrames = 6;

		ViewportState m_State;

		uint32_t m_PendingWidth = 0;
		uint32_t m_PendingHeight = 0;
		int m_PendingFrames = 0;
	};
}