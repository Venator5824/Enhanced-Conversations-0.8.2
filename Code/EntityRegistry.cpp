// EntityRegistry.cpp
#include "AbstractCalls.h"
using namespace AbstractGame;
using namespace AbstractTypes;

#include "ConfigReader.h"
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include "EntityRegistry.h"
#include "main.h"

using namespace AbstractTypes;

// --- GLOBAL STORAGE ---
// We keep these static so they are private to this file
static std::unordered_map<PersistID, EntityData> g_registry;
static std::unordered_map<GameHandle, PersistID> g_handleToID;

// Thread Safety
static std::shared_mutex g_registryMutex;
static std::mutex g_idGenMutex;
static PersistID g_nextGenericID = 0x001001;

// --- CONFIGURATION ---
bool EntityRegistry::s_KeepGenericMemory = false;

// ---------------------------------------------------------------------
// LIFECYCLE
// ---------------------------------------------------------------------

PersistID EntityRegistry::RegisterNPC(GameHandle handle) {
    if (handle == 0) return 0;

    // 1. Fast Read Check
    {
        std::shared_lock<std::shared_mutex> lock(g_registryMutex);
        if (g_handleToID.count(handle)) return g_handleToID[handle];
    }

    // 2. Write Lock
    std::unique_lock<std::shared_mutex> lock(g_registryMutex);
    if (g_handleToID.count(handle)) return g_handleToID[handle];

    // 3. Load Identity Persona
    NpcPersona persona = ConfigReader::GetPersona((AHandle)handle);

    PersistID newID;
    bool isPersistent = false;

    if (!persona.inGameName.empty()) {
        size_t nameHash = std::hash<std::string>{}(persona.inGameName);
        newID = (PersistID)nameHash;
        isPersistent = true;
    }
    else {
        std::lock_guard<std::mutex> idLock(g_idGenMutex);
        newID = g_nextGenericID++;
        isPersistent = s_KeepGenericMemory;
    }

    // --- FILL DATA ---
    EntityData data;
    data.id = newID;
    data.handle = handle;
    data.isRegistered = true;
    data.isPersistent = isPersistent;

    data.defaultName = persona.inGameName.empty() ? persona.modelName : persona.inGameName;
    data.modelHash = persona.modelHash;

    // Sync matching traits from Persona to Registry
    data.n_gender = persona.n_gender;
    data.n_age = persona.n_age;
    data.n_voicetype = persona.n_voicetype;

    // Apply specific audio model if defined in Persona
    if (!persona.p_audio_model.empty()) {
        data.voice.model = persona.p_audio_model;
        data.voice.id = (persona.p_audio_ID >= 0 || persona.p_audio_ID == -2000) ? persona.p_audio_ID : 0;
        data.e_audiomodel = data.voice.model;
        data.e_audioID = data.voice.id;
    }

    // Save to global storage
    g_registry[newID] = data;
    g_handleToID[handle] = newID;

    return newID;
}

void EntityRegistry::OnEntityRemoved(GameHandle handle) {
    std::unique_lock<std::shared_mutex> lock(g_registryMutex);

    if (g_handleToID.count(handle)) {
        PersistID id = g_handleToID[handle];

        // Break physical link
        g_handleToID.erase(handle);

        // Handle Persistence
        if (g_registry.count(id)) {
            if (g_registry[id].isPersistent) {
                // Hero: Mark as "Despawned" (handle = 0) but keep data
                g_registry[id].handle = 0;
            }
            else {
                // Extra: Forget forever
                g_registry.erase(id);
            }
        }
    }
}

// ---------------------------------------------------------------------
// GETTERS
// ---------------------------------------------------------------------

EntityData EntityRegistry::GetData(PersistID id) {
    std::shared_lock<std::shared_mutex> lock(g_registryMutex);
    if (g_registry.count(id)) return g_registry[id];
    return EntityData(); // Return empty struct if not found
}

PersistID EntityRegistry::GetIDFromHandle(GameHandle handle) {
    std::shared_lock<std::shared_mutex> lock(g_registryMutex);
    if (g_handleToID.count(handle)) return g_handleToID[handle];
    return 0;
}

bool EntityRegistry::HasEntity(PersistID id) {
    std::shared_lock<std::shared_mutex> lock(g_registryMutex);
    return g_registry.count(id) > 0;
}

// ---------------------------------------------------------------------
// SETTERS
// ---------------------------------------------------------------------

void EntityRegistry::SetIdentity(PersistID id, const std::string& name, const std::string& gender) {
    std::unique_lock<std::shared_mutex> lock(g_registryMutex);
    if (g_registry.count(id)) {
        g_registry[id].overrideName = name;
        if (!gender.empty()) g_registry[id].overrideGender = gender;
    }
}

void EntityRegistry::SetGoal(PersistID id, const std::string& goal) {
    std::unique_lock<std::shared_mutex> lock(g_registryMutex);
    if (g_registry.count(id)) {
        g_registry[id].dynamicGoal = goal;
    }
}

void EntityRegistry::SetCustomKnowledge(PersistID id, const std::string& knowledge) {
    std::unique_lock<std::shared_mutex> lock(g_registryMutex);
    if (g_registry.count(id)) {
        g_registry[id].customKnowledge = knowledge;
    }
}

void EntityRegistry::AppendMemory(PersistID id, const std::string& fact) {
    std::unique_lock<std::shared_mutex> lock(g_registryMutex);
    if (g_registry.count(id)) {
        if (!g_registry[id].customKnowledge.empty()) {
            g_registry[id].customKnowledge += "\n";
        }
        g_registry[id].customKnowledge += "[MEMORY]: " + fact;
    }
}

// get name

std::string EntityRegistry::GetEntityName(PersistID id) {
    std::shared_lock<std::shared_mutex> lock(g_registryMutex);

    if (g_registry.count(id)) {
        const auto& data = g_registry[id];

        
        if (!data.overrideName.empty()) {
            return data.overrideName;
        }

        
        if (!data.defaultName.empty()) {
            return data.defaultName;
        }
    }
    return ""; 
}


void EntityRegistry::AssignEntityName(PersistID id, const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(g_registryMutex);

    if (g_registry.count(id)) {
        g_registry[id].overrideName = name;
        // Optional: Loggen, dass die Identität jetzt fixiert ist
    }
}


bool EntityRegistry::HasAssignedName(PersistID id) {
    std::shared_lock<std::shared_mutex> lock(g_registryMutex);
    if (g_registry.count(id)) {
        return !g_registry[id].overrideName.empty();
    }
    return false;
}

std::string EntityRegistry::GetAudioModel(PersistID id) {
    // Shared Lock = Mehrere Threads dürfen gleichzeitig lesen (schnell)
    std::shared_lock<std::shared_mutex> lock(g_registryMutex);

    if (g_registry.count(id)) {
        return g_registry[id].e_audiomodel;
    }
    return ""; // Leerer String, falls ID nicht existiert
}

int EntityRegistry::GetAudioID(PersistID id) {
    std::shared_lock<std::shared_mutex> lock(g_registryMutex);

    if (g_registry.count(id)) {
        return g_registry[id].e_audioID;
    }
    return -1000; // Standard-Wert für "Keine ID"
}

void EntityRegistry::SetAudioModel(PersistID id, const std::string& model) {
    // Unique Lock = Nur Einer darf schreiben (sicher)
    std::unique_lock<std::shared_mutex> lock(g_registryMutex);

    if (g_registry.count(id)) {
        g_registry[id].e_audiomodel = model;
    }
}

void EntityRegistry::SetAudioID(PersistID id, int audioID) {
    std::unique_lock<std::shared_mutex> lock(g_registryMutex);

    if (g_registry.count(id)) {
        g_registry[id].e_audioID = audioID;
    }
}


bool EntityRegistry::IsVoiceActive(const std::string& modelName, int id) {
    // Shared Lock für Thread-Safety (wir lesen nur)
    std::shared_lock<std::shared_mutex> lock(g_registryMutex);

    for (const auto& pair : g_registry) {
        // Wir prüfen: Lebt der NPC noch? UND Hat er exakt diese Stimme?
        if (AbstractGame::IsEntityValid(pair.second.handle)) {
            if (pair.second.voice.id == id && pair.second.voice.model == modelName) {
                return true; // Ja, ist besetzt!
            }
        }
    }
    return false;
}


void EntityRegistry::SetVoiceSettings(PersistID id, const VoiceSettings& settings) {
    std::unique_lock<std::shared_mutex> lock(g_registryMutex);
    auto it = g_registry.find(id);
    if (it != g_registry.end()) {
        it->second.voice = settings;
        it->second.e_audiomodel = settings.model;
        it->second.e_audioID = settings.id;
    }
}

VoiceSettings EntityRegistry::GetVoiceSettings(PersistID id) {
    std::shared_lock<std::shared_mutex> lock(g_registryMutex);

    
    if (g_registry.count(id)) {
       
        return g_registry[id].voice;
    }

    
    return { "NONE", -2000, 1.0f };
}

//EOF