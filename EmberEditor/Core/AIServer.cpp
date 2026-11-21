#include "AIServer.h"

#include "Ember/Core/Base.h"
#include "Ember/Core/Log.h"

#include <algorithm>
#include <cstring>

#if defined(EMBER_PLATFORM_WINDOWS)
	#define WIN32_LEAN_AND_MEAN
	#define NOMINMAX
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#pragma comment(lib, "ws2_32.lib")
	using SocketLength = int;
	using NativeSocket = SOCKET;
	#define EMBER_CLOSE_SOCKET closesocket
	#define EMBER_INVALID_SOCKET static_cast<intptr_t>(INVALID_SOCKET)
#else
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <netinet/tcp.h>
	#include <sys/socket.h>
	#include <unistd.h>
	using SocketLength = socklen_t;
	using NativeSocket = int;
	#define EMBER_CLOSE_SOCKET ::close
	#define EMBER_INVALID_SOCKET intptr_t(-1)
#endif

namespace Ember {

	namespace {

		NativeSocket Native(intptr_t socketHandle) { return static_cast<NativeSocket>(socketHandle); }

		bool InitializeSockets()
		{
#if defined(EMBER_PLATFORM_WINDOWS)
			static bool s_Initialized = false;
			if (!s_Initialized)
			{
				WSADATA data;
				s_Initialized = WSAStartup(MAKEWORD(2, 2), &data) == 0;
			}
			return s_Initialized;
#else
			return true;
#endif
		}

		bool SendAll(intptr_t socket, const std::string& data)
		{
			size_t sent = 0;
			while (sent < data.size())
			{
#if defined(EMBER_PLATFORM_WINDOWS)
				const int result = send(static_cast<SOCKET>(socket), data.data() + sent, static_cast<int>(data.size() - sent), 0);
#else
				const ssize_t result = send(static_cast<int>(socket), data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#endif
				if (result <= 0)
					return false;
				sent += static_cast<size_t>(result);
			}
			return true;
		}

	}

	AIServer::~AIServer()
	{
		Stop();
	}

	bool AIServer::Start(uint16_t port, Dispatcher dispatcher, std::string* outError)
	{
		if (m_Running)
			return true;
		if (!InitializeSockets())
		{
			if (outError)
				*outError = "socket initialization failed";
			return false;
		}

		const intptr_t listenSocket = static_cast<intptr_t>(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
		if (listenSocket == EMBER_INVALID_SOCKET)
		{
			if (outError)
				*outError = "cannot create socket";
			return false;
		}

		int reuse = 1;
		setsockopt(Native(listenSocket), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = htons(port);
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // never exposed to the network
		if (bind(Native(listenSocket), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
			listen(Native(listenSocket), 8) != 0)
		{
			EMBER_CLOSE_SOCKET(Native(listenSocket));
			if (outError)
				*outError = "cannot listen on 127.0.0.1:" + std::to_string(port) + " (port in use?)";
			return false;
		}

		SocketLength length = sizeof(address);
		getsockname(Native(listenSocket), reinterpret_cast<sockaddr*>(&address), &length);
		m_Port = ntohs(address.sin_port);
		m_ListenSocket = listenSocket;
		m_Dispatcher = std::move(dispatcher);
		m_Running = true;
		m_AcceptThread = std::thread(&AIServer::AcceptLoop, this);
		EMBER_CORE_INFO("AI control server listening on 127.0.0.1:{}", m_Port);
		return true;
	}

	void AIServer::Stop()
	{
		if (!m_Running.exchange(false))
			return;

#if defined(EMBER_PLATFORM_WINDOWS)
		closesocket(static_cast<SOCKET>(m_ListenSocket));
#else
		shutdown(static_cast<int>(m_ListenSocket), SHUT_RDWR);
		::close(static_cast<int>(m_ListenSocket));
#endif
		if (m_AcceptThread.joinable())
			m_AcceptThread.join();

		std::vector<std::thread> threads;
		{
			std::scoped_lock lock(m_ClientsMutex);
			for (intptr_t client : m_ClientSockets)
			{
#if defined(EMBER_PLATFORM_WINDOWS)
				shutdown(static_cast<SOCKET>(client), SD_BOTH);
#else
				shutdown(static_cast<int>(client), SHUT_RDWR);
#endif
			}
			threads.swap(m_ClientThreads);
		}
		for (std::thread& thread : threads)
		{
			if (thread.joinable())
				thread.join();
		}
		m_ListenSocket = -1;
	}

	void AIServer::AcceptLoop()
	{
		while (m_Running)
		{
			sockaddr_in clientAddress{};
			SocketLength length = sizeof(clientAddress);
			const intptr_t client = static_cast<intptr_t>(accept(Native(m_ListenSocket),
				reinterpret_cast<sockaddr*>(&clientAddress), &length));
			if (client == EMBER_INVALID_SOCKET)
			{
				if (!m_Running)
					break;
				continue;
			}
			int noDelay = 1;
			setsockopt(Native(client), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
			std::scoped_lock lock(m_ClientsMutex);
			m_ClientSockets.push_back(client);
			m_ClientThreads.emplace_back(&AIServer::ClientLoop, this, client);
		}
	}

	void AIServer::ClientLoop(intptr_t client)
	{
		EMBER_CORE_INFO("AI client connected");
		std::string buffer;
		char chunk[4096];
		constexpr size_t maxRequestSize = 64 * 1024 * 1024;
		while (m_Running)
		{
#if defined(EMBER_PLATFORM_WINDOWS)
			const int received = recv(static_cast<SOCKET>(client), chunk, sizeof(chunk), 0);
#else
			const ssize_t received = recv(static_cast<int>(client), chunk, sizeof(chunk), 0);
#endif
			if (received <= 0)
				break;
			buffer.append(chunk, static_cast<size_t>(received));
			if (buffer.size() > maxRequestSize)
			{
				SendAll(client, "{\"id\":null,\"error\":{\"message\":\"request too large\"}}\n");
				break;
			}

			size_t newline;
			while ((newline = buffer.find('\n')) != std::string::npos)
			{
				std::string line = buffer.substr(0, newline);
				buffer.erase(0, newline + 1);
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (line.empty())
					continue;
				if (!SendAll(client, m_Dispatcher(line) + "\n"))
					break;
			}
		}
		EMBER_CLOSE_SOCKET(Native(client));
		std::scoped_lock lock(m_ClientsMutex);
		m_ClientSockets.erase(std::remove(m_ClientSockets.begin(), m_ClientSockets.end(), client), m_ClientSockets.end());
		EMBER_CORE_INFO("AI client disconnected");
	}

}
