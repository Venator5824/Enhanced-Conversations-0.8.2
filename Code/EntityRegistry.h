#pragma once
#include "main.h"
#include "AudioManager.h"
#include "AbstractTypes.h"
#include <string>
#include <vector>
#include <unordered_map>




struct EntityData {
    AbstractTypes::PersistID id = 0;
    AbstractTypes::GameHandle handle = 0;
    bool isRegistered = false;
    bool isPersistent = false;

    // --- BASE IDENTITY ---
    std::string defaultName;
    std::string defaultGender;
    uint32_t modelHash = 0;
    std::string n_gender;   
    std::string n_age;      
    std::string n_voicetype; 
    // --- SANDBOX OVERRIDES ---
    std::string overrideName;
    std::string overrideGender;
    std::string overrideGroup;

    // --- DYNAMIC CONTENT ---
    std::string dynamicGoal;
    std::string customKnowledge;
    std::string e_audiomodel; 
    int e_audioID = -1000;
    VoiceSettings voice;
};

class EntityRegistry {
public:

   
    // Global Setting: Should we force random NPCs to remember things? (Default: false)
    static bool s_KeepGenericMemory;

    // --- LIFECYCLE ---
    static AbstractTypes::PersistID RegisterNPC(AbstractTypes::GameHandle handle);
    static void OnEntityRemoved(AbstractTypes::GameHandle handle);
    static bool IsVoiceActive(const std::string& modelName, int id);
    static bool HasAssignedName(PersistID id);
    static VoiceSettings GetVoiceSettings(PersistID id);
    // --- GETTERS ---
    static EntityData GetData(AbstractTypes::PersistID id);
    static AbstractTypes::PersistID GetIDFromHandle(AbstractTypes::GameHandle handle);
    static bool HasEntity(AbstractTypes::PersistID id);
    static std::string GetEntityName(PersistID id);

    // --- SETTERS ---
    static void SetIdentity(AbstractTypes::PersistID id, const std::string& name, const std::string& gender = "");
    static void SetGoal(AbstractTypes::PersistID id, const std::string& goal);
    static void SetCustomKnowledge(AbstractTypes::PersistID id, const std::string& knowledge);
    static void AppendMemory(AbstractTypes::PersistID id, const std::string& fact);
    static void AssignEntityName(PersistID id, const std::string& name);

    //Audio files
    static std::string GetAudioModel(AbstractTypes::PersistID id);
    static int GetAudioID(AbstractTypes::PersistID id);
    static void SetVoiceSettings(PersistID id, const VoiceSettings& settings);
    static void SetAudioModel(AbstractTypes::PersistID id, const std::string& model);
    static void SetAudioID(AbstractTypes::PersistID id, int audioID);
};