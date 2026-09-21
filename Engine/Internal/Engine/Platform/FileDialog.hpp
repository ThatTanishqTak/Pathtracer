#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include "Engine/Core/ApplicationClient.hpp"

#include <memory>
#include <optional>

struct SDL_Window;

namespace Engine
{
	class FileDialog
	{
	public:
		struct Pending;

		FileDialog();
		~FileDialog();

		FileDialog(const FileDialog&) = delete;
		FileDialog& operator=(const FileDialog&) = delete;

		bool Show(const FileDialogRequest& request, SDL_Window* parent);
		bool IsOpen() const { return m_Pending != nullptr; }

		std::optional<FileDialogResult> Poll();

		void Shutdown();

	private:
		std::shared_ptr<Pending> m_Pending;
	};
}