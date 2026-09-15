#pragma once

#include "Engine/Engine.hpp"

#include <array>
#include <cstddef>

namespace Sandbox
{
	class SandboxClient final : public Engine::ApplicationClient
	{
	public:
		void OnStart(Engine::ApplicationServices& services) override;
		void OnStop() noexcept override;
		void OnEvent(const Engine::InputEvent& event) override;
		void Update(const Engine::FrameTime& time, const Engine::InputState& input) override;
		Engine::RenderRequest GetRenderRequest() const override;

	private:
		Engine::ApplicationServices* m_Services = nullptr;

		static constexpr std::array<std::array<float, 4>, 4> k_ClearColors
		{ {
			{ 0.05f, 0.05f, 0.05f, 1.0f },
			{ 0.25f, 0.05f, 0.05f, 1.0f },
			{ 0.05f, 0.25f, 0.05f, 1.0f },
			{ 0.05f, 0.05f, 0.25f, 1.0f },
		} };

		size_t m_ClearColorIndex = 0;

		float m_StatisticsElapsed = 0.0f;
		uint32_t m_StatisticsFrames = 0;
		float m_StatisticsMouseDeltaX = 0.0f;
		float m_StatisticsMouseDeltaY = 0.0f;
	};
}