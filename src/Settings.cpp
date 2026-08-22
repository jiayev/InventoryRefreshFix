#include "Settings.h"

#include "pch.h"

#include <Windows.h>

namespace Settings
{
	namespace
	{
		constexpr auto kConfigPath = L"Data\\SKSE\\Plugins\\InventoryRefreshFix.ini";

		bool g_enableRefreshCoalescing = false;
		bool g_enableIncrementalInvalidation = false;
	}

	void Load()
	{
		g_enableRefreshCoalescing = GetPrivateProfileIntW(
			L"General",
			L"bEnableRefreshCoalescing",
			0,
			kConfigPath) != 0;
		g_enableIncrementalInvalidation = GetPrivateProfileIntW(
			L"General",
			L"bEnableIncrementalInvalidation",
			0,
			kConfigPath) != 0;

		SKSE::log::info(
			"Inventory refresh optimizations (incremental invalidation: {}; coalescing: {})",
			g_enableIncrementalInvalidation ? "enabled" : "disabled",
			g_enableRefreshCoalescing ? "enabled" : "disabled");
	}

	bool IsRefreshCoalescingEnabled()
	{
		return g_enableRefreshCoalescing;
	}

	bool IsIncrementalInvalidationEnabled()
	{
		return g_enableIncrementalInvalidation;
	}
}
