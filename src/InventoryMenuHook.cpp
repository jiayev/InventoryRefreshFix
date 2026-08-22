#include "InventoryMenuHook.h"

#include "Settings.h"
#include "pch.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cwchar>
#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace InventoryMenuHook
{
	namespace
	{
		using ProcessMessage_t = RE::UI_MESSAGE_RESULTS (*)(RE::InventoryMenu*, RE::UIMessage&);
		using GetInventoryItemAt_t = RE::InventoryEntryData* (*)(RE::InventoryChanges*, std::int32_t);
		using RefreshMenu_t = void (*)(RE::InventoryMenu*);
		using UpdatePlayer3D_t = void (*)(RE::AIProcess*, RE::Actor*);
		using RemoveElements_t = bool (*)(RE::GFxValue::ObjectInterface*, void*, std::uint32_t, std::int32_t);
		using PushBack_t = bool (*)(RE::GFxValue::ObjectInterface*, void*, const RE::GFxValue&);

		constexpr auto kSlowRefreshThreshold = std::chrono::milliseconds{ 5 };
		constexpr auto kInventoryListsTwoPanels = 2;
		constexpr auto kInventoryListsTransitioningToTwoPanels = 5;
		constexpr auto kNativeSnapshotMember = "__InventoryRefreshFixNativeSnapshot";
		constexpr std::array kNativeIdentityMembers{ "formId", "text", "filterFlag" };

		struct ItemTopologyEntry
		{
			const RE::TESBoundObject* object;
			std::uint32_t             filterFlag;

			friend bool operator==(const ItemTopologyEntry&, const ItemTopologyEntry&) = default;
		};

		struct ItemTopologyEntryHash
		{
			std::size_t operator()(const ItemTopologyEntry& a_entry) const noexcept
			{
				const auto objectHash = std::hash<const RE::TESBoundObject*>{}(a_entry.object);
				const auto filterHash = std::hash<std::uint32_t>{}(a_entry.filterFlag);
				return objectHash ^ (filterHash + 0x9E3779B9 + (objectHash << 6) + (objectHash >> 2));
			}
		};

		struct RefreshProfile
		{
			std::chrono::steady_clock::duration enumerationTime{};
			std::chrono::steady_clock::duration itemListTime{};
			std::chrono::steady_clock::duration scaleformClearTime{};
			std::chrono::steady_clock::duration scaleformPushTime{};
			std::chrono::steady_clock::duration scaleformInvalidateTime{};
			std::chrono::steady_clock::duration bottomBarTime{};
			std::chrono::steady_clock::duration player3DTime{};
			std::uint32_t                       enumerationCalls{ 0 };
			std::uint32_t                       scaleformPushCalls{ 0 };
			std::uint32_t                       scaleformChangedEntries{ 0 };
			RE::InventoryMenu*                  menu{ nullptr };
			std::vector<ItemTopologyEntry>      itemTopology;
			std::vector<RE::GFxValue>           scaleformEntries;
			bool                                itemTopologyCaptured{ false };
			bool                                scaleformEntriesCaptured{ false };
			bool                                allowIncrementalInvalidation{ false };
			std::string_view                    incrementalInvalidationStatus{ "full/disabled" };
		};

		enum class SkyUIEntryCacheResult
		{
			kNotApplicable,
			kPrepared,
			kFailed
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

		void CaptureItemTopology(RefreshProfile& a_profile, RE::InventoryMenu* a_menu)
		{
			a_profile.itemTopology.clear();
			a_profile.scaleformEntries.clear();
			a_profile.itemTopologyCaptured = false;
			a_profile.scaleformEntriesCaptured = false;

			if (!a_menu || !a_menu->itemList) {
				return;
			}

			const auto& items = a_menu->itemList->items;
			a_profile.itemTopology.reserve(items.size());
			for (auto* item : items) {
				if (!item || !item->data.objDesc || !item->data.objDesc->object) {
					a_profile.itemTopology.clear();
					return;
				}

				a_profile.itemTopology.push_back({
					item->data.objDesc->object,
					item->data.GetFilterFlag()
				});
			}

			a_profile.itemTopologyCaptured = true;

			const auto& entryList = a_menu->itemList->entryList;
			if (!entryList.IsArray()) {
				return;
			}

			const auto entryCount = entryList.GetArraySize();
			if (static_cast<std::size_t>(entryCount) != items.size()) {
				return;
			}

			a_profile.scaleformEntries.reserve(items.size());
			for (std::uint32_t i = 0; i < entryCount; ++i) {
				RE::GFxValue entry;
				if (!entryList.GetElement(i, std::addressof(entry)) || !entry.IsObject() ||
				    !items[i]->obj.IsObject() || !(entry == items[i]->obj)) {
					a_profile.scaleformEntries.clear();
					return;
				}

				a_profile.scaleformEntries.push_back(std::move(entry));
			}

			a_profile.scaleformEntriesCaptured = true;
		}

		bool HasMatchingItemTopology(RefreshProfile& a_profile)
		{
			if (!a_profile.itemTopologyCaptured || !a_profile.menu || !a_profile.menu->itemList) {
				a_profile.incrementalInvalidationStatus = "full/no topology snapshot";
				return false;
			}

			const auto& items = a_profile.menu->itemList->items;
			if (items.size() != a_profile.itemTopology.size()) {
				a_profile.incrementalInvalidationStatus = "full/item count changed";
				return false;
			}

			for (std::size_t i = 0; i < items.size(); ++i) {
				auto* item = items[i];
				if (!item || !item->data.objDesc ||
				    ItemTopologyEntry{ item->data.objDesc->object, item->data.GetFilterFlag() } !=
						a_profile.itemTopology[i]) {
					a_profile.incrementalInvalidationStatus = "full/item topology changed";
					return false;
				}
			}

			return true;
		}

		bool IsPrimitiveValue(const RE::GFxValue& a_value)
		{
			return a_value.IsUndefined() || a_value.IsNull() || a_value.IsBool() ||
			       a_value.IsNumber() || a_value.IsString() || a_value.IsStringW();
		}

		bool ArePrimitiveValuesEqual(const RE::GFxValue& a_lhs, const RE::GFxValue& a_rhs)
		{
			if (a_lhs.GetType() != a_rhs.GetType()) {
				return false;
			}
			if (a_lhs.IsUndefined() || a_lhs.IsNull()) {
				return true;
			}
			if (a_lhs.IsBool()) {
				return a_lhs.GetBool() == a_rhs.GetBool();
			}
			if (a_lhs.IsNumber()) {
				return a_lhs.GetNumber() == a_rhs.GetNumber();
			}
			if (a_lhs.IsString()) {
				return std::strcmp(a_lhs.GetString(), a_rhs.GetString()) == 0;
			}
			if (a_lhs.IsStringW()) {
				return std::wcscmp(a_lhs.GetStringW(), a_rhs.GetStringW()) == 0;
			}

			return false;
		}

		bool CreateNativePrimitiveSnapshot(
			RefreshProfile& a_profile,
			const RE::GFxValue& a_entry,
			RE::GFxValue& a_snapshot)
		{
			if (!a_profile.menu || !a_profile.menu->uiMovie) {
				return false;
			}

			a_profile.menu->uiMovie->CreateObject(std::addressof(a_snapshot));
			if (!a_snapshot.IsObject()) {
				return false;
			}

			// SkyUI overwrites some native fields while extending an entry. Keep the
			// pre-processor values separately so later refreshes compare like with like.
			bool succeeded = true;
			a_entry.VisitMembers([&a_snapshot, &succeeded](const char* a_name, const RE::GFxValue& a_value) {
				if (succeeded && std::strcmp(a_name, kNativeSnapshotMember) != 0 && IsPrimitiveValue(a_value)) {
					succeeded = a_snapshot.SetMember(a_name, a_value);
				}
			});

			return succeeded;
		}

		bool HasMatchingNativePrimitiveFields(
			const RE::GFxValue& a_entry,
			const RE::GFxValue& a_snapshot)
		{
			bool matches = true;
			a_entry.VisitMembers([&a_snapshot, &matches](const char* a_name, const RE::GFxValue& a_value) {
				if (!matches || std::strcmp(a_name, kNativeSnapshotMember) == 0 || !IsPrimitiveValue(a_value)) {
					return;
				}

				RE::GFxValue previousValue;
				matches = a_snapshot.GetMember(a_name, std::addressof(previousValue)) &&
				          IsPrimitiveValue(previousValue) && ArePrimitiveValuesEqual(previousValue, a_value);
			});
			a_snapshot.VisitMembers([&a_entry, &matches](const char* a_name, const RE::GFxValue& a_value) {
				if (!matches) {
					return;
				}

				RE::GFxValue currentValue;
				matches = a_entry.GetMember(a_name, std::addressof(currentValue)) &&
				          IsPrimitiveValue(currentValue) && ArePrimitiveValuesEqual(currentValue, a_value);
			});

			return matches;
		}

		bool HasMatchingNativeIdentityFields(
			const RE::GFxValue& a_entry,
			const RE::GFxValue& a_snapshot)
		{
			bool comparedAny = false;
			for (const auto* name : kNativeIdentityMembers) {
				RE::GFxValue currentValue;
				RE::GFxValue previousValue;
				if (!a_snapshot.GetMember(name, std::addressof(previousValue)) ||
				    !IsPrimitiveValue(previousValue)) {
					continue;
				}

				comparedAny = true;
				if (!a_entry.GetMember(name, std::addressof(currentValue)) ||
				    !IsPrimitiveValue(currentValue) || !ArePrimitiveValuesEqual(currentValue, previousValue)) {
					return false;
				}
			}

			return comparedAny;
		}

		bool IsProcessedSkyUIEntry(const RE::GFxValue& a_entry, std::uint32_t a_filterFlag)
		{
			if (a_filterFlag == 0) {
				return true;
			}

			RE::GFxValue processed;
			return a_entry.GetMember("skyui_itemDataProcessed", std::addressof(processed)) &&
			       processed.IsBool() && processed.GetBool();
		}

		bool AttachNativePrimitiveSnapshot(RefreshProfile& a_profile, RE::GFxValue& a_entry)
		{
			RE::GFxValue snapshot;
			return CreateNativePrimitiveSnapshot(a_profile, a_entry, snapshot) &&
			       a_entry.SetMember(kNativeSnapshotMember, snapshot);
		}

		bool EnsureSkyUIEntrySnapshots(RefreshProfile& a_profile)
		{
			if (!a_profile.menu || !a_profile.menu->itemList) {
				return false;
			}

			auto* itemList = a_profile.menu->itemList;
			RE::GFxValue dataProcessors;
			RE::GFxValue listEnumeration;
			if (!itemList->root.GetMember("_dataProcessors", std::addressof(dataProcessors)) ||
			    !itemList->root.GetMember("listEnumeration", std::addressof(listEnumeration)) ||
			    !dataProcessors.IsArray() || dataProcessors.GetArraySize() == 0 ||
			    !listEnumeration.IsObject() || !itemList->entryList.IsArray()) {
				return false;
			}

			auto& entryList = itemList->entryList;
			const auto entryCount = entryList.GetArraySize();
			for (std::uint32_t i = 0; i < entryCount; ++i) {
				RE::GFxValue entry;
				RE::GFxValue snapshot;
				if (!entryList.GetElement(i, std::addressof(entry)) || !entry.IsObject()) {
					return false;
				}
				if (entry.GetMember(kNativeSnapshotMember, std::addressof(snapshot)) && snapshot.IsObject()) {
					continue;
				}
				if (IsProcessedSkyUIEntry(entry, 1)) {
					// A processed object no longer contains a trustworthy copy of every native
					// field. Wait until the game materializes a new raw object before seeding it.
					continue;
				}

				if (!AttachNativePrimitiveSnapshot(a_profile, entry)) {
					return false;
				}
			}

			return true;
		}

		SkyUIEntryCacheResult PrepareSkyUIEntryCache(RefreshProfile& a_profile)
		{
			auto* itemList = a_profile.menu->itemList;
			RE::GFxValue dataProcessors;
			RE::GFxValue listEnumeration;
			if (!itemList->root.GetMember("_dataProcessors", std::addressof(dataProcessors)) ||
			    !itemList->root.GetMember("listEnumeration", std::addressof(listEnumeration))) {
				return SkyUIEntryCacheResult::kNotApplicable;
			}
			if (!dataProcessors.IsArray() || dataProcessors.GetArraySize() == 0 ||
			    !listEnumeration.IsObject()) {
				a_profile.incrementalInvalidationStatus = "full/unsupported SkyUI list";
				return SkyUIEntryCacheResult::kFailed;
			}
			if (!a_profile.scaleformEntriesCaptured) {
				a_profile.incrementalInvalidationStatus = "full/no Scaleform snapshot";
				return SkyUIEntryCacheResult::kFailed;
			}

			auto& entryList = itemList->entryList;
			if (!entryList.IsArray()) {
				a_profile.incrementalInvalidationStatus = "full/invalid Scaleform list";
				return SkyUIEntryCacheResult::kFailed;
			}

			const auto entryCount = entryList.GetArraySize();
			if (static_cast<std::size_t>(entryCount) != itemList->items.size()) {
				a_profile.incrementalInvalidationStatus = "full/native and Scaleform counts differ";
				return SkyUIEntryCacheResult::kFailed;
			}

			std::vector<RE::GFxValue> newEntries;
			newEntries.reserve(entryCount);
			for (std::uint32_t i = 0; i < entryCount; ++i) {
				RE::GFxValue entry;
				if (!entryList.GetElement(i, std::addressof(entry)) || !entry.IsObject()) {
					a_profile.incrementalInvalidationStatus = "full/invalid Scaleform entry";
					return SkyUIEntryCacheResult::kFailed;
				}

				newEntries.push_back(std::move(entry));
			}

			auto restoreNewEntries = [&entryList, &itemList, &newEntries]() {
				for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(newEntries.size()); ++i) {
					entryList.SetElement(i, newEntries[i]);
					if (itemList->items[i]) {
						itemList->items[i]->obj = newEntries[i];
					}
				}
			};

			std::vector<std::uint8_t> usedOldEntries(a_profile.scaleformEntries.size(), 0);
			std::unordered_multimap<ItemTopologyEntry, std::size_t, ItemTopologyEntryHash> oldEntryIndices;
			oldEntryIndices.reserve(a_profile.itemTopology.size());
			for (std::size_t oldIndex = 0; oldIndex < a_profile.itemTopology.size(); ++oldIndex) {
				oldEntryIndices.emplace(a_profile.itemTopology[oldIndex], oldIndex);
			}

			std::uint32_t changedEntries = 0;
			const bool nativeFullRebuild = a_profile.enumerationCalls != 0;
			for (std::uint32_t i = 0; i < entryCount; ++i) {
				auto& newEntry = newEntries[i];
				auto* item = itemList->items[i];
				if (!item || !item->data.objDesc || !item->data.objDesc->object ||
				    !item->obj.IsObject() || !(item->obj == newEntry)) {
					restoreNewEntries();
					a_profile.incrementalInvalidationStatus = "full/invalid native entry";
					return SkyUIEntryCacheResult::kFailed;
				}

				const ItemTopologyEntry newTopology{
					item->data.objDesc->object,
					item->data.GetFilterFlag()
				};
				std::size_t matchingIndex = a_profile.scaleformEntries.size();
				bool exactObjectMatch = false;
				const auto [firstCandidate, lastCandidate] = oldEntryIndices.equal_range(newTopology);
				for (auto candidate = firstCandidate; candidate != lastCandidate; ++candidate) {
					const auto oldIndex = candidate->second;
					if (usedOldEntries[oldIndex]) {
						continue;
					}

					auto& oldEntry = a_profile.scaleformEntries[oldIndex];
					if (oldEntry == newEntry) {
						matchingIndex = oldIndex;
						exactObjectMatch = true;
						break;
					}
				}

				if (!exactObjectMatch) {
					for (auto candidate = firstCandidate; candidate != lastCandidate; ++candidate) {
						const auto oldIndex = candidate->second;
						if (usedOldEntries[oldIndex]) {
							continue;
						}

						auto& oldEntry = a_profile.scaleformEntries[oldIndex];
						RE::GFxValue oldSnapshot;
						if (oldEntry.GetMember(kNativeSnapshotMember, std::addressof(oldSnapshot)) &&
						    oldSnapshot.IsObject() && HasMatchingNativeIdentityFields(newEntry, oldSnapshot)) {
							matchingIndex = oldIndex;
							break;
						}
					}
				}

				if (matchingIndex == a_profile.scaleformEntries.size()) {
					if (!AttachNativePrimitiveSnapshot(a_profile, newEntry)) {
						restoreNewEntries();
						a_profile.incrementalInvalidationStatus = "full/Scaleform snapshot failed";
						return SkyUIEntryCacheResult::kFailed;
					}
					++changedEntries;
					continue;
				}

				usedOldEntries[matchingIndex] = 1;
				auto& oldEntry = a_profile.scaleformEntries[matchingIndex];
				if (exactObjectMatch) {
					if (!IsProcessedSkyUIEntry(oldEntry, newTopology.filterFlag)) {
						if (!AttachNativePrimitiveSnapshot(a_profile, newEntry)) {
							restoreNewEntries();
							a_profile.incrementalInvalidationStatus = "full/Scaleform snapshot failed";
							return SkyUIEntryCacheResult::kFailed;
						}
						++changedEntries;
					}
					continue;
				}

				RE::GFxValue oldSnapshot;
				// On the native partial-update path, every replacement belongs to a form the
				// game explicitly marked dirty. Only full rebuilds may reuse a replacement
				// after comparing its raw primitive data with the previous snapshot.
				const bool canReuse =
					nativeFullRebuild &&
					oldEntry.GetMember(kNativeSnapshotMember, std::addressof(oldSnapshot)) &&
					oldSnapshot.IsObject() && HasMatchingNativePrimitiveFields(newEntry, oldSnapshot) &&
					IsProcessedSkyUIEntry(oldEntry, newTopology.filterFlag);
				if (canReuse) {
					if (!entryList.SetElement(i, oldEntry)) {
						restoreNewEntries();
						a_profile.incrementalInvalidationStatus = "full/Scaleform cache restore failed";
						return SkyUIEntryCacheResult::kFailed;
					}
					item->obj = oldEntry;
					continue;
				}

				if (!AttachNativePrimitiveSnapshot(a_profile, newEntry)) {
					restoreNewEntries();
					a_profile.incrementalInvalidationStatus = "full/Scaleform snapshot failed";
					return SkyUIEntryCacheResult::kFailed;
				}
				++changedEntries;
			}

			// SkyUI's first data processor uses skyui_itemDataProcessed as its item-card
			// cache key. Reusing only unchanged objects lets the original invalidation process
			// changed entries from scratch while preserving its filtering, sorting, category,
			// selection, and custom-processor behavior.
			a_profile.scaleformChangedEntries = changedEntries;
			a_profile.incrementalInvalidationStatus = "cached entries";
			return SkyUIEntryCacheResult::kPrepared;
		}

		bool TryIncrementalInvalidation(RefreshProfile& a_profile)
		{
			if (!a_profile.allowIncrementalInvalidation) {
				return false;
			}

			switch (PrepareSkyUIEntryCache(a_profile)) {
			case SkyUIEntryCacheResult::kPrepared:
			case SkyUIEntryCacheResult::kFailed:
				// SkyUI still runs its original invalidation. A prepared entry cache removes
				// redundant item-card work without bypassing the movie's state machine.
				return false;
			case SkyUIEntryCacheResult::kNotApplicable:
				break;
			}
			if (!HasMatchingItemTopology(a_profile)) {
				return false;
			}

			auto* menu = a_profile.menu;
			auto* itemList = menu->itemList;
			if (!menu->uiMovie || !itemList->root.IsDisplayObject()) {
				a_profile.incrementalInvalidationStatus = "full/movie unavailable";
				return false;
			}

			// RefreshItemList repopulates the same ActionScript array object. When its
			// identity, order, and filter flags are unchanged, the filterer and category
			// availability computed by the previous full invalidation remain valid.
			RE::GFxValue inventoryLists;
			RE::GFxValue currentState;
			RE::GFxValue selectedIndex;
			if (!menu->root.GetMember("InventoryLists_mc", std::addressof(inventoryLists)) &&
			    !menu->root.GetMember("inventoryLists", std::addressof(inventoryLists))) {
				a_profile.incrementalInvalidationStatus = "full/inventory lists unavailable";
				return false;
			}
			if ((!inventoryLists.GetMember("iCurrentState", std::addressof(currentState)) &&
			     !inventoryLists.GetMember("currentState", std::addressof(currentState))) ||
			    !currentState.IsNumber()) {
				a_profile.incrementalInvalidationStatus = "full/list state unavailable";
				return false;
			}
			if (!itemList->root.GetMember("selectedIndex", std::addressof(selectedIndex)) ||
			    !selectedIndex.IsNumber()) {
				a_profile.incrementalInvalidationStatus = "full/selection unavailable";
				return false;
			}

			const auto selection = selectedIndex.GetSInt();
			if (selection < 0 || static_cast<std::size_t>(selection) >= a_profile.itemTopology.size()) {
				a_profile.incrementalInvalidationStatus = "full/no active selection";
				return false;
			}

			const auto state = currentState.GetSInt();
			const char* eventType = nullptr;
			switch (state) {
			case kInventoryListsTwoPanels:
				eventType = "itemHighlightChange";
				break;
			case kInventoryListsTransitioningToTwoPanels:
				eventType = "showItemsList";
				break;
			case 0:
				a_profile.incrementalInvalidationStatus = "full/no panels";
				return false;
			case 1:
				a_profile.incrementalInvalidationStatus = "full/one panel";
				return false;
			case 3:
				a_profile.incrementalInvalidationStatus = "full/transitioning to no panels";
				return false;
			case 4:
				a_profile.incrementalInvalidationStatus = "full/transitioning to one panel";
				return false;
			default:
				a_profile.incrementalInvalidationStatus = "full/unknown list state";
				return false;
			}

			RE::GFxValue event;
			menu->uiMovie->CreateObject(std::addressof(event));
			if (!event.IsObject() ||
			    !event.SetMember("type", RE::GFxValue{ eventType }) ||
			    !event.SetMember("index", selectedIndex)) {
				a_profile.incrementalInvalidationStatus = "full/event creation failed";
				return false;
			}

			// The array identity, order, filter flags, and count are unchanged, so the
			// existing filter and scroll bounds remain valid. Redraw only the visible
			// renderers instead of rescanning every entry in InvalidateData.
			if (!itemList->root.Invoke("UpdateList")) {
				a_profile.incrementalInvalidationStatus = "full/item update failed";
				return false;
			}

			// UpdateList reuses the entry clips under the stationary cursor. Toggle the
			// selection through the public setter so the selected renderer is rearmed for
			// another mouse press, then reproduce the original highlight notification.
			const RE::GFxValue noSelection{ -1 };
			if (!itemList->root.SetMember("selectedIndex", noSelection) ||
			    !itemList->root.SetMember("selectedIndex", selectedIndex)) {
				a_profile.incrementalInvalidationStatus = "full/selection restore failed";
				return false;
			}

			const std::array args{ event };
			if (!inventoryLists.Invoke("dispatchEvent", args)) {
				a_profile.incrementalInvalidationStatus = "full/highlight dispatch failed";
				return false;
			}

			a_profile.incrementalInvalidationStatus = "incremental";
			return true;
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
			const auto scaleformClearMilliseconds =
				std::chrono::duration<double, std::milli>(a_profile.scaleformClearTime).count();
			const auto scaleformPushMilliseconds =
				std::chrono::duration<double, std::milli>(a_profile.scaleformPushTime).count();
			const auto scaleformInvalidateMilliseconds =
				std::chrono::duration<double, std::milli>(a_profile.scaleformInvalidateTime).count();
			const auto itemListOtherMilliseconds =
				itemListMilliseconds - enumerationMilliseconds - scaleformClearMilliseconds -
				scaleformPushMilliseconds - scaleformInvalidateMilliseconds;
			const auto bottomBarMilliseconds =
				std::chrono::duration<double, std::milli>(a_profile.bottomBarTime).count();
			const auto player3DMilliseconds =
				std::chrono::duration<double, std::milli>(a_profile.player3DTime).count();
			const auto otherMilliseconds =
				elapsedMilliseconds - itemListMilliseconds - bottomBarMilliseconds - player3DMilliseconds;

			if (a_elapsed >= kSlowRefreshThreshold) {
				SKSE::log::info(
					"{} inventory message: {} entries in {:.3f} ms "
					"(item list: {:.3f} ms [enumeration: {:.3f} ms / {} calls; GFx clear: {:.3f} ms; "
					"GFx push: {:.3f} ms / {} calls; GFx invalidate: {:.3f} ms / {} / {} changed; "
					"internal other: {:.3f} ms]; "
					"bottom bar: {:.3f} ms; player 3D: {:.3f} ms; other: {:.3f} ms)",
					a_kind,
					itemCount,
					elapsedMilliseconds,
					itemListMilliseconds,
					enumerationMilliseconds,
					a_profile.enumerationCalls,
					scaleformClearMilliseconds,
					scaleformPushMilliseconds,
					a_profile.scaleformPushCalls,
					scaleformInvalidateMilliseconds,
					a_profile.incrementalInvalidationStatus,
					a_profile.scaleformChangedEntries,
					itemListOtherMilliseconds,
					bottomBarMilliseconds,
					player3DMilliseconds,
					otherMilliseconds);
			} else {
				SKSE::log::debug(
					"{} inventory message: {} entries in {:.3f} ms "
					"(item list: {:.3f} ms [enumeration: {:.3f} ms / {} calls; GFx clear: {:.3f} ms; "
					"GFx push: {:.3f} ms / {} calls; GFx invalidate: {:.3f} ms / {} / {} changed; "
					"internal other: {:.3f} ms]; "
					"bottom bar: {:.3f} ms; player 3D: {:.3f} ms; other: {:.3f} ms)",
					a_kind,
					itemCount,
					elapsedMilliseconds,
					itemListMilliseconds,
					enumerationMilliseconds,
					a_profile.enumerationCalls,
					scaleformClearMilliseconds,
					scaleformPushMilliseconds,
					a_profile.scaleformPushCalls,
					scaleformInvalidateMilliseconds,
					a_profile.incrementalInvalidationStatus,
					a_profile.scaleformChangedEntries,
					itemListOtherMilliseconds,
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
				if (g_activeRefreshProfile && g_activeRefreshProfile->allowIncrementalInvalidation) {
					CaptureItemTopology(*g_activeRefreshProfile, a_menu);
				}
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

		class ScaleformListHook
		{
		public:
			static bool RemoveElementsThunk(
				RE::GFxValue::ObjectInterface* a_interface,
				void* a_data,
				std::uint32_t a_index,
				std::int32_t a_count)
			{
				auto* profile = g_activeRefreshProfile;
				if (!profile) {
					return _removeElementsOriginal(a_interface, a_data, a_index, a_count);
				}

				const auto started = std::chrono::steady_clock::now();
				const auto result = _removeElementsOriginal(a_interface, a_data, a_index, a_count);
				profile->scaleformClearTime += std::chrono::steady_clock::now() - started;

				return result;
			}

			static bool PushBackThunk(
				RE::GFxValue::ObjectInterface* a_interface,
				void* a_data,
				const RE::GFxValue& a_value)
			{
				auto* profile = g_activeRefreshProfile;
				if (!profile) {
					return _pushBackOriginal(a_interface, a_data, a_value);
				}

				const auto started = std::chrono::steady_clock::now();
				const auto result = _pushBackOriginal(a_interface, a_data, a_value);
				profile->scaleformPushTime += std::chrono::steady_clock::now() - started;
				++profile->scaleformPushCalls;

				return result;
			}

#ifdef SKYRIM_SUPPORT_AE
			static void InvalidateThunk(
				RE::GFxMovieView* a_movieView,
				const char* a_methodName,
				RE::FxResponseArgsBase& a_args)
			{
				auto* profile = g_activeRefreshProfile;
				if (!profile) {
					return _invalidateOriginal(a_movieView, a_methodName, a_args);
				}

				const auto started = std::chrono::steady_clock::now();
				if (!TryIncrementalInvalidation(*profile)) {
					if (Settings::IsIncrementalInvalidationEnabled()) {
						EnsureSkyUIEntrySnapshots(*profile);
					}
					_invalidateOriginal(a_movieView, a_methodName, a_args);
				}
#else
			static void InvalidateThunk(RE::ItemList* a_itemList)
			{
				auto* profile = g_activeRefreshProfile;
				if (!profile) {
					return _invalidateOriginal(a_itemList);
				}

				const auto started = std::chrono::steady_clock::now();
				if (!TryIncrementalInvalidation(*profile)) {
					if (Settings::IsIncrementalInvalidationEnabled()) {
						EnsureSkyUIEntrySnapshots(*profile);
					}
					_invalidateOriginal(a_itemList);
				}
#endif

				profile->scaleformInvalidateTime += std::chrono::steady_clock::now() - started;
			}

			static bool Install()
			{
				REL::Relocation<std::uintptr_t> refreshItemList{ RELOCATION_ID(50987, 51866) };
				REL::Relocation<std::uintptr_t> removeElements{ RELOCATION_ID(80252, 82280) };
				REL::Relocation<std::uintptr_t> pushBack{ RELOCATION_ID(80248, 82273) };

#ifdef SKYRIM_SUPPORT_AE
				const std::array removeCalls{
					refreshItemList.address() + 0x190,
					refreshItemList.address() + 0x218,
					refreshItemList.address() + 0x2F6
				};
				const std::array pushCalls{
					refreshItemList.address() + 0x1EE,
					refreshItemList.address() + 0x2CC,
					refreshItemList.address() + 0x3A0
				};
				const auto invalidateCall = refreshItemList.address() + 0x452;
				REL::Relocation<std::uintptr_t> invalidate{ REL::Offset(0xFBE900) };
#else
				const auto sortName = GetCallTarget(refreshItemList.address() + 0x65);
				const auto sortValue = GetCallTarget(refreshItemList.address() + 0x83);
				const auto sortWeight = GetCallTarget(refreshItemList.address() + 0xA1);
				const std::array removeCalls{
					sortName + 0x27,
					sortValue + 0x27,
					sortWeight + 0x27
				};
				const std::array pushCalls{
					sortName + 0xBF,
					sortValue + 0xFF,
					sortWeight + 0xFF
				};
				const auto invalidateCall = refreshItemList.address() + 0x11B;
				REL::Relocation<std::uintptr_t> invalidate{ REL::Offset(0x8568D0) };
#endif

				for (const auto call : removeCalls) {
					if (GetCallTarget(call) != removeElements.address()) {
						SKSE::log::critical("Scaleform list-clear call-site validation failed; profiling disabled");
						return false;
					}
				}
				for (const auto call : pushCalls) {
					if (GetCallTarget(call) != pushBack.address()) {
						SKSE::log::critical("Scaleform list-push call-site validation failed; profiling disabled");
						return false;
					}
				}
				if (GetCallTarget(invalidateCall) != invalidate.address()) {
					SKSE::log::critical("Scaleform list-invalidation call-site validation failed; profiling disabled");
					return false;
				}

				auto& trampoline = SKSE::GetTrampoline();
				for (const auto call : removeCalls) {
					const auto original = reinterpret_cast<RemoveElements_t>(
						trampoline.write_call<5>(call, RemoveElementsThunk));
					if (!_removeElementsOriginal) {
						_removeElementsOriginal = original;
					} else if (_removeElementsOriginal != original) {
						SKSE::log::critical("Scaleform list-clear call sites have different targets");
						return false;
					}
				}
				for (const auto call : pushCalls) {
					const auto original = reinterpret_cast<PushBack_t>(
						trampoline.write_call<5>(call, PushBackThunk));
					if (!_pushBackOriginal) {
						_pushBackOriginal = original;
					} else if (_pushBackOriginal != original) {
						SKSE::log::critical("Scaleform list-push call sites have different targets");
						return false;
					}
				}

				_invalidateOriginal = reinterpret_cast<Invalidate_t>(
					trampoline.write_call<5>(invalidateCall, InvalidateThunk));
				return true;
			}

		private:
#ifdef SKYRIM_SUPPORT_AE
			using Invalidate_t = void (*)(RE::GFxMovieView*, const char*, RE::FxResponseArgsBase&);
#else
			using Invalidate_t = void (*)(RE::ItemList*);
#endif

			static inline RemoveElements_t _removeElementsOriginal = nullptr;
			static inline PushBack_t _pushBackOriginal = nullptr;
			static inline Invalidate_t _invalidateOriginal = nullptr;
		};

		class InventoryEnumerationHook
		{
		public:
			static RE::InventoryEntryData* Thunk(RE::InventoryChanges* a_changes, std::int32_t a_index)
			{
				auto* profile = g_activeRefreshProfile;
				if (!profile) {
					return _original(a_changes, a_index);
				}

				const auto started = std::chrono::steady_clock::now();
				auto* result = _original(a_changes, a_index);
				profile->enumerationTime += std::chrono::steady_clock::now() - started;
				++profile->enumerationCalls;

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
					profile.menu = menu.get();
					profile.allowIncrementalInvalidation = Settings::IsIncrementalInvalidationEnabled();
					if (profile.allowIncrementalInvalidation) {
						profile.incrementalInvalidationStatus = "full/not attempted";
					}
					const auto          started = std::chrono::steady_clock::now();
					const auto          itemListStarted = std::chrono::steady_clock::now();
					if (profile.allowIncrementalInvalidation) {
						CaptureItemTopology(profile, menu.get());
					}
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
				profile.menu = a_menu;
				profile.allowIncrementalInvalidation =
					isFullRefresh && Settings::IsIncrementalInvalidationEnabled();
				if (profile.allowIncrementalInvalidation) {
					profile.incrementalInvalidationStatus = "full/not attempted";
				}
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
		SKSE::AllocTrampoline(512);
		const auto phaseProfilingInstalled = RefreshPhaseHook::Install();
		const auto scaleformProfilingInstalled = ScaleformListHook::Install();
		InventoryEnumerationHook::Install();
		ProcessMessageHook::Install();
		SKSE::log::info(
			"Inventory menu refresh phase profiler installed (refresh phases: {}; Scaleform phases: {})",
			phaseProfilingInstalled ? "enabled" : "disabled",
			scaleformProfilingInstalled ? "enabled" : "disabled");
	}
}
