// SPDX-License-Identifier: MIT

#include "log.h"

#include <windows.h>
#include <shlobj.h>
#include <mutex>
#include <cstdio>
#include <ctime>

namespace cdmp {

namespace {
std::mutex g_logMutex;
FILE* g_logFile = nullptr;
}

std::wstring GetAppDataDir() {
    PWSTR appData = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr,
                                       &appData))) {
        dir.assign(appData);
        CoTaskMemFree(appData);
    } else {
        dir = L".";
    }
    dir += L"\\CDMultiplayer";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

void LogInit() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile) return;
    const std::wstring path = GetAppDataDir() + L"\\client.log";
    g_logFile = _wfopen(path.c_str(), L"a, ccs=UTF-8");
}

void LogShutdown() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile) {
        fclose(g_logFile);
        g_logFile = nullptr;
    }
}

void LogLine(const char* level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    char ts[32];
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
    localtime_s(&tmv, &now);
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);

    char line[1024];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[%s] [%s] %s\n", ts, level,
                msg.c_str());

    if (g_logFile) {
        fputs(line, g_logFile);
        fflush(g_logFile);
    }
    OutputDebugStringA(line);
}

} // namespace cdmp
