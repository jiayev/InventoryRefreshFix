#pragma once

namespace Settings
{
	void Load();

	[[nodiscard]] bool IsInventoryEnumerationEnabled();
	[[nodiscard]] bool IsInventoryEnumerationValidationEnabled();
	[[nodiscard]] bool IsIncrementalInvalidationEnabled();
}
