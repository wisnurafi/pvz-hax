#include "logger.h"

namespace {
	std::ofstream      g_logStream;
	std::mutex         g_logMutex;
	logger::severity   g_filter = logger::severity::trace;
}

std::ofstream& logger::_stream()  { return g_logStream; }
std::mutex&    logger::_mutex()   { return g_logMutex; }
logger::severity logger::_filter() { return g_filter; }
void           logger::_setFilter(severity s) { g_filter = s; }

void logger::init()
{
	std::lock_guard<std::mutex> lock(g_logMutex);
	if (!g_logStream.is_open()) {
		g_logStream.open(LOG_FILE, std::ios::out | std::ios::app);
	}
	g_filter = severity::trace;
}

void logger::getLastError()
{
	DWORD errId = GetLastError();
	LPVOID errBuf = nullptr;
	FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		errId,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPSTR)&errBuf,
		0,
		NULL);
	BOOST_LOG_TRIVIAL(error) << "GetLastError(" << errId << "): " << (errBuf ? (LPCSTR)errBuf : "");
	if (errBuf) LocalFree(errBuf);
}
