#include "InventoryMenuHook.h"

#include "pch.h"

#include <chrono>

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

		class ProcessMessageHook
		{
		public:
			static RE::UI_MESSAGE_RESULTS Thunk(RE::InventoryMenu* a_menu, RE::UIMessage& a_message)
			{
				if (!IsFullItemListRefresh(a_message)) {
					return _original(a_menu, a_message);
				}

				const auto started = std::chrono::steady_clock::now();
				const auto result = _original(a_menu, a_message);
				const auto elapsed = std::chrono::steady_clock::now() - started;
				const auto itemCount = a_menu && a_menu->itemList ? a_menu->itemList->items.size() : 0;
				const auto elapsedMilliseconds = std::chrono::duration<double, std::milli>(elapsed).count();

				if (elapsed >= kSlowRefreshThreshold) {
					SKSE::log::info("Full inventory refresh: {} entries in {:.3f} ms", itemCount, elapsedMilliseconds);
				} else {
					SKSE::log::debug("Full inventory refresh: {} entries in {:.3f} ms", itemCount, elapsedMilliseconds);
				}

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
