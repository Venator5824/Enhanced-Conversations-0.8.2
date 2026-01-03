#pragma once
/// Platform.h
#ifndef PLATFORM_H
#define PLATFORM_H

// ------------------------------------------------------------
// 1. CONFIGURATION: SELECT YOUR TARGET
// ------------------------------------------------------------

// Available Modes
#define PLATFORM_MODE_AUTO      0
#define PLATFORM_MODE_WINDOWS   1
#define PLATFORM_MODE_LINUX     2
#define PLATFORM_MODE_PS5       3  // Prospero
#define PLATFORM_MODE_XBOX      4  // GDK / UWP
#define PLATFORM_MODE_MACOS     5  // Just in case

// ============================================================
// [USER SETTING] CHANGE THIS VALUE TO FORCE PLATFORM
// ============================================================
#define CURRENT_PLATFORM_MODE   PLATFORM_MODE_AUTO
// ============================================================

#include "PlatformSystem.h"

// ------------------------------------------------------------
// 2. LOGIC: DETERMINE THE OS
// ------------------------------------------------------------

// --- CASE 1: FORCED WINDOWS ---
#if CURRENT_PLATFORM_MODE == PLATFORM_MODE_WINDOWS
#define PLATFORM_WINDOWS
#define OS_VERSION_WINDOWS

// --- CASE 2: FORCED LINUX ---
#elif CURRENT_PLATFORM_MODE == PLATFORM_MODE_LINUX
#define PLATFORM_LINUX
#define OS_VERSION_LINUX

// --- CASE 3: FORCED CONSOLE (PS5) ---
#elif CURRENT_PLATFORM_MODE == PLATFORM_MODE_PS5
#define PLATFORM_PS5
#define PLATFORM_CONSOLE

// --- CASE 4: FORCED CONSOLE (XBOX) ---
#elif CURRENT_PLATFORM_MODE == PLATFORM_MODE_XBOX
#define PLATFORM_XBOX
#define PLATFORM_WINDOWS // Xbox is essentially a locked down Windows
#define PLATFORM_CONSOLE

// --- CASE 0: AUTO DETECTION (Fallback) ---
#elif CURRENT_PLATFORM_MODE == PLATFORM_MODE_AUTO

#if defined(_WIN32) || defined(_WIN64)
#define PLATFORM_WINDOWS
#define OS_VERSION_WINDOWS
// Check for Xbox GDK specific macros here if needed later

#elif defined(__linux__)
#define PLATFORM_LINUX
#define OS_VERSION_LINUX

#elif defined(__ORBIS__) || defined(__PROSPERO__)
#define PLATFORM_PS5
#define PLATFORM_CONSOLE

#elif defined(__APPLE__)
#define PLATFORM_MACOS

#else
#error "Unknown Platform! Please set CURRENT_PLATFORM_MODE manually."
#endif

#endif


// ------------------------------------------------------------
// 3. PLATFORM SPECIFIC INCLUDES & MACROS
// ------------------------------------------------------------

// --- WINDOWS (PC) ---
#if defined(PLATFORM_WINDOWS) && !defined(PLATFORM_XBOX)
    // Windows 10/11 Logic
#define _CRT_SECURE_NO_WARNINGS
#define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <dxgi1_4.h>
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Version.lib")
// DLL Export logic
#define GAME_API extern "C" __declspec(dllexport)

// File Separator
#define PATH_SEPARATOR '\\'

// --- LINUX ---
#elif defined(PLATFORM_LINUX)
#include <unistd.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdlib.h>

// DLL Export logic (Shared Object)
#define GAME_API extern "C" __attribute__((visibility("default")))

// Mock Windows types to keep code compatible without rewriting everything
typedef void* HANDLE;
typedef unsigned long DWORD;
typedef int BOOL;
#define TRUE 1
#define FALSE 0

// File Separator
#define PATH_SEPARATOR '/'

// --- XBOX (Series S|X) ---
#elif defined(PLATFORM_XBOX)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
// Xbox specific GDK headers would go here
// #include <XGameRuntime.h>

#define GAME_API extern "C" __declspec(dllexport)
#define PATH_SEPARATOR '\\'

// --- PLAYSTATION 5 ---
#elif defined(PLATFORM_PS5)
    // PS5 SDK Headers (NDA Restricted, usually generic names like kernel.h)
    // #include <kernel.h>

#define GAME_API extern "C" 
#define PATH_SEPARATOR '/'

// Mock types
typedef void* HANDLE;
#endif

#endif // PLATFORM_H

// EOF
