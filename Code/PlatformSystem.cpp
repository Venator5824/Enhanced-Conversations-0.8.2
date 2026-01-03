

#include "PlatformSystem.h"
#include "main.h"
#include <iostream>

// =============================================================
// PLATFORM: WINDOWS IMPLEMENTATION
// =============================================================
#ifdef PLATFORM_WINDOWS

#include <Windows.h>
#include <Psapi.h>      // For GetProcessMemoryInfo
#include <dxgi1_4.h>    // For VRAM
#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "dxgi.lib")

// Helper to convert std::string to LPCSTR (const char*)
// Not strictly needed as .c_str() works, but keeps intent clear
#endif

// =============================================================
// PLATFORM: LINUX / OTHERS (Stubs for now)
// =============================================================
#ifdef PLATFORM_LINUX
#include <time.h>
// Linux INI parsing usually requires a custom library or simple parser
#endif

namespace AOS {

    // ---------------------------------------------------------
    // 1. TIME & INPUT
    // ---------------------------------------------------------
    uint64_t GetTimeMs() {
#ifdef PLATFORM_WINDOWS
        return GetTickCount64();
#elif defined(PLATFORM_LINUX)
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#else
        return 0;
#endif
    }

    bool IsKeyPressed(int keyID) {
#ifdef PLATFORM_WINDOWS
        // 0x8000 check is standard Win32 for "Currently Down"
        return (GetAsyncKeyState(keyID) & 0x8000) != 0;
#else
        return false;
#endif
    }

    // ---------------------------------------------------------
    // 2. FILESYSTEM
    // ---------------------------------------------------------
    std::string GetExecutablePath() {
        char buffer[1024];
#ifdef PLATFORM_WINDOWS
        DWORD ret = GetModuleFileNameA(NULL, buffer, sizeof(buffer));
        if (ret > 0) return std::string(buffer);
#endif
        return "";
    }

    // ---------------------------------------------------------
    // 3. INI CONFIGURATION
    // ---------------------------------------------------------
    std::string GetIniValue(const std::string& section, const std::string& key, const std::string& defaultVal, const std::string& filePath) {
        char buffer[4096];
#ifdef PLATFORM_WINDOWS
        GetPrivateProfileStringA(section.c_str(), key.c_str(), defaultVal.c_str(), buffer, sizeof(buffer), filePath.c_str());
        return std::string(buffer);
#else
        return defaultVal; // TODO: Linux INI Parser
#endif
    }

    std::string GetIniSection(const std::string& section, const std::string& filePath) {
        // This is tricky. GetPrivateProfileSection returns null-terminated strings ending with double null.
        std::vector<char> buffer(32767); // Standard max size for INI section
#ifdef PLATFORM_WINDOWS
        DWORD len = GetPrivateProfileSectionA(section.c_str(), buffer.data(), (DWORD)buffer.size(), filePath.c_str());
        if (len > 0) return std::string(buffer.data(), len);
#endif
        return "";
    }

    std::vector<std::string> GetIniSectionNames(const std::string& filePath) {
        std::vector<std::string> sections;
        std::vector<char> buffer(32767);
#ifdef PLATFORM_WINDOWS
        DWORD len = GetPrivateProfileSectionNamesA(buffer.data(), (DWORD)buffer.size(), filePath.c_str());
        // Parse the double-null terminated string
        if (len > 0) {
            char* p = buffer.data();
            while (*p) {
                sections.push_back(std::string(p));
                p += strlen(p) + 1;
            }
        }
#endif
        return sections;
    }

    // ---------------------------------------------------------
    // 4. SYSTEM & ENVIRONMENT
    // ---------------------------------------------------------
    bool SetEnvVar(const std::string& name, const std::string& value) {
#ifdef PLATFORM_WINDOWS
        return SetEnvironmentVariableA(name.c_str(), value.c_str()) != 0;
#else
        return false;
#endif
    }

    uint64_t GetWProcessRAMUsage() {
#ifdef PLATFORM_WINDOWS
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            return pmc.WorkingSetSize;
        }
#endif
        return 0;
    }

    // ---------------------------------------------------------
    // 5. WINDOWS & DISPLAY
    // ---------------------------------------------------------
    void* FindWindowByName(const std::string& className, const std::string& windowName) {
#ifdef PLATFORM_WINDOWS
        HWND hwnd = FindWindowA(
            className.empty() ? NULL : className.c_str(),
            windowName.empty() ? NULL : windowName.c_str()
        );
        return (void*)hwnd;
#endif
        return nullptr;
    }

    bool GetWindowPosition(void* windowHandle, Rect& outRect) {
#ifdef PLATFORM_WINDOWS
        if (!windowHandle) return false;
        RECT r;
        if (GetWindowRect((HWND)windowHandle, &r)) {
            outRect.left = r.left;
            outRect.top = r.top;
            outRect.right = r.right;
            outRect.bottom = r.bottom;
            return true;
        }
#endif
        return false;
    }

    int GetSystemMetric(int index) {
#ifdef PLATFORM_WINDOWS
        return GetSystemMetrics(index);
#endif
        return 0;
    }


    // =============================================================
    // VRAM IMPLEMENTATION (DXGI)
    // =============================================================

    VRAMInfo GetVRAMUsage() {
        VRAMInfo result = { 0, 0, false };

#ifdef PLATFORM_WINDOWS
        IDXGIFactory4* pFactory = nullptr;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&pFactory))) {
            return result;
        }

        IDXGIAdapter* pAdapter = nullptr;
        // Wir nehmen Adapter 0 (meistens die Haupt-GPU)
        if (SUCCEEDED(pFactory->EnumAdapters(0, &pAdapter))) {
            IDXGIAdapter3* pAdapter3 = nullptr;
            if (SUCCEEDED(pAdapter->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&pAdapter3))) {
                DXGI_QUERY_VIDEO_MEMORY_INFO memInfo;
                // DXGI_MEMORY_SEGMENT_GROUP_LOCAL = VRAM (nicht Shared System RAM)
                if (SUCCEEDED(pAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memInfo))) {
                    result.budget = memInfo.Budget;
                    result.usage = memInfo.CurrentUsage;
                    result.valid = true;
                }
                pAdapter3->Release();
            }
            pAdapter->Release();
        }
        pFactory->Release();
#endif
        return result;
    }

    //----------------------------------------
    // 7. RAM and RAM USAGE
    // ---------------------------------------

    uint64_t GetProcessRAMUsage() {
#ifdef PLATFORM_WINDOWS
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            return pmc.WorkingSetSize; // Bytes used by GTA5.exe + Mod
        }
#endif
        return 0;
    }

    uint64_t GetFreeSystemRAM() {
#ifdef PLATFORM_WINDOWS
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo)) {
            return memInfo.ullAvailPhys; // Freier RAM in Bytes
        }
#endif
        return 0;
    }

}
//EOF