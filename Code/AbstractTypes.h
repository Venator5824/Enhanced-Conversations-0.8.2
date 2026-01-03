//AbstractTypes.h

#pragma once

// 1. Standard C++ Types only (No Windows.h, No GTA natives)
#include <cstdint> 
#include <string>

namespace AbstractTypes {
    // 1. HANDLES
    // We use uintptr_t so it works for 64-bit pointers without needing Windows.h
    typedef std::uintptr_t GameHandle;
    const GameHandle INVALID_HANDLE = 0;

    // 2. MATH
    // Keep as 'Vec3'. Do NOT rename to 'Vector3' to avoid conflict with GTA types.
    struct Vec3 {
        float x, y, z;
        // Default constructor
        Vec3(float _x = 0.0f, float _y = 0.0f, float _z = 0.0f)
            : x(_x), y(_y), z(_z) {
        }
    };

    // 3. IDENTIFICATION
    typedef std::uint32_t ModelID;
    typedef std::uint64_t PersistID; // For EntityRegistry
    typedef std::uint64_t ChatID;    // For ConversationSystem

    // 4. SYSTEM
    typedef std::uint32_t TimeMillis;
    typedef void* WindowHandle;      // Generic pointer for HWND
}

// ------------------------------------------------------------
// Global Aliases (Use these in your code)
// ------------------------------------------------------------
using AHandle = AbstractTypes::GameHandle;
using AVec3 = AbstractTypes::Vec3;
using AModel = AbstractTypes::ModelID;
using PersistID = AbstractTypes::PersistID;
using ChatID = AbstractTypes::ChatID;


//EOF