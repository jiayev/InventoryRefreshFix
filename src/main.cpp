#include "pch.h"

#include "InventoryMenuHook.h"
#include "Logging.h"
#include "Settings.h"

namespace
{
	constexpr auto kPluginVersion = "0.5.2";

	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (a_message && a_message->type == SKSE::MessagingInterface::kDataLoaded) {
			SKSE::log::info("Data initialization complete; installing inventory hooks");
			InventoryMenuHook::Install();
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	if (!Logging::Initialize()) {
		return false;
	}

	const auto runtime = a_skse->RuntimeVersion();
#ifdef SKYRIM_SUPPORT_AE
	constexpr auto build = "AE";
	constexpr auto supportedRuntime = SKSE::RUNTIME_SSE_1_6_1170;
#else
	constexpr auto build = "SE";
	constexpr auto supportedRuntime = SKSE::RUNTIME_SSE_1_5_97;
#endif
	if (runtime != supportedRuntime) {
		SKSE::log::critical(
			"Unsupported Skyrim {} runtime {}; inventory hook offsets are verified only for {}",
			build,
			runtime.string(),
			supportedRuntime.string());
		return false;
	}

	SKSE::log::info(
		"InventoryRefreshFix v{} loading ({} build; runtime {})",
		kPluginVersion,
		build,
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
