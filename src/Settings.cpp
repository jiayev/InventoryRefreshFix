#include "Settings.h"

#include "pch.h"

#include <Windows.h>

namespace Settings
{
	namespace
	{
		constexpr auto kConfigPath = L"Data\\SKSE\\Plugins\\InventoryRefreshFix.ini";

		bool g_enableInventoryEnumeration = true;
		bool g_validateInventoryEnumeration = true;
		bool g_enableRefreshCoalescing = false;
		bool g_enableIncrementalInvalidation = false;
	}

	void Load()
	{
		g_enableInventoryEnumeration = GetPrivateProfileIntW(
			L"General",
			L"bEnableInventoryEnumeration",
			1,
			kConfigPath) != 0;
		g_validateInventoryEnumeration = GetPrivateProfileIntW(
			L"General",
			L"bValidateInventoryEnumeration",
			1,
			kConfigPath) != 0;
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
			"Inventory refresh optimizations (bulk enumeration: {} / validation: {}; "
			"incremental invalidation: {}; coalescing: {})",
			g_enableInventoryEnumeration ? "enabled" : "disabled",
			g_validateInventoryEnumeration ? "enabled" : "disabled",
			g_enableIncrementalInvalidation ? "enabled" : "disabled",
			g_enableRefreshCoalescing ? "enabled" : "disabled");
	}

	bool IsInventoryEnumerationEnabled()
	{
		return g_enableInventoryEnumeration;
	}

	bool IsInventoryEnumerationValidationEnabled()
	{
		return g_validateInventoryEnumeration;
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
