#pragma once

#if !defined(ENABLE_SKYRIM_SE) || !defined(ENABLE_SKYRIM_AE)
#	error InventoryRefreshFix requires both ENABLE_SKYRIM_SE and ENABLE_SKYRIM_AE.
#endif

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
