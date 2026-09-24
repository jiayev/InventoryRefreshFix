# NG runtime profiles

InventoryRefreshFix uses one DLL with runtime-selected hook profiles. The CommonLib submodule tracks `alandtse/CommonLibSSE-NG`'s `ng` branch, pinned to `736dc64094e59232abfbcdf796cd0a063e136ec6` (9.0.1).

| Runtime | Profile | Verification |
| --- | --- | --- |
| SE 1.5.97 | SE signatures and call-site layout | 28 CALL instructions and targets checked against the local executable. |
| AE 1.6.1170 | AE signatures and call-site layout | 25 CALL instructions and targets checked against the local unpacked executable. |
| GOG 1.6.1179 | AE layout, GOG Address Library addresses | All 15 required function IDs resolve in the GOG database. Call-site layout is assumed shared with 1.6.1170 and checked during installation; no GOG executable or in-game verification was available. |
| 1.7.99 / 1.7.104 | Not enabled | NG recognizes these runtimes, but this plugin's call sites and signatures still need verification. |
| Other runtimes / VR | Not enabled | No supported hook profile. |

## Addressing and signatures

`Runtime.cpp` contains function IDs and function-relative call offsets, not executable RVAs. The IDs were resolved back to the old addresses using the 1.5.97 and 1.6.1170 Address Library databases. The same AE IDs resolve to different addresses on GOG: for example, `InventoryMenu::ProcessMessage` is ID 51848, at RVA `0x92C5D0` on 1.6.1170 and `0x92E610` on 1.6.1179.

SE's Scaleform clear and push offsets are relative to its three sort wrappers; AE's are relative to `InventoryMenu::RefreshItemList`. The two invalidation hooks have different signatures: SE receives an `ItemList*`, while AE receives a movie view, method name, and response arguments. Native sorting likewise uses a two-argument SE wrapper versus a four-argument AE quicksort. Each signature has its own thunk and original-function storage. Ghidra disassembly of both native name-sort functions confirms this distinction.

The original installation checks remain in place: expected CALL targets, collector prologue, collector enumeration calls, and name-comparator calls. Failed checks disable the affected hook and log the failure. These checks detect layout mismatches and prior redirects; they do not constitute a full behavioral proof for an untested runtime.

## Build and initialization

The release script builds once in `build/NG/output` and emits `InventoryRefreshFix-<version>-NG.zip`. It enables NG's default SE/AE/VR ABI, compatible with NG's optional all-runtime prebuilt library. This is a dependency ABI choice, not a claim of VR support. The plugin requires SE and AE to be enabled and checks its runtime whitelist before registering its data-loaded listener.

Menu members use NG's `GetRuntimeData()`. Trampoline allocation uses `SKSE::InitInfo`; logging remains explicitly initialized for both SE and AE, including error handling if the log directory cannot be opened. Bulk enumeration and the vanilla/SkyUI refresh paths are unchanged by the runtime migration.

This migration was reviewed statically, including executable call-site checks and PowerShell parsing. It was not compiled or tested in-game.

## Adding a runtime

Confirm the matching SKSE and Address Library support first. Check the refresh and sorting call sites, original function signatures, comparator behavior, and relevant object layouts against that executable before adding a profile. A new NG version constant or an existing Address Library ID alone does not establish compatible function-relative offsets.

References: [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG), [SKSE](https://skse.silverlock.org/).
