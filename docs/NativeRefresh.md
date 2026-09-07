# Native inventory refresh

## Verified boundaries

Ghidra analysis of SE 1.5.97 and AE 1.6.1170 distinguishes the full rebuild from the partial rebuild selected by a nonempty `InventoryMenu::pendingUpdateObjects`. Both branches subsequently sort, evaluate best-in-class candidates, and invalidate the frontend.

Addresses below use the image base `0x140000000`.

| Boundary | SE 1.5.97 | AE 1.6.1170 |
| --- | --- | --- |
| `RefreshItemList` | `0x14088F620` | `0x14092ED00` |
| Partial rebuild | `0x14088F800` | `0x14092F340` |
| Object collector | `0x1401E7210` | `0x140233CA0` |
| Menu call to object collector | `0x14088FFE1` | `0x14092F432` |
| Collector's first indexed call | `0x1401E7248` | `0x140233CD8` |
| Collector's subsequent indexed call | `0x1401E72D7` | `0x140233D5F` |
| Name comparator | `0x140889E60` | `0x140929B20` |
| Name sort at refresh boundary | `0x140854970` (wrapper) | `0x1408ED5A0` (quicksort) |
| Name-sort call in refresh | `0x14088F685` | `0x14092EEAF` |
| Best-in-class candidate visitor | `0x14088C4A0` | `0x14092B7F0` |
| Candidate visitor call in refresh | `0x14088F71A` | `0x14092F0E8` |

## Object collection

The native collector starts at logical row zero, materializes each row with `GetInventoryItemAt`, and frees nonmatching descriptors. Once it has appended at least one matching row, it frees the next nonmatching row and returns. It does not collect later disjoint runs of the same base object. Null target objects leave the output untouched; results append to the existing output array.

These indexed calls were outside the original full-rebuild enumeration hooks. Thus `0 requests` did not establish absence of enumeration work: its cost was included in `internal other`. The 0.5.3 SE log's partial rebuilds had only one native item construction, but still spent 235–653 ms in that remainder.

The replacement hooks only the menu's call to the collector and uses a fresh `InventoryEnumeration::Session`. It retains first-run termination, output order, ownership transfer, and skipped-entry destruction. Existing row validation and fallback apply. Disabling bulk enumeration invokes the native collector. Other callers of the shared collector are unchanged.

## Name sorting

SE partitions at `0x140854100` and recurses at `0x140854300`; AE inlines partitioning into `0x1408ED5A0`. Both choose the first element as pivot. Sorted and nearly sorted inputs can produce one-sided partitions and quadratic comparison counts. Partial rebuilds remove changed objects from the sorted list and append their new rows, creating this input pattern. In the supplied SE log, full-rebuild sorting took 5–6 ms while partial-rebuild sorting took 191–234 ms.

The native name comparator orders byte strings, then unsigned FormIDs, then `InventoryEntryData` addresses. It negates the result for descending order. The replacement calls that comparator directly through `std::sort`; it does not approximate localized names or introduce new tie breakers. Distinct normally owned descriptors have distinct final keys. Weight and value comparators have no equivalent final identity key and remain on their native paths to preserve their tie behavior.

On SE the replacement reproduces the wrapper: remove all GFx array elements, sort native item pointers, append every item's existing GFx object, and clear `updatePending`. On AE the wrapper stays in the engine and only the full-range name-sort call is replaced. No item, inventory descriptor, or processed GFx object is cached by the sorter. Array population and subsequent frontend invalidation still run on both vanilla and SkyUI.

Runtime installation checks direct call targets before patching. The collector additionally checks its entry prologue and both indexed-call targets. Name sorting verifies the native comparator call sites; SE also verifies the partition and recursive calls. These checks cover the known boundaries, not arbitrary edits elsewhere in the functions.

## Remaining work and validation

`CaptureItemTopology` runs inside the item-list timer. It now has a separate counter. Best-in-class evaluation also has a separate candidate-visitor counter; collector construction and final GFx flag publication remain in `internal other`. Candidate evaluation calculates armor/damage and value for the current winner and challenger, so it must not be treated as a UI-only operation or cached across equipment changes without further semantic analysis. Player 3D update costs are independent.

Static review covers both executables' call targets, comparator arguments and result interpretation, output ownership, fallback paths, and non-overlapping timing. No build or game execution is part of that review.

Runtime checks should cover SE and AE with vanilla and SkyUI, repeated equip/unequip without moving the mouse, same-name split stacks, name ascending/descending, value/weight sorts, favorites, stack removal, and menu reopening. Expected active-path labels are `bulk/object` and `bounded/name`. Compare with `bEnableNativeNameSort=0` and `bEnableInventoryEnumeration=0` independently; the first validated enumeration retains the existing deliberate native validation cost.
