# InventoryRefreshFix

Runtime profiling and safe optimizations for Skyrim's expensive full inventory-list refreshes.

The profiler measures the `InventoryMenu` paths used when Skyrim opens the menu or rebuilds the complete Scaleform item list. Each log entry separates item-list rebuilding, indexed inventory materialization, bottom-bar updates, player 3D rebuilding, and other message-handling work.

`lib/commonlibsse` tracks the `ng` branch of [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) and is pinned to [`736dc640`](https://github.com/alandtse/CommonLibSSE-NG/commit/736dc64094e59232abfbcdf796cd0a063e136ec6) (9.0.1). This revision includes the inventory-specific refresh and materialization APIs.

## Runtime support

One DLL handles SE 1.5.97 and AE 1.6.1170 using separate hook signatures selected at runtime. GOG 1.6.1179 is enabled with its own Address Library function addresses and the 1.6.1170 call-site layout. Every hook still checks its expected call targets before installation. The GOG path has not yet been verified in-game.

NG already recognizes 1.7.99 and 1.7.104, but this plugin's hook layouts have not been verified on them. They remain disabled, as do other unlisted versions and VR. Building against NG's SE/AE/VR ABI does not enable unsupported hook profiles. See [Runtime profiles](docs/RuntimeProfiles.md).

## Current scope

- Hooks `InventoryMenu::ProcessMessage` after SKSE's `kDataLoaded` event.
- Times menu opening and player-targeted `kInventoryUpdate` messages with a null `updateObj`, the two paths that perform a complete item-list rebuild.
- Instruments the exact `ProcessMessage` call sites for item-list rebuilding, bottom-bar updates, and player 3D rebuilding on Skyrim SE and AE.
- Instruments both `InventoryChanges::GetInventoryItemAt` call sites inside `InventoryMenu::RefreshItemList` and reports their aggregate duration and call count.
- Replaces the indexed near-quadratic inventory materializer with one refresh-scoped linear pass. The first sequence in a game session is compared row-for-row with the original implementation before subsequent refreshes rely on it.
- Separates Scaleform array clearing, per-entry `PushBack`, and `InvalidateListData` from the remaining native materialization and sorting work.
- Separates native per-entry construction and native sorting from the remaining item-list refresh work.
- Uses the validated bulk materializer for the object-specific collection path used by partial native rebuilds.
- Replaces native name quicksort with `std::sort`, retaining the game's name, FormID, descriptor-address, and direction comparisons. Value and weight sorting retain their original tie ordering.
- Reports topology capture and best-in-class candidate evaluation separately.

## Native inventory enumeration

`bEnableInventoryEnumeration=1` builds the complete logical inventory-row sequence once per `RefreshItemList` call and serves the game's indexed requests from that sequence. It preserves base-container order, duplicate CNTO aggregation, change-only entries, split stacks, residual aggregate stacks, leveled-item handling, and the original ExtraDataList pointer order.

`bValidateInventoryEnumeration=1` keeps the correctness guard enabled. The first bulk sequence of each game session is compared against every row produced by the native indexed implementation using the base-object pointer, count, and ordered ExtraDataList pointers. A mismatch is logged and disables bulk enumeration for the rest of the session. The validation refresh intentionally retains the original O(N²) cost; later refreshes use the linear materializer.

The same settings also cover object-specific collection during partial rebuilds. Each collection uses a fresh session, returns newly owned descriptors for the first contiguous matching run, and releases skipped descriptors. Its requests and elapsed time appear under `enumeration`, with `bulk/object` identifying the optimized path.

## Native name sorting

`bEnableNativeNameSort=1` replaces the first-element-pivot quicksort used by name sorting with `std::sort`. The native algorithm degenerates on the nearly sorted list retained by partial updates. The replacement has an O(N log N) worst-case comparison bound and calls the original comparator, including its tie breakers and ascending/descending flag.

SE retains the name-sort wrapper's array clearing, repopulation, and `updatePending` transition. AE retains the engine wrapper and replaces only its sort call. Both paths operate before the vanilla or SkyUI frontend refresh. Value and weight sorting are unchanged because their comparators allow distinct entries to compare equal. Disabling this setting restores original name sorting. The log reports `bounded/name` when the replacement runs.

Verified call sites and remaining native work are documented in [Native refresh](docs/NativeRefresh.md).

## Incremental invalidation

Set `bEnableIncrementalInvalidation=1` in `Data/SKSE/Plugins/InventoryRefreshFix.ini` to enable the incremental UI path. Before rebuilding the native list, the plugin records the already materialized Scaleform form IDs, category flags, and entry objects without re-reading expiring native inventory descriptors. SkyUI-compatible lists recognize objects retained by the game's partial-update path and match newly materialized objects to prior processed entries using the stable form, display text, and filter flag. If the opening movie processed its entries before raw snapshots could be attached, a complete rebuild may also match entries with identical per-position topology and stable native refresh fields. Entries whose native primitive data is unchanged reuse the processed object; changed and unmatched entries remain raw for normal SkyUI processing. The standard SkyUI item-card processor retains its full-list callback semantics but naturally skips cached entries, while deterministic icon and property processors run only for changed entries. When item topology and every active filter and sort input remain unchanged, the plugin replaces stale filtered-enumeration references with the current entries and updates the visible renderers without rebuilding and sorting the complete filtered list. The original selection and highlight notifications are restored explicitly. The vanilla list retains its separate topology-checked visible-renderer path. Missing movie interfaces, unreliable cache state, changed filter or sort inputs, nonstandard processor chains, and unsupported menu layouts automatically use the original full invalidation.

This option targets redundant ActionScript item-data processing, filtering, and sorting while retaining the original full refresh as the correctness fallback. It is enabled by default.

Icon and property updates invoke their installed `processList` methods with a temporary list containing only changed entry references. Both `entryList` and `_entryList` expose that same array, preserving InventoryInjector's list-level icon hook without reprocessing every cached item. The item-card processor still receives the real complete list. Native full rebuilds are identified from `pendingUpdateObjects` before the engine refresh, independently of whether partial collection also used bulk enumeration. See [InventoryInjector compatibility](docs/InventoryInjector.md).

### Requirements

* Skyrim SE 1.5.97, Skyrim AE 1.6.1170, or GOG 1.6.1179
* A matching SKSE and Address Library installation
* [XMake](https://xmake.io) [3.0.0+]
* C++23 Compiler (MSVC, Clang-CL)

## Getting Started
```bat
git clone --recurse-submodules <your-mod-repository-url>
cd InventoryRefreshFix
```

### Build
Build one NG DLL with CommonLib's multi-runtime ABI:

```bat
xmake f --skyrim_se=y --skyrim_ae=y --skyrim_vr=y
xmake build
```

### One-click release package

```powershell
.\build-release.ps1
```

This creates one `*-NG.zip` in `release/`, using an isolated `build/NG/output` directory. The release script no longer takes a `-Target` argument.

### Build Output (Optional)
If you want to redirect the build output, set one of the following environment variables:

- Path to a Mod Manager mods folder: `XSE_TES5_MODS_PATH`

  or

- Path to a Skyrim install folder: `XSE_TES5_GAME_PATH`

### Project Generation (Optional)
If you use Visual Studio, run the following command:
```bat
xmake project -k vsxmake
```

> ***Note:*** *This will generate a `vsxmakeXXXX/` directory in the **project's root directory** using the latest version of Visual Studio installed on the system.*

**Alternatively**, if you do not use Visual Studio, you can generate a `compile_commands.json` file for use with a laguage server like clangd in any code editor that supports it, like vscode:
```bat
xmake project -k compile_commands
```

> ***Note:*** *You must have a language server extension installed to make use of this file. I recommend `clangd`. Do not have more than one installed at a time as they will conflict with each other. I also recommend installing the `xmake` extension if available to make building the project easier.*

### Upgrading Packages (Optional)
If you want to upgrade the project's dependencies, run the following commands:
```bat
xmake repo --update
xmake require --upgrade
```

## Project metadata

- Name: `InventoryRefreshFix`
- Version: `0.7.0`
- Author: `Jiaye`
