#pragma once

#include "Ember/Core/Layer.h"
#include "Ember/ImGui/ImGuiRenderer.h"

#include <imgui.h>

namespace Ember {

	class ShaderLibrary;

	// Owns the Dear ImGui context (docking enabled), the GLFW platform backend and the NVRHI renderer.
	// The Application calls Begin() before and End() after all layers' OnImGuiRender().
	class ImGuiLayer : public Layer
	{
	public:
		ImGuiLayer();
		~ImGuiLayer() override;

		void OnAttach() override;
		void OnDetach() override;
		void OnEvent(Event& event) override;

		void Begin();
		// Renders ImGui onto the current back buffer (clearing it first).
		void End();

		// When true (default), mouse/keyboard events ImGui wants are not passed to lower layers.
		void BlockEvents(bool block) { m_BlockEvents = block; }

		static ImTextureID ToTextureID(nvrhi::ITexture* texture) { return reinterpret_cast<ImTextureID>(texture); }

	private:
		void ApplyStyle();

	private:
		Scope<ShaderLibrary> m_Shaders;
		Scope<ImGuiRenderer> m_Renderer;
		bool m_BlockEvents = true;
		bool m_Attached = false;
	};

}
