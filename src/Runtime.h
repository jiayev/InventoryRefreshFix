#pragma once

#include <REL/Version.h>

#include <array>
#include <cstdint>

namespace Runtime
{
	struct HookOffsets
	{
		bool                          anniversaryEdition;
		std::uint64_t                 processMessageID;
		std::array<std::uintptr_t, 5> refreshCalls;
		std::array<std::uintptr_t, 3> clearCalls;
		std::array<std::uintptr_t, 3> pushCalls;
		std::uintptr_t                invalidateCall;
		std::uint64_t                 invalidateID;
		std::uint64_t                 addItemID;
		std::array<std::uintptr_t, 2> addItemCalls;
		std::array<std::uintptr_t, 3> sortCalls;
		std::array<std::uint64_t, 3>  sortTargetIDs;
		std::uint64_t                 nameComparatorID;
		std::uint64_t                 namePartitionID;
		std::uint64_t                 nameRecurseID;
		std::array<std::uintptr_t, 2> nameCompareCalls;
		std::uintptr_t                collectCall;
		std::uint64_t                 collectID;
		std::uintptr_t                collectLoopCall;
		std::uintptr_t                bestInClassCall;
		std::uint64_t                 bestInClassID;
		std::array<std::uintptr_t, 2> enumerationCalls;
	};

	[[nodiscard]] const HookOffsets* GetHookOffsets(const REL::Version& a_version) noexcept;
}
