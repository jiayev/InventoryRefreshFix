#include "pch.h"

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);

	SKSE::log::info("InventoryRefreshFix loaded");

	return true;
}
