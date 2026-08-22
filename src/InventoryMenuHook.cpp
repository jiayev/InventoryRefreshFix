#include "InventoryMenuHook.h"

#include "Settings.h"
#include "pch.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <string_view>

namespace InventoryMenuHook
{
	namespace
	{
		using ProcessMessage_t = RE::UI_MESSAGE_RESULTS (*)(RE::InventoryMenu*, RE::UIMessage&);
		using GetInventoryItemAt_t = RE::InventoryEntryData* (*)(RE::InventoryChanges*, std::int32_t);
		using RefreshMenu_t = void (*)(RE::InventoryMenu*);
		using UpdatePlayer3D_t = void (*)(RE::AIProcess*, RE::Actor*);

		constexpr auto kSlowRefreshThreshold = std::chrono::milliseconds{ 5 };

		struct RefreshProfile
		{
			std::chrono::steady_clock::duration enumerationTime{};
			std::chrono::steady_clock::duration itemListTime{};
			std::chrono::steady_clock::duration bottomBarTime{};
			std::chrono::steady_clock::duration player3DTime{};
			std::uint32_t                       enumerationCalls{ 0 };
		};

		thread_local RefreshProfile* g_activeRefreshProfile = nullptr;

		class RefreshProfileScope
		{
		public:
			explicit RefreshProfileScope(RefreshProfile& a_profile) :
				_previous(g_activeRefreshProfile)
			{
				g_activeRefreshProfile = &a_profile;
			}

			~RefreshProfileScope()
			{
				g_activeRefreshProfile = _previous;
			}

		private:
			RefreshProfile* _previous;
		};

		std::uintptr_t GetCallTarget(std::uintptr_t a_callSite)
		{
			if (*reinterpret_cast<const std::uint8_t*>(a_callSite) != 0xE8) {
				return 0;
			}

			std::int32_t displacement;
			std::memcpy(&displacement, reinterpret_cast<const void*>(a_callSite + 1), sizeof(displacement));
			return static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(a_callSite + 5) + displacement);
		}

		bool IsFullItemListRefresh(const RE::UIMessage& a_message)
		{
			if (a_message.type != RE::UI_MESSAGE_TYPE::kInventoryUpdate || !a_message.data) {
				return false;
			}

			const auto* data = static_cast<const RE::InventoryUpdateData*>(a_message.data);
			auto* player = RE::PlayerCharacter::GetSingleton();

			// InventoryMenu ignores updates for other references. A matching update with no
			// individual item takes the complete-list branch in ProcessMessage.
			return player &&
			       data->inventoryRef == player->GetHandle().native_handle() &&
			       data->updateObj == nullptr;
		}

		void LogRefresh(
			const std::string_view a_kind,
			RE::InventoryMenu* a_menu,
			const std::chrono::steady_clock::duration a_elapsed,
			const RefreshProfile& a_profile)
		{
			const auto itemCount = a_menu && a_menu->itemList ? a_menu->itemList->items.size() : 0;
			const auto elapsedMilliseconds = std::chrono::duration<double, std::milli>(a_elapsed).count();
			const auto enumerationMilliseconds =
				std::chrono::duration<double, std::milli>(a_profile.enumerationTime).count();
			const auto itemListMilliseconds =
				std::chrono::duration<double, std::milli>(a_profile.itemListTime).count();
			const auto bottomBarMilliseconds =
				std::chrono::duration<double, std::milli>(a_profile.bottomBarTime).count();
			const auto player3DMilliseconds =
				std::chrono::duration<double, std::milli>(a_profile.player3DTime).count();
			const auto otherMilliseconds =
				elapsedMilliseconds - itemListMilliseconds - bottomBarMilliseconds - player3DMilliseconds;

			if (a_elapsed >= kSlowRefreshThreshold) {
				SKSE::log::info(
					"{} inventory message: {} entries in {:.3f} ms "
					"(item list: {:.3f} ms; enumeration: {:.3f} ms / {} calls; "
					"bottom bar: {:.3f} ms; player 3D: {:.3f} ms; other: {:.3f} ms)",
					a_kind,
					itemCount,
					elapsedMilliseconds,
					itemListMilliseconds,
					enumerationMilliseconds,
					a_profile.enumerationCalls,
					bottomBarMilliseconds,
					player3DMilliseconds,
					otherMilliseconds);
			} else {
				SKSE::log::debug(
					"{} inventory message: {} entries in {:.3f} ms "
					"(item list: {:.3f} ms; enumeration: {:.3f} ms / {} calls; "
					"bottom bar: {:.3f} ms; player 3D: {:.3f} ms; other: {:.3f} ms)",
					a_kind,
					itemCount,
					elapsedMilliseconds,
					itemListMilliseconds,
					enumerationMilliseconds,
					a_profile.enumerationCalls,
					bottomBarMilliseconds,
					player3DMilliseconds,
					otherMilliseconds);
			}
		}

		class RefreshPhaseHook
		{
		public:
			static void RefreshItemListThunk(RE::InventoryMenu* a_menu)
			{
				const auto started = std::chrono::steady_clock::now();
				_refreshItemListOriginal(a_menu);

				if (g_activeRefreshProfile) {
					g_activeRefreshProfile->itemListTime += std::chrono::steady_clock::now() - started;
				}
			}

			static void RefreshBottomBarThunk(RE::InventoryMenu* a_menu)
			{
				const auto started = std::chrono::steady_clock::now();
				_refreshBottomBarOriginal(a_menu);

				if (g_activeRefreshProfile) {
					g_activeRefreshProfile->bottomBarTime += std::chrono::steady_clock::now() - started;
				}
			}

			static void UpdatePlayer3DThunk(RE::AIProcess* a_process, RE::Actor* a_player)
			{
				const auto started = std::chrono::steady_clock::now();
				_updatePlayer3DOriginal(a_process, a_player);

				if (g_activeRefreshProfile) {
					g_activeRefreshProfile->player3DTime += std::chrono::steady_clock::now() - started;
				}
			}

			static bool Install()
			{
				REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_InventoryMenu[0] };
				REL::Relocation<std::uintptr_t> refreshItemList{ RELOCATION_ID(50987, 51866) };
				REL::Relocation<std::uintptr_t> refreshBottomBar{ RELOCATION_ID(50986, 51865) };
				REL::Relocation<std::uintptr_t> updatePlayer3D{ RELOCATION_ID(38404, 39395) };

				const auto processMessage =
					*reinterpret_cast<const std::uintptr_t*>(vtable.address() + 4 * sizeof(std::uintptr_t));

#ifdef SKYRIM_SUPPORT_AE
				constexpr std::ptrdiff_t kOpenItemListOffset = 0x21B;
				constexpr std::ptrdiff_t kOpenBottomBarOffset = 0x223;
				constexpr std::ptrdiff_t kFullItemListOffset = 0xB2B;
				constexpr std::ptrdiff_t kFullBottomBarOffset = 0xB33;
				constexpr std::ptrdiff_t kFullPlayer3DOffset = 0xB5D;
#else
				constexpr std::ptrdiff_t kOpenItemListOffset = 0x134;
				constexpr std::ptrdiff_t kOpenBottomBarOffset = 0x13C;
				constexpr std::ptrdiff_t kFullItemListOffset = 0x785;
				constexpr std::ptrdiff_t kFullBottomBarOffset = 0x78D;
				constexpr std::ptrdiff_t kFullPlayer3DOffset = 0x7B7;
#endif

				const auto openItemListCall = processMessage + kOpenItemListOffset;
				const auto openBottomBarCall = processMessage + kOpenBottomBarOffset;
				const auto fullItemListCall = processMessage + kFullItemListOffset;
				const auto fullBottomBarCall = processMessage + kFullBottomBarOffset;
				const auto fullPlayer3DCall = processMessage + kFullPlayer3DOffset;

				if (GetCallTarget(openItemListCall) != refreshItemList.address() ||
				    GetCallTarget(fullItemListCall) != refreshItemList.address() ||
				    GetCallTarget(openBottomBarCall) != refreshBottomBar.address() ||
				    GetCallTarget(fullBottomBarCall) != refreshBottomBar.address() ||
				    GetCallTarget(fullPlayer3DCall) != updatePlayer3D.address()) {
					SKSE::log::critical("Inventory refresh phase call-site validation failed; detailed profiling disabled");
					return false;
				}

				auto& trampoline = SKSE::GetTrampoline();
				_refreshItemListOriginal = reinterpret_cast<RefreshMenu_t>(
					trampoline.write_call<5>(openItemListCall, RefreshItemListThunk));
				const auto fullItemListOriginal = reinterpret_cast<RefreshMenu_t>(
					trampoline.write_call<5>(fullItemListCall, RefreshItemListThunk));
				_refreshBottomBarOriginal = reinterpret_cast<RefreshMenu_t>(
					trampoline.write_call<5>(openBottomBarCall, RefreshBottomBarThunk));
				const auto fullBottomBarOriginal = reinterpret_cast<RefreshMenu_t>(
					trampoline.write_call<5>(fullBottomBarCall, RefreshBottomBarThunk));
				_updatePlayer3DOriginal = reinterpret_cast<UpdatePlayer3D_t>(
					trampoline.write_call<5>(fullPlayer3DCall, UpdatePlayer3DThunk));

				if (_refreshItemListOriginal != fullItemListOriginal ||
				    _refreshBottomBarOriginal != fullBottomBarOriginal) {
					SKSE::log::critical("Inventory refresh phase call sites have different targets");
					return false;
				}

				return true;
			}

		private:
			static inline RefreshMenu_t _refreshItemListOriginal = nullptr;
			static inline RefreshMenu_t _refreshBottomBarOriginal = nullptr;
			static inline UpdatePlayer3D_t _updatePlayer3DOriginal = nullptr;
		};

		class InventoryEnumerationHook
		{
		public:
			static RE::InventoryEntryData* Thunk(RE::InventoryChanges* a_changes, std::int32_t a_index)
			{
				const auto started = std::chrono::steady_clock::now();
				auto* result = _original(a_changes, a_index);

				if (g_activeRefreshProfile) {
					g_activeRefreshProfile->enumerationTime += std::chrono::steady_clock::now() - started;
					++g_activeRefreshProfile->enumerationCalls;
				}

				return result;
			}

			static void Install()
			{
				REL::Relocation<std::uintptr_t> refreshItemList{ RELOCATION_ID(50987, 51866) };
				REL::Relocation<std::uintptr_t> getInventoryItemAt{ RELOCATION_ID(15866, 16106) };

#ifdef SKYRIM_SUPPORT_AE
				constexpr std::ptrdiff_t kFirstCallOffset = 0xC1;
				constexpr std::ptrdiff_t kLoopCallOffset = 0x11E;
#else
				constexpr std::ptrdiff_t kFirstCallOffset = 0x8A5;
				constexpr std::ptrdiff_t kLoopCallOffset = 0x900;
#endif

				const auto firstCall = refreshItemList.address() + kFirstCallOffset;
				const auto loopCall = refreshItemList.address() + kLoopCallOffset;
				if (GetCallTarget(firstCall) != getInventoryItemAt.address() ||
					GetCallTarget(loopCall) != getInventoryItemAt.address()) {
					SKSE::log::critical("Inventory enumeration call-site validation failed; phase profiling disabled");
					return;
				}

				auto& trampoline = SKSE::GetTrampoline();
				_original = reinterpret_cast<GetInventoryItemAt_t>(
					trampoline.write_call<5>(firstCall, Thunk));
				const auto loopOriginal = reinterpret_cast<GetInventoryItemAt_t>(
					trampoline.write_call<5>(loopCall, Thunk));

				if (_original != loopOriginal) {
					SKSE::log::critical("Inventory enumeration call sites have different targets");
				}
			}

		private:
			static inline GetInventoryItemAt_t _original = nullptr;
		};

		class FullRefreshQueue
		{
		public:
			static bool Queue()
			{
				_requestCount.fetch_add(1, std::memory_order_relaxed);
				if (_queued.exchange(true, std::memory_order_acq_rel)) {
					return true;
				}

				const auto* task = SKSE::GetTaskInterface();
				if (!task) {
					_queued.store(false, std::memory_order_release);
					_requestCount.fetch_sub(1, std::memory_order_relaxed);
					return false;
				}

				task->AddUITask([] {
					const auto requestCount = _requestCount.exchange(0, std::memory_order_acq_rel);
					_queued.store(false, std::memory_order_release);

					auto* ui = RE::UI::GetSingleton();
					if (!ui) {
						return;
					}

					auto menu = ui->GetMenu<RE::InventoryMenu>();
					if (!menu) {
						return;
					}

					RefreshProfile      profile;
					RefreshProfileScope profileScope{ profile };
					const auto          started = std::chrono::steady_clock::now();
					const auto          itemListStarted = std::chrono::steady_clock::now();
					menu->RefreshItemList();
					profile.itemListTime += std::chrono::steady_clock::now() - itemListStarted;
					const auto bottomBarStarted = std::chrono::steady_clock::now();
					menu->RefreshBottomBar();
					profile.bottomBarTime += std::chrono::steady_clock::now() - bottomBarStarted;
					LogRefresh("Coalesced", menu.get(), std::chrono::steady_clock::now() - started, profile);
					SKSE::log::debug("Coalesced {} complete inventory updates", requestCount);
				});

				return true;
			}

		private:
			static inline std::atomic_bool     _queued{ false };
			static inline std::atomic_uint32_t _requestCount{ 0 };
		};

		class ProcessMessageHook
		{
		public:
			static RE::UI_MESSAGE_RESULTS Thunk(RE::InventoryMenu* a_menu, RE::UIMessage& a_message)
			{
				const auto isFullRefresh = IsFullItemListRefresh(a_message);
				const auto isMenuOpen = a_message.type == RE::UI_MESSAGE_TYPE::kShow;

				if (!isFullRefresh && !isMenuOpen) {
					return _original(a_menu, a_message);
				}

				if (isFullRefresh && Settings::IsRefreshCoalescingEnabled() && FullRefreshQueue::Queue()) {
					return RE::UI_MESSAGE_RESULTS::kHandled;
				}

				RefreshProfile      profile;
				RefreshProfileScope profileScope{ profile };
				const auto          started = std::chrono::steady_clock::now();
				const auto result = _original(a_menu, a_message);
				LogRefresh(isMenuOpen ? "Open" : "Full", a_menu, std::chrono::steady_clock::now() - started, profile);

				return result;
			}

			static void Install()
			{
				if (_original) {
					return;
				}

				REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_InventoryMenu[0] };
				_original = reinterpret_cast<ProcessMessage_t>(vtable.write_vfunc(4, Thunk));
			}

		private:
			static inline ProcessMessage_t _original = nullptr;
		};
	}

	void Install()
	{
		SKSE::AllocTrampoline(256);
		const auto phaseProfilingInstalled = RefreshPhaseHook::Install();
		InventoryEnumerationHook::Install();
		ProcessMessageHook::Install();
		SKSE::log::info(
			"Inventory menu refresh phase profiler installed ({})",
			phaseProfilingInstalled ? "detailed phases enabled" : "message timing only");
	}
}
