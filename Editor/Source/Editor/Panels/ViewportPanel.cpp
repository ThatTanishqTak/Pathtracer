#include "Editor/Panels/ViewportPanel.hpp"

#include <imgui.h>

#include <algorithm>

namespace Editor
{
	void ViewportPanel::Draw(uint64_t textureId, bool mouseCaptured)
	{
		const ImGuiIO& l_IO = ImGui::GetIO();

		// No padding, the image fills the panel edge to edge. NoNavInputs keeps keyboard navigation off while the viewport is focused, so WantCaptureKeyboard only reports a text field or a modal and the movement keys can reach the camera
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		const bool l_Open = ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoCollapse);
		ImGui::PopStyleVar();

		bool l_Hovered = false;
		bool l_Focused = false;

		uint32_t l_Width = 0;
		uint32_t l_Height = 0;

		if (l_Open)
		{
			const ImVec2 l_Available = ImGui::GetContentRegionAvail();

			// Window coordinates to framebuffer pixels, the SDL3 backend reports the ratio on a high-DPI display
			l_Width = static_cast<uint32_t>(std::max(l_Available.x * l_IO.DisplayFramebufferScale.x, 0.0f));
			l_Height = static_cast<uint32_t>(std::max(l_Available.y * l_IO.DisplayFramebufferScale.y, 0.0f));

			if (textureId != 0 && l_Available.x > 0.0f && l_Available.y > 0.0f)
			{
				// The display texture is already sRGB encoded, the UI pass samples it with the backend's linear sampler
				ImGui::Image(static_cast<ImTextureID>(textureId), l_Available);

				l_Hovered = ImGui::IsItemHovered();
			}
			else
			{
				// Zero before the first frame, the panel is an empty rectangle that is still hoverable
				l_Hovered = ImGui::IsWindowHovered();
			}

			l_Focused = ImGui::IsWindowFocused();
		}

		ImGui::End();

		// Debounce: a size that arrives with the mouse up, a window resize or a dock change, applies at once. One seen during a splitter drag must hold for a few frames first, so the drag does not wait for every frame on each of its steps
		if (l_Width != m_PendingWidth || l_Height != m_PendingHeight)
		{
			m_PendingWidth = l_Width;
			m_PendingHeight = l_Height;
			m_PendingFrames = 0;
		}
		else
		{
			m_PendingFrames = std::min(m_PendingFrames + 1, k_SettleFrames);
		}

		const bool l_Dragging = ImGui::IsMouseDown(ImGuiMouseButton_Left);
		if (m_PendingWidth > 0 && m_PendingHeight > 0 && (!l_Dragging || m_PendingFrames >= k_SettleFrames))
		{
			// A hidden or collapsed viewport measures zero and keeps the last extent, so the view never resizes to nothing
			m_State.ContentWidth = m_PendingWidth;
			m_State.ContentHeight = m_PendingHeight;
		}

		// Ownership: the two WantCapture flags say what the UI wants, the hover and focus say whether the viewport is the part of the UI that wants it. WantCaptureMouse is true over the viewport itself, so the hover test is the finer one for the mouse. The keyboard is the camera's while the viewport is hovered or focused, unless a text field, a modal or another focused panel's keyboard navigation took it
		m_State.Hovered = l_Hovered && !mouseCaptured;
		m_State.Focused = l_Focused;
		m_State.MouseOwned = mouseCaptured || m_State.Hovered;
		m_State.KeyboardOwned = mouseCaptured || ((l_Focused || m_State.Hovered) && !l_IO.WantCaptureKeyboard);
	}
}