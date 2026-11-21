#pragma once

#include "Ember/Core/Base.h"
#include "Ember/Core/Events.h"
#include "Ember/Core/Timestep.h"

#include <string>
#include <vector>

namespace Ember {

	class Layer
	{
	public:
		explicit Layer(std::string name = "Layer") : m_DebugName(std::move(name)) {}
		virtual ~Layer() = default;

		virtual void OnAttach() {}
		virtual void OnDetach() {}
		virtual void OnUpdate(Timestep /*ts*/) {}
		// Called after OnUpdate for all layers, while the frame's command list is open.
		virtual void OnRender() {}
		virtual void OnImGuiRender() {}
		virtual void OnEvent(Event& /*event*/) {}

		const std::string& GetName() const { return m_DebugName; }

	protected:
		std::string m_DebugName;
	};

	// Owns layers. Regular layers come first, overlays (e.g. ImGui) always come after them.
	class LayerStack
	{
	public:
		LayerStack() = default;
		~LayerStack();

		LayerStack(const LayerStack&) = delete;
		LayerStack& operator=(const LayerStack&) = delete;

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* overlay);
		// Detaches and returns ownership to the caller.
		bool PopLayer(Layer* layer);
		bool PopOverlay(Layer* overlay);
		// Detaches and deletes all layers (overlays first).
		void Clear();

		std::vector<Layer*>::iterator begin() { return m_Layers.begin(); }
		std::vector<Layer*>::iterator end() { return m_Layers.end(); }
		std::vector<Layer*>::reverse_iterator rbegin() { return m_Layers.rbegin(); }
		std::vector<Layer*>::reverse_iterator rend() { return m_Layers.rend(); }
		size_t Size() const { return m_Layers.size(); }

	private:
		std::vector<Layer*> m_Layers;
		size_t m_LayerInsertIndex = 0;
	};

}
