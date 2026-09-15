#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Engine
{
	// Physical key positions, values are USB HID keyboard usage IDs so the platform layer translates by range check instead of a table
	enum class Key : uint16_t
	{
		Unknown = 0,

		A = 4, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

		Number1 = 30, Number2, Number3, Number4, Number5, Number6, Number7, Number8, Number9, Number0,

		Return = 40,
		Escape = 41,
		Backspace = 42,
		Tab = 43,
		Space = 44,
		Minus = 45,
		Equals = 46,
		LeftBracket = 47,
		RightBracket = 48,
		Backslash = 49,
		Semicolon = 51,
		Apostrophe = 52,
		Grave = 53,
		Comma = 54,
		Period = 55,
		Slash = 56,
		CapsLock = 57,

		F1 = 58, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

		PrintScreen = 70,
		ScrollLock = 71,
		Pause = 72,
		Insert = 73,
		Home = 74,
		PageUp = 75,
		Delete = 76,
		End = 77,
		PageDown = 78,
		Right = 79,
		Left = 80,
		Down = 81,
		Up = 82,

		LeftControl = 224,
		LeftShift = 225,
		LeftAlt = 226,
		LeftSuper = 227,
		RightControl = 228,
		RightShift = 229,
		RightAlt = 230,
		RightSuper = 231,

		Count = 512
	};

	enum class MouseButton : uint8_t
	{
		Unknown = 0,
		Left,
		Right,
		Middle,
		Extra1,
		Extra2,

		Count
	};

	enum class InputEventType : uint8_t
	{
		KeyPressed,
		KeyReleased,
		MouseButtonPressed,
		MouseButtonReleased,
		MouseMoved,
		MouseWheel,
		FocusGained,
		FocusLost,
		WindowResized
	};

	// One translated platform event, only the fields that belong to the type are meaningful
	struct InputEvent
	{
		InputEventType Type = InputEventType::KeyPressed;

		Key KeyCode = Key::Unknown; // KeyPressed, KeyReleased
		bool Repeat = false; // KeyPressed only, true for OS key repeat

		MouseButton Button = MouseButton::Unknown; // MouseButtonPressed, MouseButtonReleased

		// MouseMoved and mouse buttons: cursor position in window coordinates MouseWheel: scroll amount, positive Y is away from the user WindowResized: new framebuffer size in pixels
		float X = 0.0f;
		float Y = 0.0f;

		// MouseMoved only: movement since the previous motion event, in window coordinates or relative units while captured
		float DeltaX = 0.0f;
		float DeltaY = 0.0f;
	};

	// Snapshot the client reads during Update, the host owns every write
	struct InputState
	{
		static constexpr size_t k_KeyCount = static_cast<size_t>(Key::Count);
		static constexpr size_t k_MouseButtonCount = static_cast<size_t>(MouseButton::Count);

		// Held state, survives across frames until a release or focus loss
		std::array<bool, k_KeyCount> KeysDown{};
		std::array<bool, k_MouseButtonCount> MouseButtonsDown{};

		// Edges, valid for the frame they happened in and reset before the next poll
		std::array<bool, k_KeyCount> KeysPressed{};
		std::array<bool, k_KeyCount> KeysReleased{};
		std::array<bool, k_MouseButtonCount> MouseButtonsPressed{};
		std::array<bool, k_MouseButtonCount> MouseButtonsReleased{};

		// Cursor position in window coordinates, meaningless while captured
		float MouseX = 0.0f;
		float MouseY = 0.0f;

		// Accumulated over the frame, reset before the next poll
		float MouseDeltaX = 0.0f;
		float MouseDeltaY = 0.0f;
		float WheelX = 0.0f;
		float WheelY = 0.0f;

		bool HasFocus = false;
		bool MouseCaptured = false;

		bool IsKeyDown(Key key) const { return KeysDown[Index(key)]; }
		bool WasKeyPressed(Key key) const { return KeysPressed[Index(key)]; }
		bool WasKeyReleased(Key key) const { return KeysReleased[Index(key)]; }

		bool IsMouseButtonDown(MouseButton button) const { return MouseButtonsDown[Index(button)]; }
		bool WasMouseButtonPressed(MouseButton button) const { return MouseButtonsPressed[Index(button)]; }
		bool WasMouseButtonReleased(MouseButton button) const { return MouseButtonsReleased[Index(button)]; }

		// Clears edges, deltas and wheel movement, called once at the top of every loop iteration
		void ResetTransient()
		{
			KeysPressed.fill(false);
			KeysReleased.fill(false);
			MouseButtonsPressed.fill(false);
			MouseButtonsReleased.fill(false);

			MouseDeltaX = 0.0f;
			MouseDeltaY = 0.0f;
			WheelX = 0.0f;
			WheelY = 0.0f;
		}

		// Releases everything that is held, the OS will not deliver the matching key-up events after focus is lost
		void ClearHeld()
		{
			for (size_t i_Key = 0; i_Key < k_KeyCount; ++i_Key)
			{
				if (KeysDown[i_Key])
				{
					KeysDown[i_Key] = false;
					KeysReleased[i_Key] = true;
				}
			}

			for (size_t i_Button = 0; i_Button < k_MouseButtonCount; ++i_Button)
			{
				if (MouseButtonsDown[i_Button])
				{
					MouseButtonsDown[i_Button] = false;
					MouseButtonsReleased[i_Button] = true;
				}
			}
		}

		static size_t Index(Key key)
		{
			const size_t l_Index = static_cast<size_t>(key);

			return l_Index < k_KeyCount ? l_Index : 0;
		}

		static size_t Index(MouseButton button)
		{
			const size_t l_Index = static_cast<size_t>(button);

			return l_Index < k_MouseButtonCount ? l_Index : 0;
		}
	};
}