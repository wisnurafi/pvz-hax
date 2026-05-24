#pragma once
#include "common.h"
#include <fstream>
#include <sstream>
#include <mutex>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <Windows.h>

// Lightweight drop-in replacement for boost::log::trivial used by the DLL.
// See PVZCheater/logger.h for the API contract; this is the same shim duplicated
// per project so each binary owns its log stream.

namespace logger {

	enum class severity {
		trace,
		debug,
		info,
		warning,
		error,
		fatal
	};

	const std::string LOG_FILE = "PVZ_DLL.log";

	void init();
	void getLastError();

	std::ofstream& _stream();
	std::mutex&    _mutex();
	severity       _filter();
	void           _setFilter(severity s);

	inline const char* _severityName(severity s) {
		switch (s) {
			case severity::trace:   return "trace";
			case severity::debug:   return "debug";
			case severity::info:    return "info";
			case severity::warning: return "warning";
			case severity::error:   return "error";
			case severity::fatal:   return "fatal";
		}
		return "?";
	}

	struct record {
		severity          sev;
		std::stringstream buf;
		bool              enabled;

		explicit record(severity s) : sev(s), enabled(s >= _filter()) {}

		~record() {
			if (!enabled) return;
			std::lock_guard<std::mutex> lock(_mutex());
			auto& os = _stream();
			if (!os.is_open()) return;

			using namespace std::chrono;
			auto now  = system_clock::now();
			auto t    = system_clock::to_time_t(now);
			auto ms   = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
			std::tm  tm{};
			localtime_s(&tm, &t);

			os << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
			   << "." << std::setfill('0') << std::setw(3) << ms.count()
			   << "] [" << _severityName(sev) << "] "
			   << buf.str() << std::endl;
		}

		template <typename T>
		record& operator<<(const T& v) {
			if (enabled) buf << v;
			return *this;
		}

		record& operator<<(std::ostream& (*manip)(std::ostream&)) {
			if (enabled) buf << manip;
			return *this;
		}
	};
}

#define BOOST_LOG_TRIVIAL(lvl) ::logger::record(::logger::severity::lvl)
