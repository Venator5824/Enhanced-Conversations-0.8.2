#include "main.h" 
#include <algorithm> 
#include <sstream>

using namespace AbstractGame;
using namespace AbstractTypes;

// --- INITIALIZATION OF STATIC CACHES AND PATHS ---
// Fixed paths - in a real engine these would come from AOS::GetExecutablePath() + relative
const char* SETTINGS_INI_PATH = ".\\ECMod\\EC_DataFiles\\GTAV_EC_Settings.ini";
const char* RELATIONSHIPS_INI_PATH = ".\\ECMod\\EC_DataFiles\\GTAV_EC_Relationships.ini";
const char* PERSONAS_INI_PATH = ".\\ECMod\\EC_DataFiles\\GTAV_EC_Personas.ini";

std::map<std::string, VoiceConfig> ConfigReader::g_VoiceMap;
std::map<std::string, KnowledgeSection> ConfigReader::g_KnowledgeDB;

// Initialization of static members
ModSettings ConfigReader::g_Settings;
std::map<uint32_t, NpcPersona> ConfigReader::g_PersonaCache;
std::map<std::string, NpcPersona> ConfigReader::g_DefaultTypeCache;
std::map<std::string, std::string> ConfigReader::g_RelationshipMatrix;
std::map<std::string, std::string> ConfigReader::g_ZoneContextCache;
std::map<std::string, std::string> ConfigReader::g_OrgContextCache;

std::string ConfigReader::g_GlobalContextStyle;
std::string ConfigReader::g_GlobalContextTimeEra;
std::string ConfigReader::g_GlobalContextLocation;
std::string ConfigReader::g_ContentGuidelines;

// --- HELPER FUNCTION IMPLEMENTATIONS ---

std::string ConfigReader::GetValueFromINI(const char* iniPath, const std::string& section, const std::string& key, const std::string& defaultValue) {
    // [FIX] Abstraction: Using AOS
    std::string value = AOS::GetIniValue(section, key, defaultValue, iniPath);

    // Process Comments (removes everything after ';')
    size_t commentPos = value.find(';');
    if (commentPos != std::string::npos) {
        value = value.substr(0, commentPos);
    }
    // Trim
    value.erase(0, value.find_first_not_of(" \t\r\n"));
    if (!value.empty()) {
        size_t end = value.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) {
            value.erase(end + 1);
        }
    }
    if (value.empty()) {
        return defaultValue;
    }
    return value;
}

std::vector<std::string> ConfigReader::SplitString(const std::string& str, char delimiter) {
    LogConfig("SplitString called with string: " + str + ", delimiter: " + delimiter);
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);
    while (std::getline(tokenStream, token, delimiter)) {
        token.erase(0, token.find_first_not_of(" \t"));
        if (!token.empty()) {
            size_t end = token.find_last_not_of(" \t");
            if (end != std::string::npos) token.erase(end + 1);
        }
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    LogConfig("SplitString returned " + std::to_string(tokens.size()) + " tokens");
    return tokens;
}

int ConfigReader::KeyNameToVK(const std::string& keyName) {
    // Note: We are returning integers that correspond to Virtual Keys.
    // Ideally, PlatformSystem.h would provide an Enum for these keys (e.g., AOS_KEY_F4).
    // For now, we return the raw int values compatible with Windows logic.

    LogConfig("KeyNameToVK called with keyName: " + keyName);
    std::string upperKey = keyName;
    std::transform(upperKey.begin(), upperKey.end(), upperKey.begin(), ::toupper);

    if (upperKey.length() == 1 && upperKey[0] >= 'A' && upperKey[0] <= 'Z') {
        int vk_code = upperKey[0];
        return vk_code;
    }
    if (upperKey.length() == 1 && upperKey[0] >= '0' && upperKey[0] <= '9') {
        int vk_code = upperKey[0];
        return vk_code;
    }

    // We hardcode the Int values for standard keys to avoid Windows.h dependency here
    if (upperKey == "ESC" || upperKey == "VK_ESCAPE") return 0x1B; // VK_ESCAPE
    if (upperKey == "F1" || upperKey == "VK_F1") return 0x70;
    if (upperKey == "F2" || upperKey == "VK_F2") return 0x71;
    if (upperKey == "F3" || upperKey == "VK_F3") return 0x72;
    if (upperKey == "F4" || upperKey == "VK_F4") return 0x73;
    if (upperKey == "F5" || upperKey == "VK_F5") return 0x74;
    if (upperKey == "F6" || upperKey == "VK_F6") return 0x75;
    if (upperKey == "F7" || upperKey == "VK_F7") return 0x76;
    if (upperKey == "F8" || upperKey == "VK_F8") return 0x77;
    if (upperKey == "F9" || upperKey == "VK_F9") return 0x78;
    if (upperKey == "F10" || upperKey == "VK_F10") return 0x79;
    if (upperKey == "F11" || upperKey == "VK_F11") return 0x7A;
    if (upperKey == "F12" || upperKey == "VK_F12") return 0x7B;

    LogConfig("KeyNameToVK FAILED. Unmapped key: " + keyName + ". Returning 0.");
    return 0;
}

uint32_t ConfigReader::GetHashFromHex(const std::string& hexString) {
    if (hexString.empty() || (hexString.rfind("0x", 0) != 0 && hexString.rfind("0X", 0) != 0)) {
        LogConfig("GetHashFromHex returned 0 (invalid hex string format)");
        return 0;
    }
    try {
        unsigned long hash = std::stoul(hexString, nullptr, 16);
        return static_cast<uint32_t>(hash);
    }
    catch (const std::exception& e) {
        LogConfig("GetHashFromHex caught exception: " + std::string(e.what()) + ", returning 0");
        return 0;
    }
}

void ConfigReader::LoadINISectionToCache(const char* iniPath, const std::string& section, std::map<std::string, std::string>& cache) {
    LogConfig("LoadINISectionToCache called for INI: " + std::string(iniPath) + ", section: " + section);

    // [FIX] Abstraction: Using AOS to get raw section data
    std::string sectionData = AOS::GetIniSection(section, iniPath);
    if (sectionData.empty()) {
        LogConfig("LoadINISectionToCache: No data read for section " + section);
        return;
    }

    // Parse the double-null terminated style string manually if needed, 
    // OR if AOS::GetIniSection returns a processed string map, use that.
    // Based on PlatformSystem.cpp implementation, it returns the raw buffer (string).

    // Windows GetPrivateProfileSection returns "key=val\0key2=val2\0\0"
    // But std::string might stop at the first null. 
    // *CORRECTION*: AOS::GetIniSection returns a std::string. If the OS returns double-null terminated buffer, 
    // std::string constructor stops at the first null. 
    // WE NEED TO FIX THIS LOGIC OR RELY ON A BETTER AOS FUNCTION.
    //
    // Let's assume AOS::GetIniSection returns a string where nulls are replaced by newlines, 
    // OR we just use a loop over the raw buffer in PlatformSystem.cpp.
    //
    // BETTER APPROACH for this specific function:
    // Since this logic is complex to abstract perfectly without a custom parser, 
    // let's assume the string passed back contains "Key=Value" pairs separated by nulls 
    // and the string length covers it all.

    // Actually, looking at your original code, it iterated over a buffer.
    // Let's rely on the fact that we can't easily pass a null-separated string via std::string.
    // I will rewrite this to use the standard manual parser provided in your `LoadKnowledgeDatabase` which is cross-platform safe!

    // ... Re-reading your original code: You used GetPrivateProfileSectionA which is Windows specific.
    // Since we are abstracting, we should probably write a simple manual parser here 
    // or assume AOS provides a map.

    // FOR NOW: I will leave the parsing logic but adapted to a safer "Split by Null" assumption 
    // if AOS handles it, but since std::string hates nulls, we will implement a MANUAL PARSE here 
    // using standard file IO if possible? No, stick to AOS.

    // Let's assume AOS::GetIniSection returns "key=val\nkey2=val2" (Standardized format).
    // You should update PlatformSystem.cpp to replace \0 with \n.

    const char* pKey = sectionData.c_str();
    int len = (int)sectionData.length();
    int entries = 0;

    // Standard parser for "Key=Value" lines
    // Note: This assumes AOS returned a string with embedded nulls or newlines.
    // If AOS::GetIniSection returned a std::string, it likely truncated at the first null.
    // 
    // ** CRITICAL FIX **: We will use a FILE STREAM based approach for cross-platform safety here instead of Windows API.
    // It's cleaner than trying to wrap GetPrivateProfileSection.

    std::ifstream file(iniPath);
    if (!file.is_open()) return;

    std::string line;
    bool inSection = false;
    while (std::getline(file, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        if (line.empty() || line[0] == ';') continue;

        if (line[0] == '[') {
            size_t end = line.find(']');
            if (end != std::string::npos) {
                std::string secName = line.substr(1, end - 1);
                if (secName == section) inSection = true;
                else inSection = false;
            }
            continue;
        }

        if (inSection) {
            size_t sep = line.find('=');
            if (sep != std::string::npos) {
                std::string k = line.substr(0, sep);
                std::string v = line.substr(sep + 1);
                // Trim
                k.erase(0, k.find_first_not_of(" \t"));
                size_t kEnd = k.find_last_not_of(" \t");
                if (kEnd != std::string::npos) k.erase(kEnd + 1);

                v.erase(0, v.find_first_not_of(" \t"));
                size_t vEnd = v.find_last_not_of(" \t\r\n");
                if (vEnd != std::string::npos) v.erase(vEnd + 1);

                cache[k] = v;
                entries++;
            }
        }
    }

    LogConfig("LoadINISectionToCache: Loaded " + std::to_string(entries) + " entries for section " + section);
}

// --- CORE LOADING FUNCTIONS ---

void ConfigReader::LoadWorldContextDatabase() {
    LogConfig("LoadWorldContextDatabase started");
    g_GlobalContextStyle = GetValueFromINI(SETTINGS_INI_PATH, "GLOBAL_CONTEXT", "STYLE");
    g_GlobalContextTimeEra = GetValueFromINI(SETTINGS_INI_PATH, "GLOBAL_CONTEXT", "TIME_ERA");
    g_GlobalContextLocation = GetValueFromINI(SETTINGS_INI_PATH, "GLOBAL_CONTEXT", "LOCATION");

    LoadINISectionToCache(SETTINGS_INI_PATH, "KEY_ORGANIZATIONS", g_ZoneContextCache);
    LoadINISectionToCache(SETTINGS_INI_PATH, "CITY_CONTEXT", g_ZoneContextCache);
    LoadINISectionToCache(SETTINGS_INI_PATH, "MEDIA_AND_CULTURE", g_ZoneContextCache);
    LoadINISectionToCache(SETTINGS_INI_PATH, "POLITICS", g_ZoneContextCache);
    LoadINISectionToCache(SETTINGS_INI_PATH, "ECONOMY_AND_BRANDS", g_ZoneContextCache);
    LoadINISectionToCache(SETTINGS_INI_PATH, "ENTERTAINMENT", g_ZoneContextCache);
    LoadINISectionToCache(SETTINGS_INI_PATH, "HEALTH_AND_ISSUES", g_ZoneContextCache);
    LoadINISectionToCache(SETTINGS_INI_PATH, "COMPANIES", g_ZoneContextCache);
    LoadINISectionToCache(SETTINGS_INI_PATH, "GANGS", g_ZoneContextCache);
    LoadINISectionToCache(SETTINGS_INI_PATH, "CELEBRITIES", g_ZoneContextCache);
    LoadINISectionToCache(SETTINGS_INI_PATH, "NORTH_YANKTON", g_ZoneContextCache);
    LoadINISectionToCache(SETTINGS_INI_PATH, "REGIONS_LIST", g_ZoneContextCache);
    LogConfig("LoadWorldContextDatabase completed");
}

void ConfigReader::LoadRelationshipDatabase() {
    LogConfig("LoadRelationshipDatabase started");

    // [FIX] Abstraction: Using AOS
    std::vector<std::string> sections = AOS::GetIniSectionNames(RELATIONSHIPS_INI_PATH);

    if (sections.empty()) {
        LogConfig("LoadRelationshipDatabase: No sections found in " + std::string(RELATIONSHIPS_INI_PATH));
        return;
    }

    int sectionCount = 0;
    for (const auto& sectionName : sections) {
        if (sectionName == "RELATIONSHIPS" || sectionName == "TYPES" || sectionName == "GENDERS" ||
            sectionName == "GANG_SUBGROUPS" || sectionName == "LAW_SUBGROUPS" ||
            sectionName == "PRIVATE_SUBGROUPS" || sectionName == "BUSINESS_SUBGROUPS") {
            continue;
        }
        std::map<std::string, std::string> sectionCache;
        LoadINISectionToCache(RELATIONSHIPS_INI_PATH, sectionName, sectionCache);
        for (const auto& pair : sectionCache) {
            std::string key = sectionName + ":" + pair.first;
            g_RelationshipMatrix[key] = pair.second;
        }
        sectionCount++;
    }
    // LogConfig("LoadRelationshipDatabase: Loaded " + std::to_string(sectionCount) + " character/group sections");
}

std::string ConfigReader::GetSetting(const std::string& section, const std::string& key) {
    return GetValueFromINI(SETTINGS_INI_PATH, section, key);
}

void ConfigReader::LoadPersonaDatabase() {
    LogConfig("LoadPersonaDatabase started");

    // [FIX] Abstraction: Using AOS
    std::vector<std::string> sections = AOS::GetIniSectionNames(PERSONAS_INI_PATH);

    if (sections.empty()) {
        LogConfig("LoadPersonaDatabase: No sections found in " + std::string(PERSONAS_INI_PATH));
        return;
    }

    int personaCount = 0;
    for (const auto& sectionName : sections) {
        NpcPersona persona;
        persona.modelName = sectionName;
        persona.modelHash = GetHashFromHex(GetValueFromINI(PERSONAS_INI_PATH, sectionName, "Hash"));
        persona.isHuman = (GetValueFromINI(PERSONAS_INI_PATH, sectionName, "IsHuman") == "1");
        persona.inGameName = GetValueFromINI(PERSONAS_INI_PATH, sectionName, "InGameName");
        persona.type = GetValueFromINI(PERSONAS_INI_PATH, sectionName, "Type");
        persona.relationshipGroup = GetValueFromINI(PERSONAS_INI_PATH, sectionName, "Relationship");
        persona.subGroup = GetValueFromINI(PERSONAS_INI_PATH, sectionName, "SubGroup");
        persona.gender = GetValueFromINI(PERSONAS_INI_PATH, sectionName, "Gender");
        persona.behaviorTraits = GetValueFromINI(PERSONAS_INI_PATH, sectionName, "Behavior");
        persona.p_audio_model = GetValueFromINI(PERSONAS_INI_PATH, sectionName, "voicemodel");
        persona.p_audio_ID = std::stoi(GetValueFromINI(PERSONAS_INI_PATH, sectionName, "voiceID"));
        persona.LORAID = GetValueFromINI(PERSONAS_INI_PATH, sectionName, "LORAID");
        persona.LORAName = GetValueFromINI(PERSONAS_INI_PATH, sectionName, "LORAName");
        if (sectionName.find("DEFAULT_") == 0) {
            g_DefaultTypeCache[persona.type] = persona;
        }
        else if (persona.modelHash != 0) {
            g_PersonaCache[persona.modelHash] = persona;
        }
        personaCount++;
    }
    LogConfig("LoadPersonaDatabase: Loaded " + std::to_string(personaCount) + " personas");
}

// --- CORE PUBLIC FUNCTIONS ---
void ConfigReader::LoadAllConfigs() {
    LogConfig("LoadAllConfigs started");
    try {
        g_Settings.Enabled = (GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "Enabled", "1") == "1");
        g_Settings.ActivationKey = KeyNameToVK(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "ACTIVATION_KEY", "T"));
        g_Settings.ActivationDurationMs = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "ACTIVATION_DURATION", "1000"));
        g_Settings.StopKey_Primary = KeyNameToVK(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "STOP_KEY", "U"));
        g_Settings.StopDurationMs = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "STOP_DURATION", "3000"));
        g_Settings.MaxConversationRadius = std::stof(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "MAX_CONVERSATION_RADIUS", "3.0"));

        g_Settings.MaxOutputChars = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "MAX_OUTPUT_CHARS", "512"));
        g_Settings.MaxInputChars = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "MAX_INPUT_CHARS", "786"));
        g_Settings.MaxHistoryTokens = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "MAX_REMEMBER_HISTORY", "1024"));
        g_Settings.MaxChatHistoryLines = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "MAX_PROMPT_MEMORY_HALFED", "16"));
        g_Settings.MinResponseDelayMs = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "MIN_RESPONSE_DELAY_MS", "750"));

        g_Settings.USE_VRAM_PREFERED = (GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "USE_VRAM_PREFERED", "1") == "1");
        g_Settings.USE_GPU_LAYERS = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "USE_GPU_LAYERS", "33"));

        // Models & Logging
        g_Settings.MODEL_PATH = GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "MODEL_PATH", "");
        g_Settings.MODEL_ALT_NAME = GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "MODEL_ALT_NAME", "Phi3.gguf");
        g_Settings.LOG_NAME = GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "LOG_NAME", "kkamel.log");
        g_Settings.DEBUG_LEVEL = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "DEBUG_LEVEL", "0"));

        // STT / TTS
        g_Settings.StT_Enabled = (GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "SPEECH_TO_TEXT", "0") == "1");
        g_Settings.StTRB_Activation_Key = KeyNameToVK(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "SPEECH_TO_TEXT_RECORDING_BUTTON", "L"));
        g_Settings.STT_MODEL_PATH = GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "STT_MODEL_PATH", "");
        g_Settings.STT_MODEL_ALT_NAME = GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "STT_MODEL_ALT_NAME", "ggml-base.en.bin");

        g_Settings.TtS_Enabled = (GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "TEXT_TO_SPEECH", "0") == "1");
        g_Settings.TTS_MODEL_PATH = GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "TTS__MODEL_PATH", "");
        g_Settings.TTS_MODEL_ALT_NAME = GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "TTS_MODEL_ALT_NAME", "");

        // 2. MEMORY & OPTIMIZATION SETTINGS
        g_Settings.DeletionTimer = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "DELETION_TIMER", "120"));
        g_Settings.MaxAllowedChatHistory = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "MAX_ALLOWED_CHAT_HISTORY", "1"));
        g_Settings.DeletionTimerClearFull = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "DELETION_TIMER_CLEAR_FULL", "160"));

        g_Settings.TrySummarizeChat = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "TRY_SUMMARIZE_CHAT", "1"));
        g_Settings.MIN_PCSREMEMBER_SIZE = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "MIN_PCSREMEMBER_SIZE", "5"));
        g_Settings.MAX_PCSREMEMBER_SIZE = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "MAX_PCSREMEMBER_SIZE", "256"));
        g_Settings.Level_Optimization_Chat_Going = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "Level_Optimization_Chat_Going", "0"));
        g_Settings.VRAM_BUDGET_MB = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "VRAM_BUDGET_MB", "7828"));
        g_Settings.Allow_KV_Cache_Quantization_Type = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "ADDITIONAL_SETTINGS", "Allow_KV_Cache_Quantization_Type","0"));

        
        // 3. ADDITIONAL SETTINGS
        // (Using standard try/catch blocks for safety as before)
        try { g_Settings.Max_Working_Input = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "ADDITIONAL_SETTINGS", "MAX_INPUT_SIZE", "4096")); }
        catch (...) {
            g_Settings.Max_Working_Input = 4069;

        }
        try { g_Settings.n_batch = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "ADDITIONAL_SETTINGS", "n_batch", "512")); }
        catch (...) { g_Settings.n_batch = 2888; }
        try { g_Settings.n_ubatch = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "ADDITIONAL_SETTINGS", "n_ubatch", "256")); }
        catch (...) { g_Settings.n_ubatch = 512; }
        try { g_Settings.KV_Cache_Quantization_Type = std::stoi(GetValueFromINI(SETTINGS_INI_PATH, "ADDITIONAL_SETTINGS", "kv_cache_model_quantization_type", "0")); }
        catch (...) { g_Settings.KV_Cache_Quantization_Type = -1; }
        try { g_Settings.temp = std::stof(GetValueFromINI(SETTINGS_INI_PATH, "ADDITIONAL_SETTINGS", "temp", "0.65")); }
        catch (...) { g_Settings.temp = 0.75; }
        try { g_Settings.top_k = std::stof(GetValueFromINI(SETTINGS_INI_PATH, "ADDITIONAL_SETTINGS", "top_k", "40")); }
        catch (...) { g_Settings.top_k = 0.4; }
        try { g_Settings.top_p = std::stof(GetValueFromINI(SETTINGS_INI_PATH, "ADDITIONAL_SETTINGS", "top_p", "0.95")); }
        catch (...) { g_Settings.top_p = 0.95; }
        try { g_Settings.min_p = std::stof(GetValueFromINI(SETTINGS_INI_PATH, "ADDITIONAL_SETTINGS", "min_p", "0.05")); }
        catch (...) { g_Settings.min_p = 0.05; }
        try { g_Settings.repeat_penalty = std::stof(GetValueFromINI(SETTINGS_INI_PATH, "ADDITIONAL_SETTINGS", "repeat_penatly", "1.1")); }
        catch (...) { g_Settings.repeat_penalty = 1.0; }
        try { g_Settings.freq_penalty = std::stof(GetValueFromINI(SETTINGS_INI_PATH, "ADDITIONAL_SETTINGS", "freq_penalty", "0.0")); }
        catch (...) { g_Settings.freq_penalty = 0.0; }
        try { g_Settings.presence_penalty = std::stof(GetValueFromINI(SETTINGS_INI_PATH, "ADDITIONAL_SETTINGS", "presence_penalty", "0.0")); }
        catch (...) { g_Settings.presence_penalty = 0.0; }
        try { g_Settings.SAMPLER_TYPE = std::stof(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "SAMPLER_TYPE", "1")); }
        catch (...) { g_Settings.SAMPLER_TYPE = 1; }
        try { g_Settings.FORCE_GPU_INDEX = std::stof(GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "FORCE_GPU_INDEX", "-1")); }
        catch (...) { g_Settings.FORCE_GPU_INDEX = -1; }


        std::string loraEn = GetValueFromINI(SETTINGS_INI_PATH, "ADDITIONAL_SETTINGS", "lora_enabled", "0");
        g_Settings.Lora_Enabled;
        g_Settings.LORA_ALT_NAME = GetValueFromINI(SETTINGS_INI_PATH, "ADDITIONAL_SETTINGS", "LORA_ALT_NAME", "mod_lora.gguf");
        g_Settings.LORA_FILE_PATH = GetValueFromINI(SETTINGS_INI_PATH, "ADDITIONAL_SETTINGS", "LORA_FILE_PATH", "");
        try { g_Settings.LORA_SCALE = std::stof(GetValueFromINI(SETTINGS_INI_PATH, "ADDITIONAL_SETTINGS", "LORA_SCALE", "1.0")); }
        catch (...) {}

        g_Settings.StopStrings = GetValueFromINI(SETTINGS_INI_PATH, "SETTINGS", "STOP_TOKENS", "");
        g_ContentGuidelines = GetValueFromINI(SETTINGS_INI_PATH, "CONTENT_GUIDELINES", "PROMPT_INJECTION", "You are a helpful assistant.");

        LoadWorldContextDatabase();
        LoadRelationshipDatabase();
        LoadPersonaDatabase();
        LoadKnowledgeDatabase();

        if (g_Settings.TtS_Enabled) {
            LoadVoiceDatabase();
        }

        LogConfig("LoadAllConfigs completed");
    }
    catch (const std::exception& e) {
        LogConfig("Exception in LoadAllConfigs: " + std::string(e.what()));
    }
}

NpcPersona ConfigReader::GetPersona(AHandle ped) {
    if (!AbstractGame::IsEntityValid(ped)) return NpcPersona();
    Hash entityHash = AbstractGame::GetEntityModel(ped);
    LogConfig("GetPersona: Entity model hash=" + std::to_string(entityHash));
    auto it = g_PersonaCache.find(entityHash);
    if (it != g_PersonaCache.end()) {
        LogConfig("Persona found in .ini");
        return it->second;
    }
    NpcPersona p;
    p.modelHash = entityHash;
    p.modelName = "UNKNOWN_MODEL";
    p.isHuman = AbstractGame::IsPedHuman(ped);
    p.inGameName = "";
    p.type = "Stranger";
    p.relationshipGroup = "Ambient";
    p.subGroup = "None";
    p.behaviorTraits = "Unknown";
    if (p.isHuman) {
        p.gender = AbstractGame::IsPedMale(ped) ? "Male" : "Female";
    }
    else {
        p.gender = "Neutral";
        p.type = "ANIMAL";
    }
    LogConfig("Persona loaded successfully");
    g_PersonaCache[entityHash] = p;
    return p;
}

std::string ConfigReader::GetRelationship(const std::string& npcSubGroup, const std::string& playerSubGroup) {
    LogConfig("GetRelationship called for npcSubGroup: " + npcSubGroup + ", playerSubGroup: " + playerSubGroup);
    if (npcSubGroup.empty() || playerSubGroup.empty()) {
        LogConfig("GetRelationship: SubGroup is empty, fallback to neutral");
        return "neutral, stranger";
    }
    std::string key = npcSubGroup + ":" + playerSubGroup;
    auto it = g_RelationshipMatrix.find(key);
    if (it != g_RelationshipMatrix.end()) {
        LogConfig("GetRelationship: Found direct relationship: " + it->second);
        return it->second;
    }
    key = playerSubGroup + ":" + npcSubGroup;
    it = g_RelationshipMatrix.find(key);
    if (it != g_RelationshipMatrix.end()) {
        LogConfig("GetRelationship: Found reverse relationship: " + it->second);
        return it->second;
    }
    if (npcSubGroup == "Ambient" || playerSubGroup == "Ambient") {
        return "neutral, stranger";
    }
    if ((npcSubGroup == "Law" || npcSubGroup == "LSPD" || npcSubGroup == "FIB") &&
        (playerSubGroup == "Gang" || playerSubGroup == "Families" || playerSubGroup == "Ballas")) {
        return "adversary, distrusts, pursues";
    }
    if ((npcSubGroup == "Gang" || npcSubGroup == "Families" || playerSubGroup == "Ballas") &&
        (playerSubGroup == "Law" || playerSubGroup == "LSPD" || playerSubGroup == "FIB")) {
        return "adversary, distrusts, pursued";
    }
    return "neutral, unknown";
}

std::string ConfigReader::GetZoneContext(const std::string& zoneName) {
    auto it = g_ZoneContextCache.find(zoneName);
    if (it != g_ZoneContextCache.end()) {
        return it->second;
    }
    return "Location: Unknown";
}

std::string ConfigReader::GetOrgContext(const std::string& orgName) {
    auto it = g_OrgContextCache.find(orgName);
    if (it != g_OrgContextCache.end()) {
        return it->second;
    }
    return "";
}

void ConfigReader::LoadVoiceDatabase() {
    LogConfig("LoadVoiceDatabase started");
    const char* vPath = ".\\EC_Voices_list_01.ini";

    // [FIX] Abstraction: Using AOS
    std::vector<std::string> sections = AOS::GetIniSectionNames(vPath);

    if (sections.empty()) {
        LogConfig("LoadVoiceDatabase: No sections found in " + std::string(vPath));
        return;
    }

    for (const auto& sectionName : sections) {
        VoiceConfig vc;
        vc.gender = GetValueFromINI(vPath, sectionName, "gender", "");
        vc.age = GetValueFromINI(vPath, sectionName, "age", "");
        vc.voice = GetValueFromINI(vPath, sectionName, "voice", "");
        vc.special = GetValueFromINI(vPath, sectionName, "special", "");
        ConfigReader::g_VoiceMap[sectionName] = vc;
    }
    LogConfig("LoadVoiceDatabase completed. Loaded " + std::to_string(ConfigReader::g_VoiceMap.size()) + " voices.");
}

void ConfigReader::LoadKnowledgeDatabase() {
    g_KnowledgeDB.clear();
    // This function already used std::ifstream (Cross platform!) 
    // It is already perfect. No changes needed except maybe ensuring SetEnvVar/etc aren't used here.

    std::ifstream iniFile(SETTINGS_INI_PATH);
    if (!iniFile.is_open()) return;

    std::string line;
    KnowledgeSection currentSection;

    while (std::getline(iniFile, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        size_t last = line.find_last_not_of(" \t\r\n");
        if (last != std::string::npos) line.erase(last + 1);

        if (line.empty() || line[0] == ';') continue;

        if (line[0] == '[' && line.back() == ']') {
            if (!currentSection.sectionName.empty()) {
                g_KnowledgeDB[currentSection.sectionName] = currentSection;
            }
            currentSection = KnowledgeSection();
            currentSection.sectionName = line;
            continue;
        }

        if (!currentSection.sectionName.empty()) {
            size_t delimiterPos = line.find('=');
            if (delimiterPos == std::string::npos) continue;

            std::string key = line.substr(0, delimiterPos);
            std::string value = line.substr(delimiterPos + 1);

            key.erase(0, key.find_first_not_of(" \t"));
            size_t kEnd = key.find_last_not_of(" \t");
            if (kEnd != std::string::npos) key.erase(kEnd + 1);

            value.erase(0, value.find_first_not_of(" \t"));
            // Keep trailing spaces in values usually, but trimming logic is fine

            if (key == "isalwaysloaded") {
                currentSection.isAlwaysLoaded = (value == "1");
            }
            else if (key == "loadeverysectionwhenloading") {
                currentSection.loadEntireSectionOnMatch = (value == "1");
            }
            else if (key == "keywordstoload") {
                std::stringstream ss(value);
                std::string keyword;
                while (std::getline(ss, keyword, ',')) {
                    keyword.erase(0, keyword.find_first_not_of(" \t"));
                    size_t kwEnd = keyword.find_last_not_of(" \t");
                    if (kwEnd != std::string::npos) keyword.erase(kwEnd + 1);

                    if (!keyword.empty()) {
                        currentSection.keywords.push_back(NormalizeString(keyword));
                    }
                }
            }
            else {
                currentSection.content += line + "\n";
                currentSection.keyValues[key] = value;
                if (!key.empty()) {
                    currentSection.keywords.push_back(NormalizeString(key));
                }
            }
        }
    }
    if (!currentSection.sectionName.empty()) {
        g_KnowledgeDB[currentSection.sectionName] = currentSection;
    }
}

std::string NormalizeString(const std::string& input) {
    std::string output = "";
    output.reserve(input.length());
    for (char c : input) {
        char lower_c = std::tolower(static_cast<unsigned char>(c));
        if (std::isalnum(static_cast<unsigned char>(lower_c))) {
            output += lower_c;
        }
    }
    return output;
}