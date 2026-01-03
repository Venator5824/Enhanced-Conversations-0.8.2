#define _CRT_SECURE_NO_WARNINGS
#include "main.h"
#include <sstream>
#include <fstream>
#include <iomanip>
#include <cstring>
#include <future>
#include <iostream>

        // Note: helperfunctions.h should contain the forward declarations for these.

        using namespace AbstractGame;
using namespace AbstractTypes;


std::string LOG_FILE_NAME_METRICS;
std::string LOG_FILE_NAME_AUDIO;


// --- LOGGING IMPLEMENTATION ---
// Standard C++ IO is cross-platform safe. No need to abstract this unless you want Android support.

void Log(const std::string & msg) {
    std::ofstream f(LOG_FILE_NAME, std::ios::app);
    if (f) {
        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);
        f << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] " << msg << "\n";
    }
}

void LogM(const std::string & msg) {
    std::ofstream f(LOG_FILE_NAME_METRICS, std::ios::app);
    if (f) {
        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);
        f << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] " << msg << "\n";
    }
}

void LogA(const std::string & msg) {
    std::ofstream f(LOG_FILE_NAME_AUDIO, std::ios::app);
    if (f) {
        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);
        f << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] " << msg << "\n";
    }
}

void LogConfig(const std::string & message) {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::ofstream log("kkamel_loader.log", std::ios_base::app);
    if (log.is_open()) {
        log << "[" << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") << "] [ConfigReader] " << message << "\n";
        log.flush();
    }
}

void LogPerf(const std::string& msg) {
    std::ofstream f("kkamel_performance.log", std::ios::app);
    if (f) {
        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);
        f << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] " << msg << "\n";
    }
}

std::string FindLoRAFile(const std::string & root_path) {
    const std::string DEFAULT_LORA_NAME = "mod_lora.gguf";

    const std::string& custom_path = ConfigReader::g_Settings.LORA_FILE_PATH;
    const std::string& custom_name = ConfigReader::g_Settings.LORA_ALT_NAME;

    // Use correct path separator from Platform.h
    std::string sep;
    sep += PATH_SEPARATOR;

    // --- 1. Check 1: Custom Path + Custom Name ---
    if (!custom_path.empty() && !custom_name.empty()) {
        std::string full_path = custom_path;
        if (full_path.back() != PATH_SEPARATOR && full_path.back() != '/') {
            full_path += sep;
        }
        full_path += custom_name;
        if (DoesFileExist(full_path)) {
            Log("LoRA Resolver: Found custom path/name: " + full_path);
            return full_path;
        }
    }

    // --- 2. Check 2: Root Path + Custom Name ---
    if (!custom_name.empty()) {
        std::string full_path = root_path + custom_name;
        if (DoesFileExist(full_path)) {
            Log("LoRA Resolver: Found root/custom name: " + full_path);
            return full_path;
        }
    }

    // --- 3. Check 3: Root Path + Default Name ---
    std::string default_path = root_path + DEFAULT_LORA_NAME;
    if (DoesFileExist(default_path)) {
        Log("LoRA Resolver: Found default file: " + default_path);
        return default_path;
    }

    Log("LoRA Resolver: No LoRA file found matching configuration.");
    return "";
}

std::string WordWrap(const std::string & text, size_t limit) {
    if (text.empty()) return "";
    std::stringstream result;
    std::stringstream currentWord;
    size_t currentLineLength = 0;
    for (char c : text) {
        if (std::isspace(c)) {
            if (currentLineLength + currentWord.str().length() > limit) {
                result << '\n';
                currentLineLength = 0;
            }
            result << currentWord.str() << ' ';
            currentLineLength += currentWord.str().length() + 1;
            currentWord.str("");
        }
        else {
            currentWord << c;
        }
    }
    if (currentWord.str().length() > 0) {
        if (currentLineLength + currentWord.str().length() > limit) {
            result << '\n';
        }
        result << currentWord.str();
    }
    return result.str();
}

// ---------------------------------------------------------------------
// EXPORTED API FUNCTIONS (Cross-Platform)
// ---------------------------------------------------------------------

extern "C" {

    // [FIX] Abstraction: GAME_API replaces __declspec(dllexport)

    GAME_API bool API_IsModReady() {
        return g_isInitialized;
    }

    GAME_API bool API_IsBusy() {
        return (g_convo_state != ConvoState::IDLE || g_llm_state != InferenceState::IDLE);
    }

    GAME_API void API_StartConversation(int pedHandle, const char* name_override, const char* instruction, int npc_control_type, bool allow_TTS) {
        if (!IsEntityValid((AHandle)pedHandle)) return;

        // Clean up previous if distinct
        if (g_target_ped != 0 && g_target_ped != (AHandle)pedHandle) {
            EndConversation();
        }

        g_target_ped = (AHandle)pedHandle;

        ChatID chatID = ConvoManager::InitiateConversation(GetPlayerHandle(), (AHandle)pedHandle);
        g_current_chat_ID = chatID;

        Log("[API] Started Chat ID: " + std::to_string(chatID));

        std::string finalName;
        if (name_override && name_override[0] != '\0') finalName = name_override;
        else {
            NpcPersona p = ConfigReader::GetPersona((AHandle)pedHandle);
            finalName = GenerateNpcName(p);
        }
        g_current_npc_name = finalName;

        g_convo_state = ConvoState::IN_CONVERSATION;

        StartNpcConversationTasks(g_target_ped, GetPlayerHandle(), npc_control_type);

        std::vector<std::string> history = ConvoManager::GetChatHistory(chatID);
        std::string prompt = AssemblePrompt(g_target_ped, GetPlayerHandle(), history);

        if (instruction != nullptr && instruction[0] != '\0') {
            std::string instrStr = instruction;
            prompt += "\n[SYSTEM INSTRUCTION]: " + instrStr + "\n<|assistant|>\n";
        }

        g_response_start_time = std::chrono::high_resolution_clock::now();
        g_llm_start_time = std::chrono::high_resolution_clock::now();

        g_llm_future = std::async(std::launch::async, GenerateLLMResponse, prompt, false);
        g_llm_state = InferenceState::RUNNING;
    }

    GAME_API void API_StopConversation() {
        EndConversation();
    }

    GAME_API void API_ShowSubtitle(const char* speaker, const char* message) {
        if (message && speaker) {
            // Assuming g_Subtitles is your global instance
            g_Subtitles.ShowMessage(std::string(speaker), std::string(message));
        }
    }

    GAME_API bool API_GetLastResponse(char* buffer, int bufferSize) {
        if (g_renderText.empty()) return false;
        if (g_renderText.length() + 1 > static_cast<size_t>(bufferSize)) return false;

        // [FIX] Safe String Copy
        strncpy(buffer, g_renderText.c_str(), bufferSize);
        buffer[bufferSize - 1] = '\0'; // Ensure null termination
        return true;
    }

    GAME_API bool API_GetNpcName(char* buffer, int bufferSize) {
        if (g_current_npc_name.empty()) return false;
        if (g_current_npc_name.length() + 1 > static_cast<size_t>(bufferSize)) return false;

        strncpy(buffer, g_current_npc_name.c_str(), bufferSize);
        buffer[bufferSize - 1] = '\0';
        return true;
    }

    GAME_API void API_ClearCache() {
        if (g_ctx) {
            llama_memory_t mem = llama_get_memory(g_ctx);
            if (mem) {
                llama_memory_clear(mem, true);
                Log("[API] KV Cache (LLM working memory) cleared.");
                return;
            }
        }
        Log("[API] KV Cache: No context/memory to clear.");
    }

    GAME_API void API_DeloadLLM() {
        if (!g_model) {
            Log("[API] Deload LLM: Model not currently loaded.");
            return;
        }

        EndConversation();
        ShutdownLLM();
        g_isInitialized = false;

        Log("[API] Deload LLM: Model and context fully unloaded. VRAM freed.");
    }

    GAME_API bool API_LoadLLM() {
        if (g_isInitialized && g_model) {
            Log("[API] Load LLM: Already initialized, skipping load.");
            return true;
        }

        Log("[API] Load LLM: Starting full re-initialization sequence.");

        try {
            std::string root = GetModRootPath();
            std::string modelPath;
            const auto& cust = ConfigReader::g_Settings.MODEL_PATH;
            const auto& alt = ConfigReader::g_Settings.MODEL_ALT_NAME;
            const auto def = "Phi3.gguf";

            if (!cust.empty() && DoesFileExist(cust)) modelPath = cust;
            else if (!alt.empty() && DoesFileExist(root + alt)) modelPath = root + alt;
            else if (DoesFileExist(root + def)) modelPath = root + def;

            if (modelPath.empty()) {
                Log("FATAL: API_LoadLLM failed: No LLM model found.");
                return false;
            }

            Log("Using LLM: " + modelPath);
            if (!InitializeLLM(modelPath.c_str())) {
                Log("FATAL: API_LoadLLM failed: InitializeLLM() failed.");
                return false;
            }

            enum ggml_type kv_type = GGML_TYPE_F32;
            llama_context_params ctx_params = llama_context_default_params();
            ctx_params.n_ctx = static_cast<uint32_t>(ConfigReader::g_Settings.Max_Working_Input);
            ctx_params.n_batch = 1024;
            ctx_params.n_ubatch = 256;

            if (ConfigReader::g_Settings.USE_VRAM_PREFERED) {
                ctx_params.type_k = GGML_TYPE_F16;
                ctx_params.type_v = GGML_TYPE_F16;
            }

            // Quantization Setup
            switch (ConfigReader::g_Settings.KV_Cache_Quantization_Type)
            {
            case 2: kv_type = GGML_TYPE_Q2_K; break;
            case 3: kv_type = GGML_TYPE_Q3_K; break;
            case 4: kv_type = GGML_TYPE_Q4_K; break;
            case 5: kv_type = GGML_TYPE_Q5_K; break;
            case 6: kv_type = GGML_TYPE_Q6_K; break;
            case 8: kv_type = GGML_TYPE_Q8_0; break;
            default: break; // Default F32/F16
            }

            if (kv_type != GGML_TYPE_F32) {
                ctx_params.type_k = kv_type;
                ctx_params.type_v = kv_type;
                Log("KV Cache Quantization Enabled.");
            }

            g_ctx = llama_init_from_model(g_model, ctx_params);
            if (!g_ctx) {
                Log("FATAL: API_LoadLLM failed: llama_init_from_model failed.");
                return false;
            }

            if (ConfigReader::g_Settings.StT_Enabled) {
                std::string sttPath;
                const auto& custSTT = ConfigReader::g_Settings.STT_MODEL_PATH;
                const auto& altSTT = ConfigReader::g_Settings.STT_MODEL_ALT_NAME;

                if (!custSTT.empty() && DoesFileExist(custSTT)) sttPath = custSTT;
                else if (!altSTT.empty() && DoesFileExist(root + altSTT)) sttPath = root + altSTT;

                if (!sttPath.empty() && InitializeWhisper(sttPath.c_str()) && InitializeAudioCaptureDevice()) {
                    Log("Whisper re-initialized successfully.");
                }
                else {
                    Log("STT re-initialization failed or model missing.");
                    ConfigReader::g_Settings.StT_Enabled = false;
                }
            }

            g_isInitialized = true;
            Log("[API] Load LLM: Model and context loaded successfully.");
            return true;

        }
        catch (const std::exception& e) {
            Log("CRITICAL EXCEPTION in API_LoadLLM: " + std::string(e.what()));
            return false;
        }
        catch (...) {
            Log("CRITICAL UNKNOWN EXCEPTION in API_LoadLLM.");
            return false;
        }
    }

    GAME_API void API_LogMessage(const char* message) {
        if (message != nullptr) {
            Log("[API External] " + std::string(message));
        }
    }

    GAME_API void API_SetNpcName(const char* newName) {
        if (newName != nullptr && newName[0] != '\0') {
            g_current_npc_name = newName;
            Log("[API] Set NPC Name: Name forced to " + g_current_npc_name);
        }
    }

    GAME_API void API_ClearAllNpcMemories() {
        EndConversation();
        ConvoManager::RunMaintenance();
    }

    GAME_API float OutputFreeVRAM_MB() {
        return GetFreeVRAM_MB();
    }

    GAME_API int API_Convo_Initiate(int initiatorHandle, int targetHandle, int forceID) {
        return (int)ConvoManager::InitiateConversation((AHandle)initiatorHandle, (AHandle)targetHandle, (ChatID)forceID);
    }

    GAME_API int API_Convo_GetActiveID(int entityHandle) {
        return (int)ConvoManager::GetActiveChatID((AHandle)entityHandle);
    }

    GAME_API void API_Convo_SetContext(int chatID, const char* location, const char* weather) {
        if (chatID <= 0) return;
        std::string locStr = (location) ? location : "Unknown";
        std::string weaStr = (weather) ? weather : "Unknown";
        ConvoManager::SetChatContext((ChatID)chatID, locStr, weaStr);
    }

    GAME_API void API_Convo_AddMessage(int chatID, const char* sender, const char* message) {
        if (chatID <= 0 || !message) return;
        std::string senderStr = (sender) ? sender : "System";
        std::string msgStr = message;
        ConvoManager::AddMessageToChat((ChatID)chatID, senderStr, msgStr);
    }

    GAME_API void API_Convo_Close(int chatID) {
        ConvoManager::CloseConversation((ChatID)chatID);
    }

    GAME_API int API_Convo_GetHistoryCount(int chatID) {
        auto history = ConvoManager::GetChatHistory((ChatID)chatID);
        return (int)history.size();
    }

    GAME_API bool API_Convo_GetHistoryLine(int chatID, int index, char* buffer, int bufferSize) {
        auto history = ConvoManager::GetChatHistory((ChatID)chatID);

        if (index < 0 || index >= history.size()) return false;

        const std::string& line = history[index];
        if (line.length() + 1 > (size_t)bufferSize) return false;

        strncpy(buffer, line.c_str(), bufferSize);
        buffer[bufferSize - 1] = '\0';
        return true;
    }

    GAME_API void API_SetEntityIdentity(int pedHandle, const char* name, const char* gender) {
        if (!g_isInitialized) return;

        // Ensure they are registered first
        PersistID id = EntityRegistry::RegisterNPC((AHandle)pedHandle);

        std::string n = (name) ? name : "";
        std::string g = (gender) ? gender : "";

        EntityRegistry::SetIdentity(id, n, g);
        Log("[API] Identity set for Handle " + std::to_string(pedHandle));
    }

    // Allows C# to give the NPC a goal (e.g., "Arrest player", "Flee")
    GAME_API void API_SetEntityGoal(int pedHandle, const char* goal) {
        if (!g_isInitialized) return;
        PersistID id = EntityRegistry::RegisterNPC((AHandle)pedHandle);
        if (goal) {
            EntityRegistry::SetGoal(id, std::string(goal));
        }
    }

    // Allows C# to inject facts (e.g., "You saw the player steal that car")
    GAME_API void API_AddEntityMemory(int pedHandle, const char* fact) {
        if (!g_isInitialized) return;
        PersistID id = EntityRegistry::RegisterNPC((AHandle)pedHandle);
        if (fact) {
            EntityRegistry::AppendMemory(id, std::string(fact));
        }
    }

    // Allows C# to force a specific voice ID (e.g., ensure Cop #1 always sounds like Cop #1)
    GAME_API void API_SetEntityVoice(int pedHandle, const char* model, int id) {
        if (!g_isInitialized) return;
        PersistID pid = EntityRegistry::RegisterNPC((AHandle)pedHandle);
        VoiceSettings vs;
        vs.model = (model) ? model : "NONE";
        vs.id = id;
        vs.speed = 1.0f; // Default speed
        EntityRegistry::SetVoiceSettings(pid, vs);
        // Pre-load it if we can
        if (ConfigReader::g_Settings.TtS_Enabled && vs.model != "NONE") {
            // Dispatch a background load so it's ready when they speak
            std::async(std::launch::async, [=]() { AudioManager::LoadAudioModel(vs.model); });
        }
    }
    // --- STATE GETTERS (For C# UI) ---

    GAME_API bool API_HasEntityBrain(int pedHandle) {
        PersistID id = EntityRegistry::GetIDFromHandle((AHandle)pedHandle);
        return (id != 0);
    }
}

// ---------------------------------------------------------------------
// INTERNAL HELPERS
// ---------------------------------------------------------------------

std::vector<std::string> SplitString(const std::string & str, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);
    while (std::getline(tokenStream, token, delimiter)) {
        token.erase(0, token.find_first_not_of(" \t\r\n"));
        token.erase(token.find_last_not_of(" \t\r\n") + 1);
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

float GetFreeVRAM_MB() {
    AOS::VRAMInfo info = AOS::GetVRAMUsage();
    if (info.valid) {
        float freeMB = (float)(info.budget - info.usage) / 1024.f / 1024.f;
        return freeMB;
    }
    return 8192.0f; 
}

float GetFreeRAM_MB() {
    uint64_t freeBytes = AOS::GetFreeSystemRAM();
    return (float)freeBytes / 1024.f / 1024.f;
}


