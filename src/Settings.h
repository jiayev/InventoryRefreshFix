#pragma once

namespace Settings
{
	void Load();

	[[nodiscard]] bool IsRefreshCoalescingEnabled();
	[[nodiscard]] bool IsIncrementalInvalidationEnabled();
}
