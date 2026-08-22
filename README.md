# InventoryRefreshFix

Runtime profiling and optimization groundwork for Skyrim's expensive full inventory-list refreshes.

The profiler measures the `InventoryMenu` paths used when Skyrim opens the menu or rebuilds the complete Scaleform item list. Each log entry separates item-list rebuilding, indexed inventory materialization, bottom-bar updates, player 3D rebuilding, and other message-handling work. It also includes an experimental coalescing path for redundant complete updates.

`lib/commonlibsse` is pinned to [`c3c4449`](https://github.com/jiayev/CommonLibSSE/commit/c3c444980), which includes the reviewed inventory refresh interfaces used for the next implementation stage.

## Current scope

- Hooks `InventoryMenu::ProcessMessage` after SKSE's `kDataLoaded` event.
- Times menu opening and player-targeted `kInventoryUpdate` messages with a null `updateObj`, the two paths that perform a complete item-list rebuild.
- Instruments the exact `ProcessMessage` call sites for item-list rebuilding, bottom-bar updates, and player 3D rebuilding on Skyrim SE and AE.
- Instruments both `InventoryChanges::GetInventoryItemAt` call sites inside `InventoryMenu::RefreshItemList` and reports their aggregate duration and call count.
- Separates Scaleform array clearing, per-entry `PushBack`, and `InvalidateListData` from the remaining native materialization and sorting work.
- Retains profiling-only behavior by default.

## Experimental incremental invalidation

Set `bEnableIncrementalInvalidation=1` in `Data/SKSE/Plugins/InventoryRefreshFix.ini` to enable the incremental UI path. Before rebuilding the native list, the plugin records the sorted item identities and category flags. If they are unchanged at invalidation time, it refreshes only the visible item renderers and reproduces the original selection event. Item additions, removals, reordering, category changes, missing movie interfaces, and unsupported menu layouts automatically use the original `InvalidateListData` callback.

This option targets the expensive ActionScript-wide category/filter rescan while retaining the original full refresh as the correctness fallback. It remains disabled until inventory opening, equipping, favoriting, consuming, dropping, picking up, renaming, enchanting, and custom inventory-menu movies have been exercised.

## Experimental coalescing

Set `bEnableRefreshCoalescing=1` in `Data/SKSE/Plugins/InventoryRefreshFix.ini` to enable the experimental path. It consumes adjacent complete-update messages, schedules one UI task, reacquires the still-open `InventoryMenu`, and calls the game's `RefreshItemList` and `RefreshBottomBar` helpers once.

The default is `0` because the runtime data confirms redundant rebuilds, but not yet every possible update ordering. This setting should be tested with inventory opening, equipping and unequipping, consuming items, crafting, trading, picking up items, and scripted inventory changes.

### Requirements

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
- Version: `0.4.0`
- Author: `Jiaye`
