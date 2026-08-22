#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

namespace RE
{
	class InventoryChanges;
	class InventoryEntryData;
}

namespace InventoryEnumeration
{
	using OriginalFunction = RE::InventoryEntryData* (*)(RE::InventoryChanges*, std::int32_t);

	class Session
	{
	public:
		Session();
		~Session();

		Session(const Session&) = delete;
		Session(Session&&) = delete;
		Session& operator=(const Session&) = delete;
		Session& operator=(Session&&) = delete;

		[[nodiscard]] RE::InventoryEntryData* Get(
			RE::InventoryChanges* a_changes,
			std::int32_t a_index,
			OriginalFunction a_original,
			bool a_enabled,
			bool a_validate);

		[[nodiscard]] std::string_view GetStatus() const noexcept;

	private:
		class Impl;
		std::unique_ptr<Impl> _impl;
	};
}
