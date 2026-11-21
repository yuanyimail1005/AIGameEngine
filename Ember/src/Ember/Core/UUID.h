#pragma once

#include <cstdint>
#include <functional>

namespace Ember {

	// 64-bit random identifier used for entities and assets. Zero is reserved as "invalid".
	class UUID
	{
	public:
		UUID();
		constexpr UUID(uint64_t uuid) : m_UUID(uuid) {}

		constexpr operator uint64_t() const { return m_UUID; }
		constexpr bool IsValid() const { return m_UUID != 0; }

	private:
		uint64_t m_UUID;
	};

}

template<>
struct std::hash<Ember::UUID>
{
	size_t operator()(const Ember::UUID& uuid) const noexcept
	{
		return std::hash<uint64_t>()(static_cast<uint64_t>(uuid));
	}
};
