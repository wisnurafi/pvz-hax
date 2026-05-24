#include "ProcessUtil.h"

// Normalize an exe / module file name for tolerant comparison:
// - trim leading/trailing whitespace and non-printable bytes (incl. NBSP 0xA0)
// - lowercase ASCII
// This handles cases where the actual file name differs in casing
// (e.g. "plantsvszombies.exe" vs "PlantsVsZombies.exe") or has stray
// invisible characters appended.
static std::string NormalizeExeName(const char* name)
{
	if (name == nullptr) return {};
	std::string s(name);

	auto isJunk = [](unsigned char c) {
		// space, tab, CR, LF, NBSP (0xA0), and anything below 0x20
		return c <= 0x20 || c == 0xA0;
	};

	size_t b = 0;
	while (b < s.size() && isJunk((unsigned char)s[b])) ++b;
	size_t e = s.size();
	while (e > b && isJunk((unsigned char)s[e - 1])) --e;
	s = s.substr(b, e - b);

	for (auto& c : s) {
		unsigned char uc = (unsigned char)c;
		if (uc >= 'A' && uc <= 'Z') c = (char)(uc + ('a' - 'A'));
	}
	return s;
}

DWORD ProcessUtil::GetProcessIDByTitle(LPCSTR lpWindowName, LPCSTR lpClassName)
{
	HWND w = FindWindowA(lpClassName, lpWindowName);
	if (w == nullptr) return 0;
	else {
		DWORD processId = 0;
		GetWindowThreadProcessId(w, &processId);
		return processId;
	}
}

DWORD ProcessUtil::GetProcessIDByExeFile(const char* processName)
{
	HANDLE handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
	if (handle == INVALID_HANDLE_VALUE) return 0;

	const std::string target = NormalizeExeName(processName);

	PROCESSENTRY32 entry;
	entry.dwSize = sizeof(entry);
	DWORD foundPid = 0;
	while (Process32Next(handle, &entry)) {
		if (NormalizeExeName(entry.szExeFile) == target) {
			foundPid = entry.th32ProcessID;
			break;
		}
	}
	CloseHandle(handle);
	return foundPid;
}

std::pair<QWORD, QWORD> ProcessUtil::GetModule(DWORD processId, const char* moduleName)
{
	// If game is 32-bit program, so the cheater must be 32-bit.
	HANDLE mod = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, processId);
	if (mod == INVALID_HANDLE_VALUE) return { 0, 0 };

	const std::string target = NormalizeExeName(moduleName);

	MODULEENTRY32 mEntry;
	mEntry.dwSize = sizeof(mEntry);
	std::pair<QWORD, QWORD> result = { 0, 0 };
	while (Module32Next(mod, &mEntry)) {
		if (NormalizeExeName(mEntry.szModule) == target) {
			result = { (QWORD)mEntry.hModule, mEntry.modBaseSize };
			break;
		}
		//getLogger()->AddLog("Handle:%llx, ModuleName: %s(%x), exeFile:%s\n", (QWORD)mEntry.hModule, mEntry.szModule, mEntry.modBaseAddr, mEntry.szExePath);
	}
	CloseHandle(mod);
	return result;
}

std::string ProcessUtil::ReadString(HANDLE pHandle, QWORD address, size_t len)
{
	std::string s;
	for (size_t i = 0; i < len; i++)
	{
		char c;
		ReadProcessMemory(pHandle, (LPVOID)address, &c, sizeof(char), NULL);
		s.push_back(c);
		if (c == 0 && len == 65535) break;
		address = (QWORD)((BYTE*)address) + 1;
	}
	return s;
}

BOOL ProcessUtil::InjectDLL(HANDLE pHandle, const char* dllPath)
{
	if (std::filesystem::exists(dllPath) && pHandle != nullptr) {
		LPVOID pAddr = VirtualAllocEx(pHandle, NULL, strlen(dllPath) + 1, MEM_COMMIT, PAGE_READWRITE);
		if (pAddr == nullptr) return FALSE;
		WriteProcessMemory(pHandle, pAddr, dllPath, strlen(dllPath) + 1, NULL);
		HMODULE hModule = LoadLibrary("KERNEL32.DLL");
		if (hModule == nullptr) return FALSE;
		LPTHREAD_START_ROUTINE func = (LPTHREAD_START_ROUTINE)GetProcAddress(hModule, "LoadLibraryA");
		if (func == nullptr) return FALSE;
		return CreateRemoteThread(pHandle, NULL, 0, func, pAddr, 0, NULL) != nullptr;
	}
	return FALSE;
}

void ProcessUtil::UnloadDLL(HANDLE pHandle, const char* moduleName)
{
	DWORD pid = GetProcessId(pHandle);
	LPVOID paramAddr = (LPVOID)GetModule(pid, moduleName).first;
	if (paramAddr != nullptr) {
		HMODULE hModule = LoadLibrary("KERNEL32.DLL");
		LPTHREAD_START_ROUTINE func = (LPTHREAD_START_ROUTINE)GetProcAddress(hModule, "FreeLibrary");
		CreateRemoteThread(pHandle, NULL, 0, func, paramAddr, 0, NULL);
	}
}

LPVOID ProcessUtil::AllocAndWrite(HANDLE h, void* data, DWORD size)
{
	LPVOID dataAddr = VirtualAllocEx(h, NULL, size, MEM_COMMIT, PAGE_READWRITE);
	if (dataAddr == nullptr) return nullptr;
	if (!WriteProcessMemory(h, dataAddr, data, size, NULL)) return nullptr;
	return dataAddr;
}

void ProcessUtil::WaitToFree(HANDLE threadHandle, std::vector<LPVOID> addrList, bool async)
{
	auto func = [threadHandle, addrList] {
		WaitForSingleObject(threadHandle, INFINITE);
		// https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualfreeex
		for (auto item : addrList)
			VirtualFreeEx(threadHandle, item, 0, MEM_RELEASE);
		CloseHandle(threadHandle);
	};
	
	if (async) {
		std::thread t(func);
		t.detach();
	}
	else func();
}

bool ProcessUtil::RemoteCallDllFunc(const HANDLE h, const HMODULE dllModule, const char* funcName, std::vector<LPVOID> addrList, bool async)
{
	if (h == nullptr || dllModule == nullptr || addrList.empty()) return false;
	auto funcAddr = (LPTHREAD_START_ROUTINE)GetProcAddress(dllModule, funcName);
	if (!funcAddr) return false;
	HANDLE threadHandle = CreateRemoteThread(h, nullptr, 0, funcAddr, addrList[0], 0, NULL);
	if (!threadHandle) return false;
	WaitToFree(threadHandle, addrList, async);
	return true;
}
