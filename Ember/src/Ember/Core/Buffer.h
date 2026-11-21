#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

namespace Ember {

	// Owning, move-only block of raw bytes.
	class Buffer
	{
	public:
		Buffer() = default;
		explicit Buffer(size_t size) : m_Data(size) {}
		Buffer(const void* data, size_t size)
			: m_Data(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size) {}

		Buffer(const Buffer&) = delete;
		Buffer& operator=(const Buffer&) = delete;
		Buffer(Buffer&&) noexcept = default;
		Buffer& operator=(Buffer&&) noexcept = default;

		uint8_t* Data() { return m_Data.data(); }
		const uint8_t* Data() const { return m_Data.data(); }
		size_t Size() const { return m_Data.size(); }
		bool Empty() const { return m_Data.empty(); }

		void Resize(size_t size) { m_Data.resize(size); }
		void Release() { m_Data.clear(); m_Data.shrink_to_fit(); }

		std::span<uint8_t> AsSpan() { return m_Data; }
		std::span<const uint8_t> AsSpan() const { return m_Data; }

		template<typename T>
		T* As() { return reinterpret_cast<T*>(m_Data.data()); }

		template<typename T>
		const T* As() const { return reinterpret_cast<const T*>(m_Data.data()); }

		explicit operator bool() const { return !m_Data.empty(); }

	private:
		std::vector<uint8_t> m_Data;
	};

}
