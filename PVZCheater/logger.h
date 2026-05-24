#pragma once
#include "common.h"
#include <fstream>
#include <sstream>
#include <mutex>
#include <chrono>
#include <ctime>
#include <iomanip>

// Lightweight drop-in replacement for boost::log::trivial.
// Keeps the BOOST_LOG_TRIVIAL(level) << "msg" call sites working without pulling in Boost.
//
// Usage:
//   BOOST_LOG_TRIVIAL(trace)   << "A trace severity message";
//   BOOST_LOG_TRIVIAL(debug)   << "A debug severity message";
//   BOOST_LOG_TRIVIAL(info)    << "An informational severity message";
//   BOOST_LOG_TRIVIAL(warning) << "A warning severity message";
//   BOOST_LOG_TRIVIAL(error)   << "An error severity message";
//   BOOST_LOG_TRIVIAL(fatal)   << "A fatal severity message";

namespace logger {

	enum class severity {
		trace,
		debug,
		info,
		warning,
		error,
		fatal
	};

	const std::string LOG_FILE = "pvz_logger.log";

	void init();
	void getLastError();

	// Internal: returns the singleton ofstream + mutex used by the streamer.
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

	// One-shot record: buffers the message in a stringstream and flushes
	// to the log file in the destructor under a mutex.
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

		// Support for stream manipulators like std::endl.
		record& operator<<(std::ostream& (*manip)(std::ostream&)) {
			if (enabled) buf << manip;
			return *this;
		}
	};
}

// trace/debug/info/warning/error/fatal are bare identifiers passed by the macro,
// so we resolve them through logger::severity::<id>.
#define BOOST_LOG_TRIVIAL(lvl) ::logger::record(::logger::severity::lvl)
