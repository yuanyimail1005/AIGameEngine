#include "Ember/ImGui/ImGuiLayer.h"

#include "Ember/Core/Application.h"
#include "Ember/Core/Log.h"
#include "Ember/Renderer/GraphicsDevice.h"
#include "Ember/Renderer/ShaderLibrary.h"

#include <ImGuizmo.h>
#include <imgui_impl_glfw.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace Ember {

	ImGuiLayer::ImGuiLayer()
		: Layer("ImGuiLayer")
	{
	}

	ImGuiLayer::~ImGuiLayer()
	{
		if (m_Attached)
			OnDetach();
	}

	void ImGuiLayer::OnAttach()
	{
		Application& app = Application::Get();
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
		io.IniFilename = "imgui.ini";

		float scale = 1.0f;
		if (Window* window = app.GetWindow())
		{
			float xScale = 1.0f, yScale = 1.0f;
			glfwGetWindowContentScale(window->GetNativeWindow(), &xScale, &yScale);
			scale = std::max(1.0f, xScale);
		}
		ImFontConfig fontConfig;
		fontConfig.SizePixels = 14.0f * scale;
		io.Fonts->AddFontDefault(&fontConfig);

		ApplyStyle();
		ImGui::GetStyle().ScaleAllSizes(scale);

		if (Window* window = app.GetWindow())
			ImGui_ImplGlfw_InitForOther(window->GetNativeWindow(), true);

		nvrhi::IDevice* device = app.GetGraphicsDevice().GetDevice();
		m_Shaders = CreateScope<ShaderLibrary>(device);
		m_Renderer = CreateScope<ImGuiRenderer>(device, *m_Shaders);
		if (!m_Renderer->CreateFontTexture())
			EMBER_CORE_ERROR("ImGui: failed to create the font texture");
		m_Attached = true;
	}

	void ImGuiLayer::OnDetach()
	{
		if (!m_Attached)
			return;
		m_Attached = false;
		m_Renderer.reset();
		m_Shaders.reset();
		if (Application::HasInstance() && Application::Get().GetWindow())
			ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}

	void ImGuiLayer::OnEvent(Event& event)
	{
		if (!m_BlockEvents)
			return;
		const ImGuiIO& io = ImGui::GetIO();
		if (event.IsInCategory(EventCategoryMouse) && io.WantCaptureMouse)
			event.Handled = true;
		if (event.IsInCategory(EventCategoryKeyboard) && io.WantCaptureKeyboard)
			event.Handled = true;
	}

	void ImGuiLayer::Begin()
	{
		if (Application::Get().GetWindow())
			ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		ImGuizmo::BeginFrame();
	}

	void ImGuiLayer::End()
	{
		ImGui::Render();
		GraphicsDevice& device = Application::Get().GetGraphicsDevice();
		nvrhi::IFramebuffer* framebuffer = device.GetCurrentFramebuffer();
		if (!framebuffer)
			return;

		nvrhi::CommandListHandle clear = device.GetDevice()->createCommandList();
		clear->open();
		clear->clearTextureFloat(device.GetCurrentBackBuffer(), nvrhi::AllSubresources, nvrhi::Color(0.1f, 0.1f, 0.11f, 1.0f));
		clear->close();
		device.GetDevice()->executeCommandList(clear);

		m_Renderer->Render(ImGui::GetDrawData(), framebuffer);
	}

	void ImGuiLayer::ApplyStyle()
	{
		ImGui::StyleColorsDark();
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 3.0f;
		style.FrameRounding = 3.0f;
		style.GrabRounding = 3.0f;
		style.TabRounding = 3.0f;
		style.WindowBorderSize = 1.0f;
		style.FramePadding = ImVec2(6.0f, 4.0f);

		ImVec4* colors = style.Colors;
		const ImVec4 ember(0.92f, 0.45f, 0.15f, 1.0f);
		colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.11f, 0.12f, 1.0f);
		colors[ImGuiCol_Header] = ImVec4(0.22f, 0.22f, 0.24f, 1.0f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.30f, 0.33f, 1.0f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.35f, 0.25f, 0.18f, 1.0f);
		colors[ImGuiCol_Button] = ImVec4(0.22f, 0.22f, 0.24f, 1.0f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.32f, 0.32f, 0.35f, 1.0f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.40f, 0.28f, 0.18f, 1.0f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.17f, 0.18f, 1.0f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.24f, 0.26f, 1.0f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.28f, 0.30f, 1.0f);
		colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.16f, 1.0f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.38f, 0.28f, 0.20f, 1.0f);
		colors[ImGuiCol_TabActive] = ImVec4(0.28f, 0.22f, 0.18f, 1.0f);
		colors[ImGuiCol_TabUnfocused] = ImVec4(0.15f, 0.15f, 0.16f, 1.0f);
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.20f, 0.20f, 0.21f, 1.0f);
		colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.12f, 0.13f, 1.0f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.14f, 0.15f, 1.0f);
		colors[ImGuiCol_CheckMark] = ember;
		colors[ImGuiCol_SliderGrab] = ember;
		colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.55f, 0.25f, 1.0f);
		colors[ImGuiCol_DockingPreview] = ImVec4(ember.x, ember.y, ember.z, 0.5f);
		colors[ImGuiCol_SeparatorHovered] = ember;
		colors[ImGuiCol_ResizeGripHovered] = ember;
	}

}
