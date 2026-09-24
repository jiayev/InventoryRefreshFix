#include "Runtime.h"

#include "pch.h"

namespace Runtime
{
	namespace
	{
		constexpr HookOffsets kSE1597{
			.anniversaryEdition = false,
			.processMessageID = 50969,
			.refreshCalls = { 0x134, 0x13C, 0x785, 0x78D, 0x7B7 },
			.clearCalls = { 0x27, 0x27, 0x27 },
			.pushCalls = { 0xBF, 0xFF, 0xFF },
			.invalidateCall = 0x11B,
			.invalidateID = 50097,
			.addItemID = 50071,
			.addItemCalls = { 0x8CE, 0xA0C },
			.sortCalls = { 0x65, 0x83, 0xA1 },
			.sortTargetIDs = { 50034, 50032, 50033 },
			.nameComparatorID = 50932,
			.namePartitionID = 50022,
			.nameRecurseID = 50025,
			.nameCompareCalls = { 0x4F, 0xAD },
			.collectCall = 0x9C1,
			.collectID = 15867,
			.collectLoopCall = 0xC7,
			.bestInClassCall = 0xFA,
			.bestInClassID = 50954,
			.enumerationCalls = { 0x8A5, 0x900 }
		};

		constexpr HookOffsets kAE{
			.anniversaryEdition = true,
			.processMessageID = 51848,
			.refreshCalls = { 0x21B, 0x223, 0xB2B, 0xB33, 0xB5D },
			.clearCalls = { 0x190, 0x218, 0x2F6 },
			.pushCalls = { 0x1EE, 0x2CC, 0x3A0 },
			.invalidateCall = 0x452,
			.invalidateID = 82640,
			.addItemID = 51011,
			.addItemCalls = { 0xEC, 0x779 },
			.sortCalls = { 0x1AF, 0x286, 0x365 },
			.sortTargetIDs = { 50968, 50966, 50967 },
			.nameComparatorID = 51809,
			.namePartitionID = 0,
			.nameRecurseID = 0,
			.nameCompareCalls = { 0x70, 0xFF },
			.collectCall = 0x732,
			.collectID = 16107,
			.collectLoopCall = 0xBF,
			.bestInClassCall = 0x3E8,
			.bestInClassID = 51831,
			.enumerationCalls = { 0xC1, 0x11E }
		};
	}

	const HookOffsets* GetHookOffsets(const REL::Version& a_version) noexcept
	{
		if (a_version == SKSE::RUNTIME_SSE_1_5_97) {
			return &kSE1597;
		}
		if (a_version.major() == 1 && (a_version.minor() == 6 || a_version.minor() == 7)) {
			return &kAE;
		}
		return nullptr;
	}
}
