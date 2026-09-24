# NG runtime profiles

InventoryRefreshFix uses one DLL with runtime-selected hook profiles. The CommonLib submodule tracks `alandtse/CommonLibSSE-NG`'s `ng` branch, pinned to `736dc64094e59232abfbcdf796cd0a063e136ec6` (9.0.1).

| Runtime | Profile | Verification |
| --- | --- | --- |
| SE 1.5.97 | SE signatures and call-site layout | 28 CALL instructions and targets checked against the local executable. |
| AE 1.6.1170 | AE signatures and call-site layout | 25 CALL instructions and targets checked against the local unpacked executable. |
| GOG 1.6.1179 | AE layout, GOG Address Library addresses | All 15 required function IDs resolve in the GOG database. Call-site layout is assumed shared with 1.6.1170 and checked during installation; no GOG executable or in-game verification was available. |
| Other 1.6.x | Shared AE layout, version-specific Address Library addresses | Enabled without a patch-version whitelist; no individual executable verification. |
| AE 1.7.x | Shared AE layout, version-specific Address Library addresses | 25 CALL instructions and targets checked against 1.7.104, plus the collector prologue, signatures, layouts, and ProcessMessage vtable slot. Other 1.7.x versions are enabled by compatibility assumption, not individual executable verification. |
| Other runtimes / VR | Not enabled | No supported hook profile. |

## Addressing and signatures

`Runtime.cpp` contains function IDs and function-relative call offsets, not executable RVAs. The IDs were resolved back to the old addresses using the 1.5.97 and 1.6.1170 Address Library databases. The same AE IDs resolve to different addresses on GOG: for example, `InventoryMenu::ProcessMessage` is ID 51848, at RVA `0x92C5D0` on 1.6.1170 and `0x92E610` on 1.6.1179.

SE's Scaleform clear and push offsets are relative to its three sort wrappers; AE's are relative to `InventoryMenu::RefreshItemList`. The two invalidation hooks have different signatures: SE receives an `ItemList*`, while AE receives a movie view, method name, and response arguments. Native sorting likewise uses a two-argument SE wrapper versus a four-argument AE quicksort. Each signature has its own thunk and original-function storage. Ghidra disassembly of both native name-sort functions confirms this distinction.

The original installation checks remain in place: expected CALL targets, collector prologue, collector enumeration calls, and name-comparator calls. Failed checks disable the affected hook and log the failure. These checks detect layout mismatches and prior redirects; they do not constitute a full behavioral proof for an untested runtime. ProcessMessage still chains the existing vtable entry rather than requiring an unmodified engine pointer.

## 1.7 verification

All 1.6.x and 1.7.x runtimes use the existing AE offsets. Ghidra MCP analysis of `/skyrim/ae1104/SkyrimSE.1.7.104.exe.unpacked.exe` and an independent PE-byte check confirmed every CALL below as an `E8 rel32` targeting the expected Address Library ID. Addresses are RVAs, relative to image base `0x140000000`.

Unpacked executable SHA-256: `e1ad5f94cdb6c30af4ede23c9438883b64029180201892507870a012d651f57d`.

| Caller ID | Call offsets | Target ID | Target RVA (1.7.104) |
| --- | --- | --- | --- |
| 51848 (ProcessMessage) | `0x21B`, `0xB2B` | 51866 (RefreshItemList) | `0x9452B0` |
| 51848 | `0x223`, `0xB33` | 51865 (RefreshBottomBar) | `0x9451E0` |
| 51848 | `0xB5D` | 39395 (UpdatePlayer3D) | `0x6F6720` |
| 51866 | `0x190`, `0x218`, `0x2F6` | 82280 (RemoveElements) | `0xCF6F40` |
| 51866 | `0x1EE`, `0x2CC`, `0x3A0` | 82273 (PushBack) | `0xCF6AB0` |
| 51866 | `0x452` | 82640 (invalidation dispatcher) | `0x117A9D0` |
| 51866 | `0xEC`, `0x779` | 51011 (AddItem) | `0x904DF0` |
| 51866 | `0x1AF` | 50968 (name sort) | `0x903340` |
| 51866 | `0x286` | 50966 (weight sort) | `0x9030C0` |
| 51866 | `0x365` | 50967 (value sort) | `0x903200` |
| 50968 | `0x70`, `0xFF` | 51809 (name comparator) | `0x9400D0` |
| 51866 | `0x732` | 16107 (object collector) | `0x2394C0` |
| 16107 | `0x38`, `0xBF` | 16106 (GetInventoryItemAt) | `0x238940` |
| 51866 | `0x3E8` | 51831 (best-in-class candidate) | `0x941DA0` |
| 51866 | `0xC1`, `0x11E` | 16106 (GetInventoryItemAt) | `0x238940` |

The partial-refresh helper is ID 51868 at RVA `0x9458F0`, exactly `0x640` after RefreshItemList. The `0x732` collector and `0x779` AddItem sites are inside this helper, while the offsets remain relative to ID 51866 as in the existing AE profile. ProcessMessage is at RVA `0x942B80`; InventoryMenu's vtable (ID 215494, RVA `0x1978208`) still points to it at slot 4. The collector still begins with `48 85 D2`.

Ghidra decompilation and instruction comparison retain the three-argument AddItem and collector interfaces, four-argument AE sorting interface, and movie/name/response-arguments invalidation interface. The name comparator retains display-name ordering, FormID and descriptor-address tie breakers, and the direction flag. Indexed enumeration and its non-stackable normalization helper retain their instruction layout apart from relocated references; the bulk materializer's existing first-sequence validation remains enabled by default.

Relevant member accesses are unchanged: InventoryMenu's runtime data starts at `0x30`, itemList is at `0x48`, and pendingUpdateObjects is at `0x60` (size at `0x70`). ItemList's entryList, items, and updatePending remain at `0x20`, `0x38`, and `0x50`; Item entries remain `0x40` bytes with their GFxValue at `0x18`.

The pinned NG dependency already loads format-5 Address Libraries and selects the AE ABI on 1.7. No CommonLib changes or separate DLL are needed. This enables the whole 1.6.x and 1.7.x families as requested, not a guarantee of identical layouts across every patch. The 1.5.97 and 1.6.1170 CALL checks were also rerun (28 and 25 passing respectively). No compilation or in-game testing was performed for this update.

## Build and initialization

The release script builds once in `build/NG/output` and emits `InventoryRefreshFix-<version>-NG.zip`. It enables NG's default SE/AE/VR ABI, compatible with NG's optional all-runtime prebuilt library. This is a dependency ABI choice, not a claim of VR support. The plugin requires SE and AE to be enabled and checks its runtime whitelist before registering its data-loaded listener.

Menu members use NG's `GetRuntimeData()`. Trampoline allocation uses `SKSE::InitInfo`; logging remains explicitly initialized for both SE and AE, including error handling if the log directory cannot be opened. Bulk enumeration and the vanilla/SkyUI refresh paths are unchanged by the runtime migration.

This migration was reviewed statically, including executable call-site checks and PowerShell parsing. It was not compiled or tested in-game.

## Adding a runtime

Confirm the matching SKSE and Address Library support first. Check the refresh and sorting call sites, original function signatures, comparator behavior, and relevant object layouts when adding or revising a profile. The 1.6.x and 1.7.x families intentionally share a profile without patch-version gating; if an executable changes these interfaces, revise the affected offsets or signatures. A new NG version constant or an existing Address Library ID alone does not establish compatible function-relative offsets.

References: [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG), [SKSE](https://skse.silverlock.org/).
