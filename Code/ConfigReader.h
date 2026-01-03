#pragma once
#include "AbstractTypes.h"
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <fstream>


// ---------------------------------------------------------------------
// 1. DATA STRUCTURES
// ---------------------------------------------------------------------

struct VoiceConfig {
    std::string gender;
    std::string age;
    std::string voice;
    std::string special;
};

struct NpcPersona {
    uint32_t modelHash = 0; 
    bool isHuman = true;
    std::string modelName = "DEFAULT_UNKNOWN_PED";
    std::string inGameName = "";
    std::string type = "DEFAULT";
    std::string relationshipGroup = "Ambient";
    std::string subGroup = "";
    std::string gender = "M";
    std::string behaviorTraits = "generic, neutral, friendly, random";
    std::string assignedVoiceId;
    // NEW for audio:
    std::string n_gender = "m";    
    std::string n_age = "";       
    std::string n_voicetype = "";
    std::string p_audio_model = "";
    int p_audio_ID =  -1000;
    std::string LORAName = "";
    std::string LORAID = "";
};

struct ModSettings {
    bool Enabled = false;
    int ActivationKey = 0x54;
    int ActivationDurationMs = 1000;
    int StopKey_Primary = 0x1B;
    int StopKey_Secondary = 0;
    int StopDurationMs = 3000;
    int MaxInputChars = 128;
    float MaxConversationRadius = 3.0f;
    int MaxOutputChars = 70;
    int MinResponseDelayMs = 1500;
    int MaxHistoryTokens = 2048;
    int DeletionTimer = 120;
    int MaxNpcGetModel = 1;
    int MaxChatHistoryLines = 10;
    int USE_GPU_LAYERS = 0;
    bool USE_VRAM_PREFERED = false;
    int StT_Enabled = 0;
    int TtS_Enabled = 0;
    std::string MODEL_PATH = "";
    std::string MODEL_ALT_NAME = "";
    int StTRB_Activation_Key = 0;
    int DEBUG_LEVEL = 0;
    std::string LOG_NAME = "kkamel.log";
    std::string LOG2_NAME = "kkamel_metrics.log";
    std::string LOG3_NAME = "kkamel_timers.log";
    std::string LOG4_NAME = "kkamel_load.log";
    std::string STT_MODEL_PATH = "";
    std::string STT_MODEL_ALT_NAME = "";
    std::string TTS_MODEL_PATH = "";
    std::string TTS_MODEL_ALT_NAME = "";
    std::string GET_TARGET_GAME = "TARGET_GAME_GTAV ";
    int SAMPLER_TYPE = 1; // 0 = greedy default, 1 == chain quality, 2 = manual sampler
    int FORCE_GPU_INDEX = -1; // -1 or 0 or anythign else not given = auto, set number for gpu is use only that gpu. 
    uint32_t Max_Working_Input = 4096;
    int Allow_EX_Script = 0;
    int KV_Cache_Quantization_Type = 16;
    int n_batch = 512;
    int n_ubatch = 256;
    float temp = 0.65f;
    float top_k = 40.0f;
    float top_p = 0.9f;
    float min_p = 0.05f;
    float float_p = 0.0f;
    float repeat_penalty = 1.1f;
    float freq_penalty = 0.0f;
    float presence_penalty = 0.0f;
    int Level_Optimization_Chat_Going = 0;
    float VRAM_BUDGET_MB = 7828;
    // LoRA
    int Lora_Enabled = 0;
    std::string LORA_ALT_NAME;
    std::string LORA_FILE_PATH;
    float LORA_SCALE = 1.0f;
    int Allow_KV_Cache_Quantization_Type = 0;
    // Memory Logic
    int TrySummarizeChat = 0;
    int MIN_PCSREMEMBER_SIZE = 100;
    int MAX_PCSREMEMBER_SIZE = 500;
    std::string StopStrings = "";
    int DeletionTimerClearFull = 160;
    int MaxAllowedChatHistory = 2;
    
};

struct KnowledgeSection {
    std::string sectionName;
    std::string content;
    std::map<std::string, std::string> keyValues;
    std::vector<std::string> keywords;
    bool isAlwaysLoaded = false;
    bool loadEntireSectionOnMatch = true;
};

// ---------------------------------------------------------------------
// 2. CONFIG READER CLASS
// ---------------------------------------------------------------------
class ConfigReader {
public:
    // Caches
    static ModSettings g_Settings;
    static std::map<uint32_t, NpcPersona> g_PersonaCache;
    static std::map<std::string, NpcPersona> g_DefaultTypeCache;
    static std::map<std::string, std::string> g_RelationshipMatrix;
    static std::map<std::string, std::string> g_ZoneContextCache;
    static std::map<std::string, std::string> g_OrgContextCache;
    static std::string g_GlobalContextStyle;
    static std::string g_GlobalContextTimeEra;
    static std::string g_GlobalContextLocation;
    static std::map<std::string, VoiceConfig> g_VoiceMap;
    static std::map<std::string, KnowledgeSection> g_KnowledgeDB;
    static std::string g_ContentGuidelines;

    // Public API
    static void LoadAllConfigs();
    static NpcPersona GetPersona(AHandle npc);
    static std::string GetRelationship(const std::string& npcSubGroup, const std::string& playerSubGroup);
    static std::string GetZoneContext(const std::string& zoneName);
    static std::string GetOrgContext(const std::string& orgName);
    static std::string GetSetting(const std::string& section, const std::string& key);

    // Utilities made public for LLM inference
    static std::vector<std::string> SplitString(const std::string& str, char delimiter);
    static int KeyNameToVK(const std::string& keyName);

private:
    static void LoadKnowledgeDatabase();
    static std::string GetValueFromINI(const char* iniPath, const std::string& section, const std::string& key, const std::string& defaultValue = "");
    static void LoadINISectionToCache(const char* iniPath, const std::string& section, std::map<std::string, std::string>& cache);
    static void LoadPersonaDatabase();
    static void LoadRelationshipDatabase();
    static void LoadVoiceDatabase();
    static void LoadWorldContextDatabase();
    static uint32_t GetHashFromHex(const std::string& hexString);
};

//EOF