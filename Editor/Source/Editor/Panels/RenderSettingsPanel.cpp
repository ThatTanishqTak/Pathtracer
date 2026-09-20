#include "Editor/Panels/RenderSettingsPanel.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace Editor
{
	namespace
	{
		// In DiagnosticMode order, the combo index is the enum value
		constexpr std::array<const char*, 6> k_ModeNames
		{
			"Ray direction",
			"Hit",
			"Normal",
			"Distance",
			"Base colour",
			"Path traced",
		};

		// Fixed steps rather than a slider, every new scale recreates the view images behind a wait for every frame
		constexpr std::array<float, 4> k_RenderScales{ 1.0f, 0.75f, 0.5f, 0.25f };
		constexpr std::array<const char*, 4> k_RenderScaleNames{ "1.00", "0.75", "0.50", "0.25" };

		int NearestRenderScaleIndex(float scale)
		{
			int l_Index = 0;
			for (size_t i_Scale = 1; i_Scale < k_RenderScales.size(); ++i_Scale)
			{
				if (std::abs(k_RenderScales[i_Scale] - scale) < std::abs(k_RenderScales[static_cast<size_t>(l_Index)] - scale))
				{
					l_Index = static_cast<int>(i_Scale);
				}
			}

			return l_Index;
		}
	}

	void RenderSettingsPanel::Draw(EditorRenderSettings& settings, uint32_t viewWidth, uint32_t viewHeight)
	{
		if (ImGui::Begin("Render Settings"))
		{
			int l_Mode = std::clamp(static_cast<int>(settings.Mode), 0, static_cast<int>(k_ModeNames.size()) - 1);
			if (ImGui::Combo("Mode", &l_Mode, k_ModeNames.data(), static_cast<int>(k_ModeNames.size())))
			{
				settings.Mode = static_cast<Engine::DiagnosticMode>(l_Mode);
			}

			// Zero shows emitters and the environment only
			int l_MaxBounces = static_cast<int>(std::min(settings.Integrator.MaxBounces, static_cast<uint32_t>(k_MaxBounces)));
			if (ImGui::SliderInt("Max bounces", &l_MaxBounces, 0, k_MaxBounces, "%d", ImGuiSliderFlags_AlwaysClamp))
			{
				settings.Integrator.MaxBounces = static_cast<uint32_t>(l_MaxBounces);
			}

			int l_ScaleIndex = NearestRenderScaleIndex(settings.RenderScale);
			if (ImGui::Combo("Render scale", &l_ScaleIndex, k_RenderScaleNames.data(), static_cast<int>(k_RenderScaleNames.size())))
			{
				settings.RenderScale = k_RenderScales[static_cast<size_t>(l_ScaleIndex)];
			}

			// The same seed and sample count reproduce the same image
			const uint32_t l_SeedStep = 1;
			ImGui::InputScalar("Seed", ImGuiDataType_U32, &settings.Integrator.Seed, &l_SeedStep, nullptr, "%u");

			if (ImGui::SliderFloat("Exposure (stops)", &settings.ExposureStops, -k_ExposureRangeStops, k_ExposureRangeStops, "%+.1f", ImGuiSliderFlags_AlwaysClamp))
			{
				settings.ExposureStops = std::clamp(settings.ExposureStops, -k_ExposureRangeStops, k_ExposureRangeStops);
			}

			ImGui::Separator();
			ImGui::TextDisabled("View %ux%u, exposure %.3fx", viewWidth, viewHeight, std::exp2(settings.ExposureStops));
		}

		ImGui::End();
	}
}