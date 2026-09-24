#include "pch.h"

#include "InventoryMenuHook.h"
#include "Logging.h"
#include "Runtime.h"
#include "Settings.h"

namespace
{
	constexpr auto kPluginVersion = "0.7.1";

	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (a_message && a_message->type == SKSE::MessagingInterface::kDataLoaded) {
			SKSE::log::info("Data initialization complete; installing inventory hooks");
			InventoryMenuHook::Install();
		}
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, { .log = false, .trampoline = true, .trampolineSize = 1024 });
	if (!Logging::Initialize()) {
		return false;
	}

	const auto runtime = a_skse->RuntimeVersion();
	if (!Runtime::GetHookOffsets(runtime)) {
		SKSE::log::critical(
			"Unsupported Skyrim runtime {}; no inventory hook profile is available",
			runtime.string());
		return false;
	}
	if (runtime.major() == 1 && (runtime.minor() == 6 || runtime.minor() == 7)) {
		SKSE::log::info("AE runtime: using shared call-site layout verified against 1.6.1170 and 1.7.104; installation checks enabled");
	}

	SKSE::log::info(
		"InventoryRefreshFix v{} loading (NG build; runtime {})",
		kPluginVersion,
		runtime.string());
	Settings::Load();

	const auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
		SKSE::log::critical("Unable to register the SKSE message listener");
		return false;
	}

	SKSE::log::info("InventoryRefreshFix loaded; waiting for data initialization");

	return true;
}
