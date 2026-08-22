# Inventory enumeration

## Current engine path

`InventoryMenu::RefreshItemList` calls `InventoryChanges::GetInventoryItemAt` with consecutive indices until it returns `nullptr`. The function returns a newly allocated `InventoryEntryData`; the menu owns and later destroys that object.

`GetInventoryItemAt` does not index an existing array. Every call rebuilds a unique list of base-container objects and starts the merge from the beginning. It combines the base `TESContainer` counts with the matching `InventoryChanges::entryList` entry, splits non-stackable `ExtraDataList` objects into individual logical rows, emits any remaining stackable count as an aggregate row, and finally processes change-only entries. Requesting rows `0..N` therefore repeats the same prefixes and has near-quadratic cost.

Both supported executables use the same algorithm:

| Function | SE 1.5.97 | AE 1.6.1170 | Address Library IDs |
| --- | ---: | ---: | --- |
| `InventoryChanges::GetInventoryItemAt` | `0x1401E6690` | `0x140233120` | `15866`, `16106` |
| `ExtraDataList::IsInventoryStackable` | `0x14010CC40` | `0x140159B20` | `11452`, `11598` |
| `TESContainer::CollectUniqueInventoryObjects` | `0x140190070` | `0x1401DB8D0` | `14390`, `14543` |
| `InventoryEntryData::GetFavoriteExtraList` | `0x1401D6970` | `0x140223370` | `15760`, `15998` |
| `InventoryEntryData::NormalizeAndCountNonStackableExtraLists` | `0x1401D89F0` | `0x140225470` | `15797`, `16035` |

## Stack semantics

An `ExtraDataList` remains part of the aggregate inventory row only when every contained ExtraData type is accepted by `IsInventoryStackable`. The accepted metadata is `ReferenceHandle`, `OriginalReference`, `Ownership`, `Count`, `TimeLeft`, `LeveledItem`, `Scale`, `Hotkey`, `AliasInstanceArray`, `OutfitItem`, `FromAlias`, `ShouldWear`, and `UniqueID`. `Worn` and `WornLeft` are also accepted when the caller asks to ignore worn state.

Any other ExtraData type makes the list a distinct logical row. This includes gameplay-visible differences such as health, charge, enchantment, poison, soul, and custom display data. A replacement must preserve the original row order, row count, `countDelta`, and ordered ExtraDataList pointers.

For a split row merged with a positive base-container count, the emitted count is `min(ExtraDataList count, base count + countDelta)`. The no-split base merge copies all retained ExtraDataLists by repeated head insertion and therefore reverses their order; residual aggregate rows append stackable lists and preserve their order. These branch-specific ordering rules are part of the native result and are covered by shadow validation.

`NormalizeAndCountNonStackableExtraLists` is not a pure query. It removes `ExtraCount` from a list whose only ExtraData is a count of at least two, deletes the resulting empty list, and then returns the number of positive-count non-stackable lists. A bulk implementation must perform this normalization at the same point as the engine path.

## Optimization boundary

The implemented optimization is a refresh-scoped bulk materializer, not a persistent inventory cache. It performs the same merge once, stores immutable row descriptors, and materializes a fresh caller-owned `InventoryEntryData` for every indexed request from the two existing `RefreshItemList` call sites. The descriptors die with that refresh, so repeated indices remain safe and no native entry outlives the inventory state that produced it.

The first sequence of each game session runs in shadow mode. For the same unchanged inventory state, it compares the bulk sequence against the original indexed sequence using the object pointer, `countDelta`, and ordered ExtraDataList pointer sequence for every row. Any mismatch disables the replacement for the rest of the session and records the first divergent row. Test coverage must include duplicate base CNTO records, negative deltas, leveled items, worn and left-worn stacks, favorites, ownership, renamed items, tempered items, enchantments, charge, poison, soul gems, and change-only entries.

`InventoryChanges::VisitInventory` is not an equivalent shortcut because it omits the owner's base container. `TESObjectREFR::GetInventory` aggregates by base object and does not retain the menu's split-stack ordering. Neither should replace the indexed function.
