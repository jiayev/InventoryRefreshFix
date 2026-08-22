#include "InventoryMenuHook.h"

#include "Settings.h"
#include "pch.h"

#include <atomic>
#include <chrono>
#include <string_view>

namespace InventoryMenuHook
{
	namespace
	{
		using ProcessMessage_t = RE::UI_MESSAGE_RESULTS (*)(RE::InventoryMenu*, RE::UIMessage&);

		constexpr auto kSlowRefreshThreshold = std::chrono::milliseconds{ 5 };

		bool IsFullItemListRefresh(const RE::UIMessage& a_message)
		{
			if (a_message.type != RE::UI_MESSAGE_TYPE::kInventoryUpdate || !a_message.data) {
				return false;
			}

			// Skyrim rebuilds the complete list only when no individual item is supplied.
			const auto* data = static_cast<const RE::InventoryUpdateData*>(a_message.data);
			return data->updateObj == nullptr;
		}

		void LogRefresh(
			const std::string_view a_kind,
			RE::InventoryMenu* a_menu,
			const std::chrono::steady_clock::duration a_elapsed)
		{
			const auto itemCount = a_menu && a_menu->itemList ? a_menu->itemList->items.size() : 0;
			const auto elapsedMilliseconds = std::chrono::duration<double, std::milli>(a_elapsed).count();

			if (a_elapsed >= kSlowRefreshThreshold) {
				SKSE::log::info("{} inventory refresh: {} entries in {:.3f} ms", a_kind, itemCount, elapsedMilliseconds);
			} else {
				SKSE::log::debug("{} inventory refresh: {} entries in {:.3f} ms", a_kind, itemCount, elapsedMilliseconds);
			}
		}

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

					const auto* ui = RE::UI::GetSingleton();
					if (!ui) {
						return;
					}

					auto menu = ui->GetMenu<RE::InventoryMenu>();
					if (!menu) {
						return;
					}

					const auto started = std::chrono::steady_clock::now();
					menu->RefreshItemList();
					menu->RefreshBottomBar();
					LogRefresh("Coalesced", menu.get(), std::chrono::steady_clock::now() - started);
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
				if (!IsFullItemListRefresh(a_message)) {
					return _original(a_menu, a_message);
				}

				if (Settings::IsRefreshCoalescingEnabled() && FullRefreshQueue::Queue()) {
					return RE::UI_MESSAGE_RESULTS::kHandled;
				}

				const auto started = std::chrono::steady_clock::now();
				const auto result = _original(a_menu, a_message);
				LogRefresh("Full", a_menu, std::chrono::steady_clock::now() - started);

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
		ProcessMessageHook::Install();
		SKSE::log::info("Inventory menu refresh profiler installed");
	}
}
