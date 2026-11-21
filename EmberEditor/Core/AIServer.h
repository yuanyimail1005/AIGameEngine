#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Ember {

	// Line-delimited JSON server for AI agents and tools, bound to 127.0.0.1 only.
	// Each received line is a request; the handler is invoked (on the main thread, via the dispatcher) and its
	// return value is sent back as one line. Multiple clients may connect; requests are processed in order.
	class AIServer
	{
	public:
		// dispatch(line) must return the response line. It is called from network threads; the editor wraps it
		// so the actual work runs on the main thread.
		using Dispatcher = std::function<std::string(const std::string&)>;

		AIServer() = default;
		~AIServer();

		AIServer(const AIServer&) = delete;
		AIServer& operator=(const AIServer&) = delete;

		// Starts listening. port 0 picks a free port (see GetPort). Returns false if the port is unavailable.
		bool Start(uint16_t port, Dispatcher dispatcher, std::string* outError = nullptr);
		void Stop();
		bool IsRunning() const { return m_Running; }
		uint16_t GetPort() const { return m_Port; }

	private:
		void AcceptLoop();
		void ClientLoop(intptr_t client);

	private:
		Dispatcher m_Dispatcher;
		std::atomic<bool> m_Running{ false };
		intptr_t m_ListenSocket = -1;
		uint16_t m_Port = 0;
		std::thread m_AcceptThread;
		std::mutex m_ClientsMutex;
		std::vector<std::thread> m_ClientThreads;
		std::vector<intptr_t> m_ClientSockets;
	};

}
