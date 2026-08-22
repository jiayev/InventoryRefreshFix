# InventoryRefreshFix

Coalesces expensive Skyrim inventory-list refreshes and records refresh timing.

Created from the local SKSE Mod template. `lib/commonlibsse` is a Git submodule pinned to a reviewed commit in [jiayev/CommonLibSSE](https://github.com/jiayev/CommonLibSSE).

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

## Template metadata

- Name: `InventoryRefreshFix`
- Version: `0.1.0`
- Author: `Jiaye`
