#include "InventoryEnumeration.h"

#include "pch.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace InventoryEnumeration
{
	namespace
	{
		constexpr std::size_t kMaximumRows = 100000;

		enum class ValidationState
		{
			kPending,
			kRunning,
			kPassed,
			kFailed
		};

		struct EntryDescriptor
		{
			RE::TESBoundObject*              object{ nullptr };
			std::int32_t                     countDelta{ 0 };
			std::vector<RE::ExtraDataList*> extraLists;
		};

		std::atomic<ValidationState> g_validationState{ ValidationState::kPending };

		[[nodiscard]] bool HasLeveledItem(const RE::ExtraDataList* a_extraList)
		{
			return a_extraList && a_extraList->HasType<RE::ExtraLeveledItem>();
		}

		[[nodiscard]] bool FirstExtraListHasLeveledItem(const RE::InventoryEntryData& a_entry)
		{
			return a_entry.extraLists && !a_entry.extraLists->empty() &&
			       HasLeveledItem(a_entry.extraLists->front());
		}

		[[nodiscard]] bool AnyExtraListHasLeveledItem(const RE::InventoryEntryData& a_entry)
		{
			if (!a_entry.extraLists) {
				return false;
			}

			for (const auto* extraList : *a_entry.extraLists) {
				if (!extraList) {
					break;
				}
				if (HasLeveledItem(extraList)) {
					return true;
				}
			}

			return false;
		}

		[[nodiscard]] std::vector<RE::ExtraDataList*> CollectExtraLists(
			const RE::InventoryEntryData& a_entry,
			const bool a_stackableOnly)
		{
			std::vector<RE::ExtraDataList*> result;
			if (!a_entry.extraLists) {
				return result;
			}

			for (auto* extraList : *a_entry.extraLists) {
				if (!extraList) {
					break;
				}
				if (!a_stackableOnly || extraList->IsInventoryStackable(true)) {
					result.push_back(extraList);
				}
			}

			return result;
		}

		[[nodiscard]] RE::InventoryEntryData* Materialize(const EntryDescriptor& a_descriptor)
		{
			auto* result = new RE::InventoryEntryData(a_descriptor.object, a_descriptor.countDelta);
			for (auto iter = a_descriptor.extraLists.rbegin(); iter != a_descriptor.extraLists.rend(); ++iter) {
				result->AddExtraList(*iter);
			}
			return result;
		}

		[[nodiscard]] bool Matches(
			const EntryDescriptor& a_expected,
			const RE::InventoryEntryData& a_actual,
			std::string& a_reason)
		{
			if (a_expected.object != a_actual.object) {
				a_reason = "object differs";
				return false;
			}
			if (a_expected.countDelta != a_actual.countDelta) {
				a_reason = "count differs";
				return false;
			}

			std::vector<RE::ExtraDataList*> actualExtraLists;
			if (a_actual.extraLists) {
				for (auto* extraList : *a_actual.extraLists) {
					if (!extraList) {
						break;
					}
					actualExtraLists.push_back(extraList);
				}
			}
			if (a_expected.extraLists != actualExtraLists) {
				a_reason = "ExtraDataList order differs";
				return false;
			}

			return true;
		}
	}

	class Session::Impl
	{
	public:
		[[nodiscard]] RE::InventoryEntryData* Get(
			RE::InventoryChanges* a_changes,
			const std::int32_t a_index,
			const OriginalFunction a_original,
			const bool a_enabled,
			const bool a_validate)
		{
			if (!a_enabled) {
				_status = "original/disabled";
				return a_original(a_changes, a_index);
			}
			if (!a_changes || a_index < 0) {
				_status = "original/invalid request";
				return a_original(a_changes, a_index);
			}

			const auto validationState = g_validationState.load(std::memory_order_acquire);
			if (a_validate && validationState == ValidationState::kFailed) {
				_status = "original/validation failed";
				return a_original(a_changes, a_index);
			}
			if (a_validate && validationState == ValidationState::kRunning) {
				_status = "original/validation busy";
				_fallback = true;
				return a_original(a_changes, a_index);
			}
			if (_fallback) {
				return a_original(a_changes, a_index);
			}

			if (!_prepared) {
				_changes = a_changes;
				if (!Build(a_changes)) {
					_status = "original/materialization failed";
					_fallback = true;
					return a_original(a_changes, a_index);
				}
				_prepared = true;

				if (a_validate && validationState == ValidationState::kPending) {
					auto expected = ValidationState::kPending;
					if (g_validationState.compare_exchange_strong(
							expected,
							ValidationState::kRunning,
							std::memory_order_acq_rel)) {
						if (!Validate(a_changes, a_original)) {
							g_validationState.store(ValidationState::kFailed, std::memory_order_release);
							_status = "original/validation failed";
							_fallback = true;
							return a_original(a_changes, a_index);
						}

						g_validationState.store(ValidationState::kPassed, std::memory_order_release);
						SKSE::log::info(
							"Bulk inventory enumeration validated against {} native rows",
							_rows.size());
						_status = "bulk/validated";
					}
				}
			}

			if (_fallback || a_changes != _changes) {
				if (a_changes != _changes) {
					_status = "original/multiple inventories";
				}
				return a_original(a_changes, a_index);
			}

			if (_status != "bulk/validated") {
				_status = a_validate ? "bulk" : "bulk/unvalidated";
			}
			if (static_cast<std::size_t>(a_index) >= _rows.size()) {
				return nullptr;
			}

			return Materialize(_rows[static_cast<std::size_t>(a_index)]);
		}

		[[nodiscard]] std::string_view GetStatus() const noexcept { return _status; }

	private:
		[[nodiscard]] bool Append(EntryDescriptor a_descriptor)
		{
			if (_rows.size() >= kMaximumRows || !a_descriptor.object) {
				return false;
			}
			_rows.push_back(std::move(a_descriptor));
			return true;
		}

		[[nodiscard]] bool AppendDistinctExtraLists(
			RE::InventoryEntryData& a_entry,
			std::int32_t& a_nonStackableCount,
			const bool a_positiveCountsOnly,
			const std::int32_t a_maximumRowCount)
		{
			a_nonStackableCount = 0;
			if (!a_entry.extraLists) {
				return true;
			}

			for (auto* extraList : *a_entry.extraLists) {
				if (!extraList) {
					break;
				}
				if (!extraList->IsInventoryStackable(true)) {
					const auto count = extraList->GetCount();
					if (!a_positiveCountsOnly || count > 0) {
						a_nonStackableCount += count;
					}
					if (count > 0 && !Append({
						a_entry.object,
						std::min(count, a_maximumRowCount),
						{ extraList }
					})) {
						return false;
					}
				}
			}

			return true;
		}

		[[nodiscard]] bool AppendBaseEntry(
			RE::InventoryChanges& a_changes,
			RE::TESBoundObject* a_object,
			const std::int32_t a_baseCount,
			RE::InventoryEntryData* a_change)
		{
			if (!a_change) {
				return Append({ a_object, std::abs(a_baseCount), {} });
			}
			if (a_baseCount <= 0 || a_baseCount + a_change->countDelta <= 0 ||
			    FirstExtraListHasLeveledItem(*a_change)) {
				return true;
			}

			const auto distinctListCount = a_change->NormalizeAndCountNonStackableExtraLists();
			if (distinctListCount == 0) {
				std::int32_t nonStackableCount = 0;
				if (a_change->extraLists) {
					for (auto* extraList : *a_change->extraLists) {
						if (!extraList) {
							break;
						}
						if (!extraList->IsInventoryStackable(true)) {
							nonStackableCount += extraList->GetCount();
						}
					}
				}

				EntryDescriptor descriptor{
					a_object,
					a_baseCount + a_change->countDelta - nonStackableCount,
					{}
				};
				if (a_change->GetFavoriteExtraList() ||
				    a_change->GetOwner() != static_cast<RE::TESForm*>(a_changes.owner)) {
					// This native branch copies with repeated head insertion.
					descriptor.extraLists = CollectExtraLists(*a_change, false);
					std::ranges::reverse(descriptor.extraLists);
				}
				return Append(std::move(descriptor));
			}

			const auto totalCount = a_baseCount + a_change->countDelta;
			std::int32_t nonStackableCount = 0;
			if (!AppendDistinctExtraLists(*a_change, nonStackableCount, false, totalCount)) {
				return false;
			}

			if (nonStackableCount < totalCount &&
			    (!AnyExtraListHasLeveledItem(*a_change) || a_change->countDelta <= a_baseCount)) {
				return Append({
					a_object,
					totalCount - nonStackableCount,
					CollectExtraLists(*a_change, true)
				});
			}

			return true;
		}

		[[nodiscard]] bool AppendChangeOnlyEntry(
			RE::InventoryEntryData& a_entry,
			const std::int32_t a_baseCount,
			const bool a_containerHasObject,
			const bool a_hasContainer)
		{
			if (a_entry.countDelta <= 0 && a_baseCount >= 0) {
				return true;
			}
			if (!FirstExtraListHasLeveledItem(a_entry) && a_baseCount >= 0 &&
			    a_hasContainer && a_containerHasObject) {
				return true;
			}

			if (a_entry.NormalizeAndCountNonStackableExtraLists() < 0) {
				return false;
			}
			std::int32_t nonStackableCount = 0;
			if (!AppendDistinctExtraLists(
					a_entry,
					nonStackableCount,
					true,
					std::numeric_limits<std::int32_t>::max())) {
				return false;
			}

			const auto residualCount = a_entry.countDelta - nonStackableCount;
			return residualCount <= 0 || Append({
				a_entry.object,
				residualCount,
				CollectExtraLists(a_entry, true)
			});
		}

		[[nodiscard]] bool Build(RE::InventoryChanges* a_changes)
		{
			auto* container = a_changes->owner ? a_changes->owner->GetContainer() : nullptr;

			std::unordered_map<RE::TESBoundObject*, RE::InventoryEntryData*> changesByObject;
			if (a_changes->entryList) {
				for (auto* entry : *a_changes->entryList) {
					if (!entry) {
						continue;
					}
					if (!entry->object) {
						return false;
					}
					changesByObject.try_emplace(entry->object, entry);
				}
			}

			std::vector<RE::TESBoundObject*>                      baseObjects;
			std::unordered_map<RE::TESBoundObject*, std::int32_t> baseCounts;
			if (container) {
				if (container->numContainerObjects != 0 && !container->containerObjects) {
					return false;
				}
				// CollectUniqueInventoryObjects followed by GetObjectCount repeats the
				// container scan. Aggregate duplicate CNTO records here while retaining
				// the first record's order.
				baseObjects.reserve(container->numContainerObjects);
				baseCounts.reserve(container->numContainerObjects);
				for (std::uint32_t index = 0; index < container->numContainerObjects; ++index) {
					auto* containerObject = container->containerObjects[index];
					if (!containerObject || !containerObject->obj) {
						return false;
					}

					auto [count, inserted] = baseCounts.try_emplace(containerObject->obj, containerObject->count);
					if (inserted) {
						baseObjects.push_back(containerObject->obj);
					} else {
						count->second += containerObject->count;
					}
				}
				_rows.reserve(baseObjects.size() + changesByObject.size());

				for (auto* object : baseObjects) {
					if (object->Is(RE::FormType::LeveledItem)) {
						continue;
					}

					auto change = changesByObject.find(object);
					if (!AppendBaseEntry(
							*a_changes,
							object,
							baseCounts.at(object),
							change != changesByObject.end() ? change->second : nullptr)) {
						return false;
					}
				}
			}
			if (!container) {
				_rows.reserve(changesByObject.size());
			}

			if (a_changes->entryList) {
				for (auto* entry : *a_changes->entryList) {
					if (!entry) {
						break;
					}

					const auto baseCount = baseCounts.contains(entry->object) ? baseCounts.at(entry->object) : 0;
					if (!AppendChangeOnlyEntry(
							*entry,
							baseCount,
							baseCounts.contains(entry->object),
							container != nullptr)) {
						return false;
					}
				}
			}

			return true;
		}

		[[nodiscard]] bool Validate(
			RE::InventoryChanges* a_changes,
			const OriginalFunction a_original)
		{
			for (std::size_t index = 0; index <= _rows.size(); ++index) {
				std::unique_ptr<RE::InventoryEntryData> original{
					a_original(a_changes, static_cast<std::int32_t>(index))
				};
				if (index == _rows.size()) {
					if (original) {
						SKSE::log::critical(
							"Bulk inventory enumeration validation failed at row {}: native sequence has more rows",
							index);
						return false;
					}
					return true;
				}
				if (!original) {
					SKSE::log::critical(
						"Bulk inventory enumeration validation failed at row {}: native sequence ended early",
						index);
					return false;
				}

				std::string reason;
				if (!Matches(_rows[index], *original, reason)) {
					SKSE::log::critical(
						"Bulk inventory enumeration validation failed at row {}: {}",
						index,
						reason);
					return false;
				}
			}

			return false;
		}

		RE::InventoryChanges*       _changes{ nullptr };
		std::vector<EntryDescriptor> _rows;
		std::string_view             _status{ "original/disabled" };
		bool                         _prepared{ false };
		bool                         _fallback{ false };
	};

	Session::Session() :
		_impl(std::make_unique<Impl>())
	{}

	Session::~Session() = default;

	RE::InventoryEntryData* Session::Get(
		RE::InventoryChanges* a_changes,
		const std::int32_t a_index,
		const OriginalFunction a_original,
		const bool a_enabled,
		const bool a_validate)
	{
		return _impl->Get(a_changes, a_index, a_original, a_enabled, a_validate);
	}

	std::string_view Session::GetStatus() const noexcept
	{
		return _impl->GetStatus();
	}
}
