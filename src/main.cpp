#include "pch.h"

#include "InventoryMenuHook.h"
#include "Settings.h"

namespace
{
	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (a_message && a_message->type == SKSE::MessagingInterface::kDataLoaded) {
			InventoryMenuHook::Install();
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	Settings::Load();

	const auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
		SKSE::log::critical("Unable to register the SKSE message listener");
		return false;
	}

	SKSE::log::info("InventoryRefreshFix loaded");

	return true;
}
