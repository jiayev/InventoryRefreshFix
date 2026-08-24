#include "Logging.h"

#include "pch.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

namespace Logging
{
	bool Initialize()
	{
#ifdef SKYRIM_SUPPORT_AE
		return true;
#else
		auto path = SKSE::log::log_directory();
		if (!path) {
			return false;
		}

		*path /= "InventoryRefreshFix.log";

		try {
			std::vector<spdlog::sink_ptr> sinks{
				std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true),
				std::make_shared<spdlog::sinks::msvc_sink_mt>()
			};

			auto logger = std::make_shared<spdlog::logger>("global", sinks.begin(), sinks.end());
#ifndef NDEBUG
			logger->set_level(spdlog::level::debug);
			logger->flush_on(spdlog::level::debug);
#else
			logger->set_level(spdlog::level::info);
			logger->flush_on(spdlog::level::info);
#endif
			spdlog::set_default_logger(std::move(logger));
			spdlog::set_pattern("[%T.%e] [%=5t] [%L] %v");
		} catch (const spdlog::spdlog_ex&) {
			return false;
		}

		return true;
#endif
	}
}
