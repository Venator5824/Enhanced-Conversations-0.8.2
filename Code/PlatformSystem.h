
#pragma once
// platformsystem.h 
#ifndef PLATFORM_SYSTEM_H
#define PLATFORM_SYSTEM_H

#include "Platform.h"
#include <string>
#include <vector>
#include <cstdint>

namespace AOS {


    // =============================================================
    // 1. TIME & INPUT
    // =============================================================
    uint64_t GetTimeMs(); // Replaces GetTickCount64
    bool IsKeyPressed(int keyID); // Replaces GetAsyncKeyState

    // =============================================================
    // 2. FILESYSTEM & PATHS
    // =============================================================
    // Replaces GetModuleFileNameA
    std::string GetExecutablePath();

    // =============================================================
    // 3. INI CONFIGURATION (GetPrivateProfileString)
    // =============================================================
    // Replaces GetPrivateProfileStringA
    std::string GetIniValue(const std::string& section, const std::string& key, const std::string& defaultVal, const std::string& filePath);

    // Replaces GetPrivateProfileSectionA (Returns raw section data)
    std::string GetIniSection(const std::string& section, const std::string& filePath);

    // Replaces GetPrivateProfileSectionNamesA
    std::vector<std::string> GetIniSectionNames(const std::string& filePath);

    // =============================================================
    // 4. SYSTEM & ENVIRONMENT
    // =============================================================
    // Replaces SetEnvironmentVariableA
    bool SetEnvVar(const std::string& name, const std::string& value);

    // Replaces GetProcessMemoryInfo. Returns bytes used by this process.
    uint64_t GetProcessRAMUsage();

    // =============================================================
    // 5. WINDOWS & DISPLAY
    // =============================================================
    // Replaces FindWindowA. Returns an abstract handle (void*) or NULL.
    void* FindWindowByName(const std::string& className, const std::string& windowName);

    // Replaces GetWindowRect. We return a simple struct.
    struct Rect { long left; long top; long right; long bottom; };
    bool GetWindowPosition(void* windowHandle, Rect& outRect);

    // Replaces GetSystemMetrics. 
    // index 0 = SM_CXSCREEN, 1 = SM_CYSCREEN usually.
    int GetSystemMetric(int index);

    // =============================================================
    // 6. GRAPHICS & VRAM (DXGI Abstraction)
    // =============================================================
    struct VRAMInfo {
        uint64_t budget; // Available video memory
        uint64_t usage;  // Current usage
        bool valid;      // True if retrieval succeeded
    };

    VRAMInfo GetVRAMUsage();
    uint64_t GetFreeSystemRAM();
    uint64_t GetProcessRAMUsage();
    VRAMInfo GetVRAMSize();


    

}
//EOF
#endif