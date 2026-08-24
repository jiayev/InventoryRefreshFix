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
		bool g_enableIncrementalInvalidation = true;
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
		g_enableIncrementalInvalidation = GetPrivateProfileIntW(
			L"General",
			L"bEnableIncrementalInvalidation",
			1,
			kConfigPath) != 0;

		const auto configStatus =
			GetFileAttributesW(kConfigPath) != INVALID_FILE_ATTRIBUTES ? "loaded" : "not found; using defaults";
		SKSE::log::info(
			"Inventory refresh optimizations (bulk enumeration: {} / validation: {}; "
			"incremental invalidation: {}; configuration: {})",
			g_enableInventoryEnumeration ? "enabled" : "disabled",
			g_validateInventoryEnumeration ? "enabled" : "disabled",
			g_enableIncrementalInvalidation ? "enabled" : "disabled",
			configStatus);
	}

	bool IsInventoryEnumerationEnabled()
	{
		return g_enableInventoryEnumeration;
	}

	bool IsInventoryEnumerationValidationEnabled()
	{
		return g_validateInventoryEnumeration;
	}

	bool IsIncrementalInvalidationEnabled()
	{
		return g_enableIncrementalInvalidation;
	}
}
