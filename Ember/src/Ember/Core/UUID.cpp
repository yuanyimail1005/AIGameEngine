#include "Ember/Core/UUID.h"

#include <random>

namespace Ember {

	namespace {

		uint64_t GenerateNonZero()
		{
			thread_local std::mt19937_64 engine(std::random_device{}());
			thread_local std::uniform_int_distribution<uint64_t> distribution(1, UINT64_MAX);
			return distribution(engine);
		}

	}

	UUID::UUID()
		: m_UUID(GenerateNonZero())
	{
	}

}
