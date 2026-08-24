# InventoryRefreshFix

Runtime profiling and safe optimizations for Skyrim's expensive full inventory-list refreshes.

The profiler measures the `InventoryMenu` paths used when Skyrim opens the menu or rebuilds the complete Scaleform item list. Each log entry separates item-list rebuilding, indexed inventory materialization, bottom-bar updates, player 3D rebuilding, and other message-handling work.

`lib/commonlibsse` is pinned to [`1066ead`](https://github.com/jiayev/CommonLibSSE/commit/1066ead1b), which contains the inventory-specific refresh and materialization APIs based on CommonLibSSE `dev`.

## Current scope

- Hooks `InventoryMenu::ProcessMessage` after SKSE's `kDataLoaded` event.
- Times menu opening and player-targeted `kInventoryUpdate` messages with a null `updateObj`, the two paths that perform a complete item-list rebuild.
- Instruments the exact `ProcessMessage` call sites for item-list rebuilding, bottom-bar updates, and player 3D rebuilding on Skyrim SE and AE.
- Instruments both `InventoryChanges::GetInventoryItemAt` call sites inside `InventoryMenu::RefreshItemList` and reports their aggregate duration and call count.
- Replaces the indexed near-quadratic inventory materializer with one refresh-scoped linear pass. The first sequence in a game session is compared row-for-row with the original implementation before subsequent refreshes rely on it.
- Separates Scaleform array clearing, per-entry `PushBack`, and `InvalidateListData` from the remaining native materialization and sorting work.

## Native inventory enumeration

`bEnableInventoryEnumeration=1` builds the complete logical inventory-row sequence once per `RefreshItemList` call and serves the game's indexed requests from that sequence. It preserves base-container order, duplicate CNTO aggregation, change-only entries, split stacks, residual aggregate stacks, leveled-item handling, and the original ExtraDataList pointer order.

`bValidateInventoryEnumeration=1` keeps the correctness guard enabled. The first bulk sequence of each game session is compared against every row produced by the native indexed implementation using the base-object pointer, count, and ordered ExtraDataList pointers. A mismatch is logged and disables bulk enumeration for the rest of the session. The validation refresh intentionally retains the original O(N²) cost; later refreshes use the linear materializer.

## Incremental invalidation

Set `bEnableIncrementalInvalidation=1` in `Data/SKSE/Plugins/InventoryRefreshFix.ini` to enable the incremental UI path. Before rebuilding the native list, the plugin records the already materialized Scaleform form IDs, category flags, and entry objects without re-reading expiring native inventory descriptors. SkyUI-compatible lists recognize objects retained by the game's partial-update path and match newly materialized objects to prior processed entries using the stable form, display text, and filter flag. If the opening movie processed its entries before raw snapshots could be attached, a complete rebuild may also match entries with identical per-position topology and stable native refresh fields. Entries whose native primitive data is unchanged reuse the processed object; changed and unmatched entries remain raw for normal SkyUI processing. The standard SkyUI item-card processor retains its full-list callback semantics but naturally skips cached entries, while deterministic icon and property processors run only for changed entries. When item topology and every active filter and sort input remain unchanged, the plugin replaces stale filtered-enumeration references with the current entries and updates the visible renderers without rebuilding and sorting the complete filtered list. The original selection and highlight notifications are restored explicitly. The vanilla list retains its separate topology-checked visible-renderer path. Missing movie interfaces, unreliable cache state, changed filter or sort inputs, nonstandard processor chains, and unsupported menu layouts automatically use the original full invalidation.

This option targets redundant ActionScript item-data processing, filtering, and sorting while retaining the original full refresh as the correctness fallback. It is enabled by default.

### Requirements

* Skyrim SE 1.5.97 or Skyrim AE 1.6.1170
* A matching SKSE and Address Library installation
* [XMake](https://xmake.io) [3.0.0+]
* C++23 Compiler (MSVC, Clang-CL)

## Getting Started
```bat
git clone --recurse-submodules <your-mod-repository-url>
cd InventoryRefreshFix
```

### Build
To build the Skyrim SE target, run:
```bat
xmake f --skyrim_ae=n
xmake build
```

For Skyrim AE 1.6.1170:

```bat
xmake f --skyrim_ae=y
xmake build
```

### One-click release package

```powershell
.\build-release.ps1
```

This creates separately configured SE and AE packages in `release/`.

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
- Version: `0.5.1`
- Author: `Jiaye`
