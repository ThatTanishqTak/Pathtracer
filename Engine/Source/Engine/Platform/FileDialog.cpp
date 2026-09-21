#include "Engine/Platform/FileDialog.hpp"

#include "Engine/Core/Log.hpp"

#include <SDL3/SDL.h>

#include <mutex>
#include <string>
#include <utility>

namespace Engine
{
	struct FileDialog::Pending
	{
		std::mutex Mutex;
		bool Finished = false;
		FileDialogResult Result;
		std::string FilterName;
		std::string FilterPattern;
		SDL_DialogFileFilter Filter{};
		std::string DefaultLocation;
	};

	namespace
	{
		// Runs on whichever thread the platform chooses, so it only writes under the lock and never touches the owner
		void SDLCALL OnDialogComplete(void* userdata, const char* const* fileList, int filter)
		{
			(void)filter;

			// The callback holds one reference of its own, released when it returns
			const std::unique_ptr<std::shared_ptr<FileDialog::Pending>> l_Reference(static_cast<std::shared_ptr<FileDialog::Pending>*>(userdata));
			FileDialog::Pending& l_Pending = **l_Reference;

			std::string l_Error;
			if (fileList == nullptr)
			{
				l_Error = SDL_GetError();
			}

			const std::lock_guard<std::mutex> l_Lock(l_Pending.Mutex);

			if (fileList == nullptr)
			{
				l_Pending.Result.Accepted = false;
				l_Pending.Result.Error = l_Error.empty() ? "unknown error" : l_Error;
			}
			else if (fileList[0] == nullptr)
			{
				// Cancelled, nothing was chosen
				l_Pending.Result.Accepted = false;
			}
			else
			{
				// UTF-8 from SDL, the path takes it as such on every platform
				l_Pending.Result.Accepted = true;
				l_Pending.Result.Path = std::filesystem::path(reinterpret_cast<const char8_t*>(fileList[0]));
			}

			l_Pending.Finished = true;
		}
	}

	FileDialog::FileDialog() = default;
	FileDialog::~FileDialog() = default;

	bool FileDialog::Show(const FileDialogRequest& request, SDL_Window* parent)
	{
		if (IsOpen())
		{
			PT_CORE_WARN("A file dialog is already open, the new request is ignored");

			return false;
		}

		m_Pending = std::make_shared<Pending>();
		m_Pending->Result.Kind = request.Kind;
		m_Pending->FilterName = request.FilterName;
		m_Pending->FilterPattern = request.FilterPattern;
		m_Pending->Filter = SDL_DialogFileFilter{ m_Pending->FilterName.c_str(), m_Pending->FilterPattern.c_str() };

		const std::u8string l_Location = request.DefaultLocation.u8string();
		m_Pending->DefaultLocation.assign(reinterpret_cast<const char*>(l_Location.data()), l_Location.size());

		const SDL_DialogFileFilter* l_Filters = m_Pending->FilterPattern.empty() ? nullptr : &m_Pending->Filter;
		const int l_FilterCount = l_Filters != nullptr ? 1 : 0;
		const char* l_DefaultLocation = m_Pending->DefaultLocation.empty() ? nullptr : m_Pending->DefaultLocation.c_str();

		// The callback's reference, freed by the callback
		auto* l_Reference = new std::shared_ptr<Pending>(m_Pending);

		if (request.Kind == FileDialogKind::Save)
		{
			SDL_ShowSaveFileDialog(&OnDialogComplete, l_Reference, parent, l_Filters, l_FilterCount, l_DefaultLocation);
		}
		else
		{
			SDL_ShowOpenFileDialog(&OnDialogComplete, l_Reference, parent, l_Filters, l_FilterCount, l_DefaultLocation, false);
		}

		PT_CORE_TRACE("{} file dialog shown, starting at {}", request.Kind == FileDialogKind::Save ? "Save" : "Open", l_DefaultLocation != nullptr ? l_DefaultLocation : "<platform default>");

		return true;
	}

	std::optional<FileDialogResult> FileDialog::Poll()
	{
		if (!m_Pending)
		{
			return std::nullopt;
		}

		FileDialogResult l_Result;
		{
			const std::lock_guard<std::mutex> l_Lock(m_Pending->Mutex);
			if (!m_Pending->Finished)
			{
				return std::nullopt;
			}

			l_Result = std::move(m_Pending->Result);
		}

		m_Pending.reset();

		return l_Result;
	}

	void FileDialog::Shutdown()
	{
		if (m_Pending)
		{
			PT_CORE_WARN("A file dialog is still open at shutdown, its outcome is dropped");

			m_Pending.reset();
		}
	}
}