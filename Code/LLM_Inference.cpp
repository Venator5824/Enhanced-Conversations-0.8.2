#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
// ECMain.cpp
#include "AbstractCalls.h"
#include "LLM_Inference.h"
#include "main.h" 
#include "whisper.h"
#include "whisper-arch.h"
#include <algorithm> 
#include <cstring>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <mutex>

using namespace AbstractGame;
using namespace AbstractTypes;

ModSettings g_ModSettings;

// ------------------------------------------------------------
// GLOBAL STATE
// ------------------------------------------------------------
llama_sampler* SamplerChain_New(const llama_vocab* vocab, uint32_t seed);
// LLM Globals
llama_model* g_model = nullptr;
llama_context* g_ctx = nullptr;
InferenceState g_llm_state = InferenceState::IDLE;
std::future<std::string> g_llm_future;
std::string g_llm_response = "";
std::chrono::high_resolution_clock::time_point g_response_start_time;
static int32_t g_repeat_last_n = 512;

// [FIX] Global Mutex to prevent Thread Collision (Crashes)
static std::mutex g_inference_mutex;
std::string LOG_FILE_NAME3 = "kkamel_inf.log";
// Metrics
float g_current_tps = 20.0f;

// LoRA
struct llama_adapter_lora* g_lora_adapter = nullptr;

// STT Globals
struct whisper_context* g_whisper_ctx = nullptr;
std::vector<float> g_audio_buffer;
ma_device g_capture_device;
bool g_is_recording = false;
std::future<std::string> g_stt_future;

// Memory Monitoring
static size_t g_memoryAllocations = 0;
static size_t g_memoryFrees = 0;

// ------------------------------------------------------------
// LOGGING HELPERS
// ------------------------------------------------------------



static void LogA(const std::string& msg) {
    std::ofstream f("kkamel_audio2.log", std::ios::app);
    if (f) {
        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);
        f << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] " << msg << "\n";
    }
}

void LogMemoryStats() {
    
        LogLLM("Memory Stats - Allocations: " + std::to_string(g_memoryAllocations) +
            ", Frees: " + std::to_string(g_memoryFrees) +
            ", Net: " + std::to_string(g_memoryAllocations - g_memoryFrees));
    
}

static void LogM(const std::string& message) {
    std::ofstream log(LOG_FILE_NAME3, std::ios_base::app);
    if (log.is_open()) {
        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);
        log << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] [ModMain] " << message << "\n";
        log.flush();
    }
}

static void LlamaLogCallback(ggml_log_level level, const char* text, void* user_data) {
    std::string logText = text;
    if (!logText.empty() && logText.back() == '\n') {
        logText.pop_back();
    }
    LogLLM("LLAMA_INTERNAL: " + logText);
}

void LogLLM(const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::ofstream log("kkamel.log", std::ios_base::app);
    if (log.is_open()) {
        log << "[" << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") << "] [LLM_Inference] " << message << "\n";
        log.flush();
    }
}

// ------------------------------------------------------------
// STATIC DATA (Names, Weather)
// ------------------------------------------------------------

const std::vector<std::string> MALE_FIRST_NAMES = {
    "James", "John", "Robert", "Michael", "William", "David", "Richard", "Joseph", "Thomas", "Charles",
    "Chris", "Daniel", "Matthew", "Anthony", "Mark", "Don", "Paul", "Steve", "Andy", "Ken",
    "Luis", "Carlos", "Juan", "Miguel", "Jose", "Hector", "Javier", "Ricardo", "Manny", "Chico"
};
const std::vector<std::string> FEMALE_FIRST_NAMES = {
    "Mary", "Patricia", "Jennifer", "Linda", "Elizabeth", "Barbara", "Susan", "Jessica", "Sarah", "Karen",
    "Nancy", "Lisa", "Betty", "Margaret", "Sandra", "Ashley", "Kim", "Emily", "Donna", "Michelle",
    "Maria", "Carmen", "Rosa", "Isabella", "Sofia", "Camila", "Valeria", "Lucia", "Ximena"
};
const std::vector<std::string> LAST_NAMES = {
    "Smith", "Johnson", "Williams", "Brown", "Jones", "Garcia", "Miller", "Davis", "Rodriguez", "Martinez",
    "Hernandez", "Lopez", "Gonzalez", "Wilson", "Anderson", "Thomas", "Taylor", "Moore", "Jackson", "Martin",
    "Lee", "Perez", "Thompson", "White", "Harris", "Sanchez", "Clark", "Ramirez", "Lewis", "Robinson"
};

// Map hash (int32 representation) to string
static std::map<uint32_t, std::string> g_WeatherMap = {
    {669657108, "BLIZZARD"}, {916995460, "CLEAR"}, {1840358669, "CLEARING"},
    {821931868, "CLOUDS"}, {-1750463879, "EXTRASUNNY"}, {-1368164796, "FOGGY"},
    {-921030142, "HALLOWEEN"}, {-1148613331, "OVERCAST"}, {1420204096, "RAIN"},
    {282916021, "SMOG"}, {603685163, "SNOWLIGHT"}, {-1233681761, "THUNDER"},
    {-1429616491, "XMAS"}
};

// ------------------------------------------------------------
// HELPER FUNCTIONS
// ------------------------------------------------------------

std::string GetCurrentWeatherState() {
    uint32_t currentHash = AbstractGame::GetCurrentWeatherType();
    std::string weatherStr = "UNKNOWN";
    int signedHash = static_cast<int>(currentHash);
    for (const auto& pair : g_WeatherMap) {
        if (pair.first == signedHash) {
            weatherStr = pair.second;
            break;
        }
    }
    return "Current Weather: " + weatherStr + ".";
}

std::string GetCurrentTimeState() {
    int hours, minutes;
    AbstractGame::GetGameTime(hours, minutes);
    std::string timeOfDay = "Day";
    if (hours >= 20 || hours < 6) timeOfDay = "Night";
    else if (hours >= 6 && hours < 10) timeOfDay = "Morning";
    else if (hours >= 17 && hours < 20) timeOfDay = "Evening";
    std::stringstream ss;
    ss << hours << ":" << (minutes < 10 ? "0" : "") << minutes << " (" << timeOfDay << ")";
    return ss.str();
}

std::string GenerateNpcName(const NpcPersona& persona) {
    if (!persona.inGameName.empty()) {
        return persona.inGameName;
    }
    int rand_idx = 0;
    if (persona.relationshipGroup == "Gang") {
        if (persona.gender == "M" && !MALE_FIRST_NAMES.empty()) {
            rand_idx = rand() % MALE_FIRST_NAMES.size();
            return MALE_FIRST_NAMES[rand_idx];
        }
        else if (persona.gender == "F" && !FEMALE_FIRST_NAMES.empty()) {
            rand_idx = rand() % FEMALE_FIRST_NAMES.size();
            return FEMALE_FIRST_NAMES[rand_idx];
        }
    }
    if (persona.relationshipGroup == "Law" && !LAST_NAMES.empty()) {
        rand_idx = rand() % LAST_NAMES.size();
        return "Officer " + LAST_NAMES[rand_idx];
    }
    if (persona.gender == "M" && !MALE_FIRST_NAMES.empty() && !LAST_NAMES.empty()) {
        int first_idx = rand() % MALE_FIRST_NAMES.size();
        int last_idx = rand() % LAST_NAMES.size();
        return MALE_FIRST_NAMES[first_idx] + " " + LAST_NAMES[last_idx];
    }
    else if (persona.gender == "F" && !FEMALE_FIRST_NAMES.empty() && !LAST_NAMES.empty()) {
        int first_idx = rand() % FEMALE_FIRST_NAMES.size();
        int last_idx = rand() % LAST_NAMES.size();
        return FEMALE_FIRST_NAMES[first_idx] + " " + LAST_NAMES[last_idx];
    }
    return persona.modelName;
}

void UpdateRepeatPenaltyFromConfig() {
    int estimated_tokens = ConfigReader::g_Settings.MaxHistoryTokens / 2;
    estimated_tokens = std::max(128, std::min(1024, estimated_tokens));

    g_repeat_last_n = static_cast<int32_t>(estimated_tokens);

    LogLLM("UpdateRepeatPenaltyFromConfig: repeat_last_n set to " + std::to_string(g_repeat_last_n) +
        " (based on MaxHistoryTokens = " + std::to_string(ConfigReader::g_Settings.MaxHistoryTokens) + ")");
}

// In LLM_Inference.cpp

#include "LLM_Inference.h"
#include "EntityRegistry.h"
#include "ConfigReader.h"
#include <sstream>
#include <set>
#include <algorithm>

// IMPORT GLOBAL VARIABLES
extern ConversationCache g_ConvoCache; // <--- We use the global cache!
extern AHandle g_target_ped;           // Access global target handle if needed



std::string AssemblePrompt(AHandle targetPed, AHandle playerPed, const std::vector<std::string>& chatHistory) {

    // 1. Safety Checks
    if (!g_model || !g_ctx) {
        LogLLM("AssemblePrompt FATAL: g_model or g_ctx is null.");
        return "";
    }
    const llama_vocab* vocab = llama_model_get_vocab(g_model);
    if (!vocab) return "";

    std::stringstream basePromptStream;

    // 2. Cache & Daten laden (DEIN ORIGINALER CODE, angepasst auf globale Variable)
    // Wir nutzen hier g_ConvoCache statt dem Parameter "cache", weil Main.cpp den Cache global füllt.
    const NpcPersona& target = g_ConvoCache.npcPersona;
    const std::string& npcName = g_ConvoCache.npcName;
    const std::string& playerName = g_ConvoCache.playerName;

    // Dynamische Daten holen (wie in deinem Original)
    PersistID targetID = EntityRegistry::GetIDFromHandle(targetPed);
    EntityData targetData = EntityRegistry::GetData(targetID);

    // 3. SYSTEM PROMPT AUFBAU
    basePromptStream << "<|system|>\n";

    // Dynamic Goal Injection
    if (!targetData.dynamicGoal.empty()) {
        basePromptStream << "CURRENT OBJECTIVE: " << targetData.dynamicGoal << "\n";
    }

    basePromptStream << "You are a single human character interacting within the world of Grand Theft Auto V.\n";
    basePromptStream << "YOUR CHARACTER:\n";
    basePromptStream << "- Name: " << npcName << " \n";
    basePromptStream << "- Gender: " << target.gender << "\n";
    basePromptStream << "- Role: " << target.type << " / " << target.subGroup << "\n";
    basePromptStream << "- Traits: " << target.behaviorTraits << "\n";

    basePromptStream << "\nSCENARIO:\n";
    // basePromptStream << "- Time: " << GetCurrentTimeState() << "\n"; // (Einkommentieren wenn verfügbar)
    basePromptStream << "- Interacting with: " << playerName << " (Role: " << g_ConvoCache.playerPersona.type << " / " << g_ConvoCache.playerPersona.subGroup << ")\n";
    basePromptStream << "- Character Relationship: " << g_ConvoCache.characterRelationship << "\n";
    basePromptStream << "- Group Relationship: " << g_ConvoCache.groupRelationship << "\n";

    basePromptStream << "\nINSTRUCTIONS:\n";
    basePromptStream << "- Speak ONLY as " << npcName << ".\n";
    // ... deine restlichen Anweisungen ...
    basePromptStream << "Never Say that you are an fictional character, an AI, phi3, or similar. Never say you are in a fictional world.";

    // 4. COMPLEX CONTEXT INJECTION (DEIN KOMPLETTER ORIGINAL-CODE)
    // -----------------------------------------------------------
    std::stringstream injectedContext;
    std::set<std::string> alreadyInjectedSections;

    if (!targetData.customKnowledge.empty()) {
        injectedContext << "[PERSISTENT MEMORY]: " << targetData.customKnowledge << "\n";
    }

    for (const auto& pair : ConfigReader::g_KnowledgeDB) {
        if (pair.second.isAlwaysLoaded) {
            injectedContext << pair.second.content;
            alreadyInjectedSections.insert(pair.first);
        }
    }

    std::string lastPlayerMsg = "";
    if (!chatHistory.empty()) {
        for (auto it = chatHistory.rbegin(); it != chatHistory.rend(); ++it) {
            // Wir suchen nach dem User Tag oder nehmen einfach die letzte Zeile, wenn sie vom Spieler ist
            if (it->find("Player") != std::string::npos || it->find("<|user|>") != std::string::npos) {
                lastPlayerMsg = *it;
                break;
            }
        }
    }

    std::string normalizedPlayerInput = NormalizeString(lastPlayerMsg);

    if (!normalizedPlayerInput.empty()) {
        for (const auto& pair : ConfigReader::g_KnowledgeDB) {
            const auto& section = pair.second;
            if (section.isAlwaysLoaded || alreadyInjectedSections.count(pair.first)) continue;

            for (const std::string& keyword : section.keywords) {
                if (normalizedPlayerInput.find(keyword) != std::string::npos) {
                    // Hier ist deine komplexe Logik:
                    if (section.loadEntireSectionOnMatch) {
                        injectedContext << section.content;
                    }
                    else {
                        for (const auto& kvPair : section.keyValues) {
                            if (NormalizeString(kvPair.first) == keyword) {
                                injectedContext << kvPair.first << " = " << kvPair.second << "\n";
                                break;
                            }
                        }
                    }
                    alreadyInjectedSections.insert(pair.first);
                    goto next_section_label; // Dein originales goto bleibt erhalten!
                }
            }
        next_section_label:;
        }
    }

    // Zone Context
    AVec3 centre = GetEntityPosition(playerPed);
    std::string zoneName = AbstractGame::GetZoneName(centre);
    std::string zoneContext = ConfigReader::GetZoneContext(zoneName);
    if (!zoneContext.empty()) {
        injectedContext << zoneName << " = " << zoneContext << "\n";
    }

    std::string finalInjectedText = injectedContext.str();
    if (!finalInjectedText.empty()) {
        basePromptStream << "\n[ADDITIONAL CONTEXT]:\n" << finalInjectedText;
    }
    // -----------------------------------------------------------

    basePromptStream << "<|end|>\n";
    std::string static_prompt = basePromptStream.str();

    // 5. TOKEN BUDGETING (DEIN KOMPLETTER ORIGINAL-CODE)
    // -----------------------------------------------------------
    const int32_t n_ctx = llama_n_ctx(g_ctx);
    const int32_t response_buffer = 256;

    std::vector<llama_token> static_tokens(n_ctx);
    int32_t static_token_count = llama_tokenize(vocab, static_prompt.c_str(), static_prompt.length(), static_tokens.data(), static_tokens.size(), false, false);

    int32_t history_token_budget = n_ctx - static_token_count - response_buffer;

    // Config Limit checken
    int maxHistTokens = ConfigReader::g_Settings.MaxHistoryTokens;
    if (maxHistTokens > 0) {
        history_token_budget = std::min(history_token_budget, static_cast<int32_t>(maxHistTokens));
    }

    std::vector<std::string> selected_history;
    int32_t history_tokens_used = 0;

    if (history_token_budget > 0 && !chatHistory.empty()) {
        std::vector<llama_token> msg_tokens(n_ctx);
        for (auto it = chatHistory.rbegin(); it != chatHistory.rend(); ++it) {
            const std::string& message = *it;
            int32_t msg_token_count = llama_tokenize(vocab, message.c_str(), message.length(), msg_tokens.data(), msg_tokens.size(), false, false);

            if (history_tokens_used + msg_token_count <= history_token_budget) {
                selected_history.insert(selected_history.begin(), message);
                history_tokens_used += msg_token_count;
            }
            else {
                // Budget voll
                break;
            }
        }
    }
    // -----------------------------------------------------------

    // 6. FINAL ASSEMBLY
    std::stringstream finalPromptStream;
    finalPromptStream << static_prompt;

    if (!selected_history.empty()) {
        finalPromptStream << "\nCHAT HISTORY:\n";
        for (const std::string& line : selected_history) {
            // Wir lassen die History im Wesentlichen wie sie ist, um keine Formatierung zu verlieren
            finalPromptStream << line << "\n";
        }
    }

    finalPromptStream << "\n<|assistant|>\n";
    return finalPromptStream.str();
}


/**
bool InitializeLLM(const char* model_path) {
    LogLLM("InitializeLLM: Starting...");

    // GPU Forcing (bleibt am Anfang)
    if (ConfigReader::g_Settings.FORCE_GPU_INDEX >= 0) {
        std::string gpu_id = std::to_string(ConfigReader::g_Settings.FORCE_GPU_INDEX);
        SetEnvironmentVariableA("GGML_VK_VISIBLE_DEVICES", gpu_id.c_str());
        LogLLM("VULKAN: Forcing GPU ID to " + gpu_id);
    }

    // Setze die allgemeinen, unkritischen Flags immer
    SetEnvironmentVariableA("GGML_VULKAN_MEMORY_DEBUG", "1");
    std::string vram_budget_str = std::to_string(ConfigReader::g_Settings.VRAM_BUDGET_MB);
    SetEnvironmentVariableA("GGML_VK_MAX_MEMORY_MB", vram_budget_str.c_str());

    // Backend initialisieren
    llama_log_set(LlamaLogCallback, nullptr);
    llama_backend_init();

    // Model-Parameter vorbereiten
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = ConfigReader::g_Settings.USE_GPU_LAYERS;

    // Deine if-Struktur, gefüllt mit den V0.6.0 Einstellungen
    if (ConfigReader::g_Settings.USE_VRAM_PREFERED != 0) {
        // --- TURBO MODE (V0.6.0 Replica) ---
        LogLLM(">>> VRAM PREFERED MODE (V0.6.0 Replica) ACTIVE <<<");
        SetEnvironmentVariableA("GGML_VK_FORCE_HEAP_INDEX", "0");
        SetEnvironmentVariableA("GGML_VK_FORCE_HOST_MEMORY", "1"); // Der "magische" Wert aus V0.6.0
        model_params.use_mmap = false;
        model_params.split_mode = LLAMA_SPLIT_MODE_NONE;
    }
    else {
        // --- NORMAL MODE ---
        LogLLM(">>> NORMAL MODE ACTIVE <<<");
        SetEnvironmentVariableA("GGML_VK_FORCE_HOST_MEMORY", "0");
        model_params.use_mmap = true;
        model_params.split_mode = LLAMA_SPLIT_MODE_NONE;
    }

    // Modell laden
    g_model = llama_model_load_from_file(model_path, model_params);
    if (g_model == nullptr) {
        LogLLM("FATAL: llama_model_load_from_file failed.");
        llama_backend_free();
        return false;
    }
    LogLLM("Model loaded successfully.");

    // i change here: Kontext-Erstellung ist jetzt HIER, direkt im Anschluss.
    LogLLM("Creating LLM context...");
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = ConfigReader::g_Settings.Max_Working_Input;
    ctx_params.n_batch = ConfigReader::g_Settings.n_batch;
    ctx_params.n_ubatch = ConfigReader::g_Settings.n_ubatch; // Wichtig für neue Versionen

    // KV Cache Quantisierung
    enum ggml_type kv_type = GGML_TYPE_F16;
    if (ConfigReader::g_Settings.Allow_KV_Cache_Quantization_Type == 1) {
        switch (ConfigReader::g_Settings.KV_Cache_Quantization_Type) {
        case 8:  kv_type = GGML_TYPE_Q8_0; break;
        case 4:  kv_type = GGML_TYPE_Q4_K; break;
        case 16: kv_type = GGML_TYPE_F16;  break;
        default: kv_type = GGML_TYPE_F16;  break;
        }
    }
    ctx_params.type_k = kv_type;
    ctx_params.type_v = kv_type;
    LogLLM("KV Cache Type set to: " + std::to_string(kv_type));

    g_ctx = llama_init_from_model(g_model, ctx_params);
    if (g_ctx == nullptr) {
        LogLLM("FATAL: llama_init_from_model failed. Check VRAM/Context/Batch settings.");
        llama_model_free(g_model);
        llama_backend_free();
        return false;
    }
    LogLLM("Context created successfully.");

    // i change here: LoRA Logik ist jetzt HIER, nach der Kontext-Erstellung.
    if (ConfigReader::g_Settings.Lora_Enabled) {
        std::string lora_file_path = FindLoRAFile(GetModRootPath());
        if (!lora_file_path.empty()) {
            float loraScale = ConfigReader::g_Settings.LORA_SCALE;
            Log("LoRA: Attempting to load adapter: " + lora_file_path);
            g_lora_adapter = llama_adapter_lora_init(g_model, lora_file_path.c_str());
            if (g_lora_adapter != nullptr) {
                if (llama_set_adapter_lora(g_ctx, g_lora_adapter, loraScale) == 0) {
                    Log("LoRA: Adapter loaded and applied successfully with scale " + std::to_string(loraScale));
                }
                else {
                    Log("LoRA: ERROR: Failed to apply adapter.");
                    llama_adapter_lora_free(g_lora_adapter);
                    g_lora_adapter = nullptr;
                }
            }
            else {
                Log("LoRA: ERROR: Failed to load adapter file.");
            }
        }
    }

    return true;
}


**/
bool InitializeLLM(const char* model_path) {
    LogLLM("InitializeLLM called with model_path: " + std::string(model_path));

    if (g_model != nullptr) {
        LogLLM("InitializeLLM: Model already initialized, returning true");
        return true;
    }

    // Random Seed setup
    srand(static_cast<unsigned int>(std::time(nullptr)));
    LogLLM("InitializeLLM: Random seed initialized.");

    // Redirect logs
    llama_log_set(LlamaLogCallback, nullptr);

    LogLLM("InitializeLLM: Initializing backend");
    llama_backend_init();

    llama_model_params model_params = llama_model_default_params();

    // --- GPU LAYER LOGIC FIXED ---
    int requested_layers = ConfigReader::g_Settings.USE_GPU_LAYERS;

    // Logic: 
    // -1 = Offload EVERYTHING (Best for speed if VRAM implies)
    // 0  = CPU Only
    // >0 = Partial offload
    if (requested_layers == 0) {
        model_params.n_gpu_layers = 0;
        LogLLM("InitializeLLM: Forced CPU mode (Settings set to 0)");
    }
    else {
        // We accept -1 or any positive number. 
        // We removed the "> 33" check because modern models have more layers.
        model_params.n_gpu_layers = requested_layers;

        if (requested_layers == -1)
            LogLLM("InitializeLLM: FULL GPU MODE requested (-1)");
        else
            LogLLM("InitializeLLM: Partial GPU mode: " + std::to_string(requested_layers) + " layers");
    }

    LogLLM("InitializeLLM: Loading model from " + std::string(model_path));

    // --- VULKAN ENVIRONMENT VARIABLES FIXED ---
 
    // 1. Force Heap 0 (Main VRAM usually)
    SetEnvironmentVariableA("GGML_VK_FORCE_HEAP_INDEX", "0");

    // 2. Max Memory: FIXED conversion from number to String
    std::string vramStr = std::to_string(ConfigReader::g_Settings.VRAM_BUDGET_MB);
    SetEnvironmentVariableA("GGML_VK_MAX_MEMORY_MB", vramStr.c_str());

    // 3. Force Host Memory: Important for Vulkan stability on Windows
    SetEnvironmentVariableA("GGML_VK_FORCE_HOST_MEMORY", "1");

    // 4. Debugging (Optional, can be removed for release)
    SetEnvironmentVariableA("GGML_VULKAN_MEMORY_DEBUG", "1");

    // Load the model
    g_model = llama_model_load_from_file(model_path, model_params);

    if (g_model == nullptr) {
        LogLLM("InitializeLLM: FATAL - Failed to load model. Check path or AVX support.");
        llama_backend_free();
        return false;
    }

    LogLLM("InitializeLLM: Model loaded successfully.");
    g_memoryAllocations++;
    LogMemoryStats();

    return true;
}

std::string TokenToPiece(const llama_vocab* vocab, llama_token token) {
    char buf[256];
    int n = llama_token_to_piece(vocab, token, buf, sizeof(buf), 0, true);
    if (n < 0) return "";
    return std::string(buf, n);
}


void ShutdownLLM() {
    LogLLM("ShutdownLLM called");
    if (g_llm_state == InferenceState::RUNNING) {
        if (g_llm_future.valid()) {
            try {
                g_llm_future.wait();
                g_llm_future.get();
            }
            catch (...) {
                LogLLM("ShutdownLLM: Ignored exception during future cleanup");
            }
        }
    }
    if (g_ctx != nullptr) {
        LogLLM("ShutdownLLM: Freeing context");
        llama_free(g_ctx);
        g_ctx = nullptr;
        g_memoryFrees++;
    }
    if (g_model != nullptr) {
        LogLLM("ShutdownLLM: Freeing model");
        llama_model_free(g_model);
        g_model = nullptr;
        g_memoryFrees++;
    }
    LogLLM("ShutdownLLM: Freeing backend");
    llama_backend_free();
    //g_chat_history.clear();
    //g_chat_history.shrink_to_fit();
    LogLLM("ShutdownLLM completed");
    LogMemoryStats();
    ShutdownBadassLogging();
}



std::string CleanupResponse(std::string text) {
    // Logging Helper (optional, aber nützlich)
    LogLLM("CleanupResponse: Original text (first 400): " + text.substr(0, (std::min)((size_t)400, text.length())));

    // ------------------------------------------------------------
    // 1. DYNAMISCHE STOP-TOKEN LISTE ERSTELLEN
    // ------------------------------------------------------------

    
    std::vector<std::string> stop_tokens = ConfigReader::SplitString(
        ConfigReader::g_Settings.StopStrings,
        ','
    );

    
    stop_tokens.push_back("<|endoftext|>");
    stop_tokens.push_back("<|end|>");
    stop_tokens.push_back("\n<|user|>");
    stop_tokens.push_back("<|assistant|>");

    
    if (!g_current_npc_name.empty()) {
        stop_tokens.push_back(g_current_npc_name + ":");
    }

    // ------------------------------------------------------------
    // 2. TEXT NACH ERSTEM STOP-TOKEN ABSCHNEIDEN (PRUNING)
    // ------------------------------------------------------------

    size_t earliest_stop_pos = std::string::npos;
    for (const std::string& token : stop_tokens) {
        size_t pos = text.find(token);
        if (pos != std::string::npos) {
            
            if (pos < earliest_stop_pos || earliest_stop_pos == std::string::npos) {
                earliest_stop_pos = pos;
            }
        }
    }

    if (earliest_stop_pos != std::string::npos) {
        text = text.substr(0, earliest_stop_pos);
        LogLLM("CleanupResponse: Truncated at final stop token string.");
    }

    // ------------------------------------------------------------
    // 3. FINALE BEREINIGUNG (Prefix und Whitespace)
    // ------------------------------------------------------------

    
    size_t firstColonPos = text.find(": ");
    if (firstColonPos != std::string::npos && !g_current_npc_name.empty()) {
        std::string generatedPrefix = text.substr(0, firstColonPos);
        if (g_current_npc_name.rfind(generatedPrefix, 0) == 0 || generatedPrefix.length() < 15) {
            text = text.substr(firstColonPos + 2);
            LogLLM("CleanupResponse: Stripped redundant name prefix.");
        }
    }


    // Trimmt Whitespace von Anfang und Ende (Standard C++ Logik)
    size_t start = text.find_first_not_of(" \t\n\r");
    text = (start == std::string::npos) ? "" : text.substr(start);
    size_t end = text.find_last_not_of(" \t\n\r");
    text = (end == std::string::npos) ? "" : text.substr(0, end + 1);

    LogLLM("CleanupResponse: Final clean text: " + text);
    return text;
}

extern ConvoState g_convo_state;



// for testing only will be removed later
static void LogGen(const std::string& msg) {
    std::ofstream f("kkamel_generate.log", std::ios::app);
    if (f) {
        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);
        f << "[" << std::put_time(&tm, "%H:%M:%S") << "] " << msg << std::endl;
    }
}
// ------------------------------------------------------------
// MANUAL SAMPLER IMPLEMENTATION
// ------------------------------------------------------------

struct TokenProb {
    int id;
    float val;
};

// Static buffer to avoid reallocation
static std::vector<TokenProb> s_candidates;

// In LLM_Inference.cpp

llama_token ManualSample(float* logits, int32_t n_vocab, const std::vector<llama_token>& history,
    float temp, float top_p, int top_k, float min_p, float rep_penalty)
{
    // 1. Nur einmal Speicher holen
    if (s_candidates.size() < (size_t)n_vocab) s_candidates.resize(n_vocab);

    // 2. Schnelles Kopieren (Pointer statt Index)
    TokenProb* pCand = s_candidates.data();
    for (int i = 0; i < n_vocab; ++i) {
        pCand[i].id = i;
        pCand[i].val = logits[i];
    }

    // 3. Repetition Penalty (Nur anwenden wenn nötig)
    if (rep_penalty > 1.001f && !history.empty()) {
        for (const auto& h : history) {
            if (h >= 0 && h < n_vocab) {
                float& val = s_candidates[h].val;
                val = (val > 0.0f) ? (val / rep_penalty) : (val * rep_penalty);
            }
        }
    }

    // 4. Greedy Shortcut (Sofort das beste nehmen, wenn Temp <= 0)
    if (temp <= 0.0f) {
        int max_id = 0;
        float max_val = s_candidates[0].val;
        for (int i = 1; i < n_vocab; ++i) {
            if (s_candidates[i].val > max_val) {
                max_val = s_candidates[i].val;
                max_id = i;
            }
        }
        return s_candidates[max_id].id;
    }


    int k_search = (top_k <= 0) ? 50 : std::min(top_k, 100);

    std::partial_sort(s_candidates.begin(), s_candidates.begin() + k_search, s_candidates.end(),
        [](const TokenProb& a, const TokenProb& b) { return a.val > b.val; });

    // Softmax nur auf die Top K anwenden
    float max_logit = s_candidates[0].val;
    float sum = 0.0f;
    for (int i = 0; i < k_search; ++i) {
        float p = expf((s_candidates[i].val - max_logit) / temp);
        s_candidates[i].val = p;
        sum += p;
    }

    // Würfeln
    float r = ((float)rand() / RAND_MAX) * sum;
    float acc = 0.0f;
    for (int i = 0; i < k_search; ++i) {
        acc += s_candidates[i].val;
        if (r <= acc) return s_candidates[i].id;
    }

    return s_candidates[0].id;
}

void LogHardWareStats() {
    //void
}



std::string TokensToString(const std::vector<llama_token>& tokens, llama_context* ctx) {
    std::string text = "";
    const llama_vocab* vocab = llama_model_get_vocab(llama_get_model(ctx));
    for (llama_token token_id : tokens) {
        char buf[128];
        int n = llama_token_to_piece(vocab, token_id, buf, sizeof(buf), 0, true);
        if (n > 0) {
            text.append(buf, n);
        }
    }
    return text;
}


std::string GenerateLLMResponse(std::string fullPrompt, bool slowMode) {
    // Thread-Safety
    std::lock_guard<std::mutex> lock(g_inference_mutex);

    // Validation
    if (!g_model || !g_ctx) return "ERR_NO_CTX";

    const llama_vocab* vocab = llama_model_get_vocab(g_model);

    // -----------------------------------------------------------------------
    // 0. PREPARE STOP TOKENS (Speed & Anti-Hallucination)
    // -----------------------------------------------------------------------
    std::vector<std::string> stop_strs;

    // A) Load from INI (Your flexible list)
    std::vector<std::string> config_stops = ConfigReader::SplitString(ConfigReader::g_Settings.StopStrings, ',');
    for (const auto& s : config_stops) {
        // Trim whitespace
        std::string clean = s;
        clean.erase(0, clean.find_first_not_of(" "));
        clean.erase(clean.find_last_not_of(" ") + 1);
        if (!clean.empty()) stop_strs.push_back(clean);
    }

    // B) Hardcoded Safety Stops
    // <| catches <|end|>, <|user|>, <|assistant|> etc. all at once
    stop_strs.push_back("<|");
    stop_strs.push_back("User:");
    stop_strs.push_back("Player:");
    stop_strs.push_back("[END");

    // C) Dynamic Name (Prevent AI from writing script for the NPC)
    if (!g_current_npc_name.empty()) {
        stop_strs.push_back(g_current_npc_name + ":");
        stop_strs.push_back("\n" + g_current_npc_name + ":");
    }

    // -----------------------------------------------------------------------
    // 1. TOKENIZATION
    // -----------------------------------------------------------------------
    // Reserve some space for new tokens
    std::vector<llama_token> all_tokens(fullPrompt.length() + 100);
    int32_t n_all_tokens = llama_tokenize(vocab, fullPrompt.c_str(), (int32_t)fullPrompt.length(), all_tokens.data(), all_tokens.size(), true, false);

    if (n_all_tokens <= 0) return "ERROR: TOKENIZATION";
    all_tokens.resize(n_all_tokens);

    // -----------------------------------------------------------------------
    // 2. KV CACHE RESET (THE CRITICAL FIX)
    // -----------------------------------------------------------------------
    // We clear the memory for sequence 0 completely.
    // This prevents the "inconsistent sequence positions" error by ensuring a fresh start.
    llama_memory_t memory = llama_get_memory(g_ctx);
    llama_memory_seq_rm(memory, -1, 0, -1);

    int32_t n_past = 0; // Start fresh at 0
    int32_t n_new_tokens = n_all_tokens; // Process everything

    // -----------------------------------------------------------------------
    // 3. PREFILL PHASE (Process the Prompt)
    // -----------------------------------------------------------------------
    // Process in batches defined by n_batch
    int32_t n_batch = ConfigReader::g_Settings.n_batch;

    for (int32_t i = 0; i < n_new_tokens; i += n_batch) {
        int32_t n_eval = n_new_tokens - i;
        if (n_eval > n_batch) n_eval = n_batch;

        llama_batch batch = llama_batch_init(n_eval, 0, 1);
        batch.n_tokens = n_eval;

        for (int32_t j = 0; j < n_eval; j++) {
            batch.token[j] = all_tokens[i + j];
            batch.pos[j] = n_past + j;
            batch.n_seq_id[j] = 1;
            batch.seq_id[j][0] = 0; // Sequence 0
            batch.logits[j] = false;
        }

        // We only need logits for the very last token of the prompt to predict the next one
        if (i + n_eval == n_new_tokens) {
            batch.logits[n_eval - 1] = true;
        }

        if (llama_decode(g_ctx, batch) != 0) {
            llama_batch_free(batch);
            return "LLM_EVAL_ERROR_PREFILL";
        }

        n_past += n_eval;
        llama_batch_free(batch);
    }

    // -----------------------------------------------------------------------
    // 4. GENERATION LOOP
    // -----------------------------------------------------------------------
    auto start_time = std::chrono::high_resolution_clock::now();
    std::vector<llama_token> generated_tokens;
    generated_tokens.reserve(ConfigReader::g_Settings.MaxOutputChars);

    std::string current_response_text = "";
    int n_decode = 0;
    const int max_out = ConfigReader::g_Settings.MaxOutputChars;
    int32_t n_cur = n_all_tokens; // Not strictly needed for pos if using n_past, but good for tracking

    // Sampler Setup
    llama_sampler* sampler_chain = nullptr;
    if (ConfigReader::g_Settings.SAMPLER_TYPE == 2) {
        uint32_t seed = static_cast<uint32_t>(time(NULL));
        sampler_chain = SamplerChain_New(vocab, seed);
    }

    // History for Manual Sampler
    std::vector<llama_token> history;
    if (ConfigReader::g_Settings.SAMPLER_TYPE == 3) {
        // Use the last N tokens of the prompt as initial history
        int start_idx = std::max(0, (int)all_tokens.size() - g_repeat_last_n);
        for (size_t i = start_idx; i < all_tokens.size(); ++i) {
            history.push_back(all_tokens[i]);
        }
    }

    bool stop_triggered = false;

    // Reusable batch for single token generation
    llama_batch batch_gen = llama_batch_init(1, 0, 1);

    while (n_decode < max_out) {
        if (slowMode) std::this_thread::sleep_for(std::chrono::milliseconds(20));

        // Get Logits
        auto* logits = llama_get_logits_ith(g_ctx, -1); // -1 = Last token in batch
        int32_t n_vocab = llama_vocab_n_tokens(vocab);
        llama_token id = 0;

        // Sampling Logic
        switch (ConfigReader::g_Settings.SAMPLER_TYPE) {
        case 1: { // Greedy
            float max_logit = -FLT_MAX;
            for (int32_t i = 0; i < n_vocab; ++i) {
                if (logits[i] > max_logit) { max_logit = logits[i]; id = i; }
            }
            break;
        }
        case 3: { // Manual
            id = ManualSample(logits, n_vocab, history,
                ConfigReader::g_Settings.temp, ConfigReader::g_Settings.top_p,
                (int)ConfigReader::g_Settings.top_k, ConfigReader::g_Settings.min_p,
                ConfigReader::g_Settings.repeat_penalty);
            history.push_back(id);
            if (history.size() > (size_t)g_repeat_last_n) history.erase(history.begin());
            break;
        }
        case 2:
        default:
            id = llama_sampler_sample(sampler_chain, g_ctx, -1);
            llama_sampler_accept(sampler_chain, id);
            break;
        }

        // STOP CHECK 1: Model EOG (End of Generation)
        if (llama_vocab_is_eog(vocab, id)) break;

        // STOP CHECK 2: String Based (Real-time)
        std::string piece = TokenToPiece(vocab, id);
        current_response_text += piece;
        generated_tokens.push_back(id);

        for (const auto& stop : stop_strs) {
            // Check if stop string appears in the text generated so far
            if (current_response_text.find(stop) != std::string::npos) {
                stop_triggered = true;
                break;
            }
        }
        if (stop_triggered) break;

        // Decode Next Token
        // IMPORTANT: Reset batch properties for every token
        batch_gen.n_tokens = 1;
        batch_gen.token[0] = id;
        batch_gen.pos[0] = n_past; // Continue position
        batch_gen.n_seq_id[0] = 1;
        batch_gen.seq_id[0][0] = 0;
        batch_gen.logits[0] = true;

        if (llama_decode(g_ctx, batch_gen) != 0) {
            // Context full or error
            break;
        }

        n_decode++;
        n_past++; // Advance cursor
    }

    // -----------------------------------------------------------------------
    // 5. CLEANUP
    // -----------------------------------------------------------------------
    llama_batch_free(batch_gen);
    if (sampler_chain) llama_sampler_free(sampler_chain);

    std::string response_text = TokensToString(generated_tokens, g_ctx);

    double duration = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start_time).count();
    UpdateTPS(n_decode, duration);

    // Final cleanup (removes the stop token itself if it was partially generated)
    return CleanupResponse(response_text);
}


void audio_capture_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    if (g_is_recording && pInput != NULL) {
        const float* pInputF32 = (const float*)pInput;
        
        g_audio_buffer.insert(g_audio_buffer.end(), pInputF32, pInputF32 + frameCount);
    }
}
bool InitializeWhisper(const char* model_path) {
    LogA("InitializeWhisper: Attempting to load model at: " + std::string(model_path));
    struct whisper_context_params cparams = whisper_context_default_params();
    g_whisper_ctx = whisper_init_from_file_with_params(model_path, cparams);

    if (g_whisper_ctx == nullptr) {
        LogA("InitializeWhisper: FAILED to initialize whisper context.");
        return false;
    }
    LogA("InitializeWhisper: Model loaded successfully.");
    return true;
}

// 2. Initialize Microphone
bool InitializeAudioCaptureDevice() {
    ma_device_config deviceConfig;
    deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format = ma_format_f32; 
    deviceConfig.capture.channels = 1;             
    deviceConfig.sampleRate = WHISPER_SAMPLE_RATE; 
    deviceConfig.dataCallback = audio_capture_callback;
    deviceConfig.pUserData = NULL;

    if (ma_device_init(NULL, &deviceConfig, &g_capture_device) != MA_SUCCESS) {
        LogA("InitializeAudioCaptureDevice: FAILED to initialize audio capture device.");
        return false;
    }
    LogA("InitializeAudioCaptureDevice: Audio device initialized (16kHz, 1-ch, f32).");
    return true;
}

// 3. Start Recording
void StartAudioRecording() {
    LogA("StartAudioRecording: Starting audio stream...");
    g_audio_buffer.clear();
    g_is_recording = true;
    if (ma_device_start(&g_capture_device) != MA_SUCCESS) {
        LogA("StartAudioRecording: FAILED to start audio device.");
        g_is_recording = false;
    }
}

// 4. Stop Recording
void StopAudioRecording() {
    LogA("StopAudioRecording: Stopping audio stream.");
    g_is_recording = false;
    ma_device_stop(&g_capture_device);
    LogA("StopAudioRecording: Audio buffer size: " + std::to_string(g_audio_buffer.size()));
}

// 5. Transcribe (This runs in the async thread)
std::string TranscribeAudio(std::vector<float> pcm_data) {
    LogA("TranscribeAudio: Thread started. Processing " + std::to_string(pcm_data.size()) + " audio samples.");
    if (g_whisper_ctx == nullptr) {
        LogA("TranscribeAudio: FAILED - Whisper context is null.");
        return "";
    }
    if (pcm_data.empty()) {
        LogA("TranscribeAudio: FAILED - Audio buffer is empty.");
        return "";
    }

    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_progress = false;
    wparams.print_realtime = false;
    wparams.print_special = false;
    wparams.print_timestamps = false;
    wparams.language = "auto"; // Use "auto" or "de" if you use the multilingual model
    wparams.n_threads = 4;   // Keep it low to not lag the game

    if (whisper_full(g_whisper_ctx, wparams, pcm_data.data(), pcm_data.size()) != 0) {
        LogA("TranscribeAudio: FAILED - whisper_full failed to process audio.");
        return "";
    }

    int n_segments = whisper_full_n_segments(g_whisper_ctx);
    std::string transcribed_text = "";
    for (int i = 0; i < n_segments; ++i) {
        const char* text = whisper_full_get_segment_text(g_whisper_ctx, i);
        transcribed_text += text;
    }

    LogA("TranscribeAudio: Transcription complete: " + transcribed_text);
    return transcribed_text;

}

// 6. Shutdown STT
void ShutdownWhisper() {
    if (g_whisper_ctx) {
        whisper_free(g_whisper_ctx);
        g_whisper_ctx = nullptr;
        LogA("ShutdownWhisper: Whisper context freed.");
    }
}

// 7. Shutdown Audio
void ShutdownAudioDevice() {
    ma_device_uninit(&g_capture_device);
    LogA("ShutdownAudioDevice: Audio device uninitialized.");
}

// 8. update Token per seconds
void UpdateTPS(int tokensGenerated, double elapsedSeconds) {
    if (elapsedSeconds < 0.001 || tokensGenerated <= 0) return;

    float rawTPS = (float)tokensGenerated / (float)elapsedSeconds;

    // Ignoriere unlogische Spikes (>500 ist meist ein Messfehler)
    if (rawTPS > 500.0f) return;

    // Glättung (Smoothing)
    g_current_tps = (rawTPS * 0.7f) + (g_current_tps * 0.3f);

    // LOGGING: Das hat gefehlt!
    // Wir loggen nur, wenn auch wirklich was passiert ist (>1 Token)
    if (tokensGenerated > 1) {
        LogLLM("Perf: " + std::to_string(tokensGenerated) + " tokens in " +
            std::to_string(elapsedSeconds) + "s. TPS: " + std::to_string(g_current_tps));
    }
}


static std::ofstream g_tkd_file;

static void CustomLlamaLogger(ggml_log_level level, const char* text, void* user_data) {
    if (g_tkd_file.is_open()) {
        // Zeitstempel für präzises Performance-Tracking
        auto now = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000000;

        g_tkd_file << "[" << ms << "] ";

        switch (level) {
        case GGML_LOG_LEVEL_ERROR: g_tkd_file << "[ERR] "; break;
        case GGML_LOG_LEVEL_WARN:  g_tkd_file << "[WARN] "; break;
        case GGML_LOG_LEVEL_DEBUG: g_tkd_file << "[DBG] "; break;
        default:                   g_tkd_file << "[INF] "; break;
        }

        g_tkd_file << text;

        // Wir flashen nicht nach jeder Zeile (Performance!), 
        // aber llama.cpp schickt meistens eh ganze Zeilen inkl. \n
        if (text[strlen(text) - 1] == '\n') {
            g_tkd_file.flush();
        }
    }
}

void GetBadassLogging() {
    // 1. Datei öffnen
    g_tkd_file.open("tkd.log", std::ios::out | std::ios::trunc);

    if (g_tkd_file.is_open()) {
        Log("--- BADASS LOGGING INITIALIZED: CHECK tkd.log ---");
        g_tkd_file << "=== TKD BADASS LOG START ===\n";

        // 2. Vulkan Internals aktivieren
        _putenv("GGML_VULKAN_DEBUG=1");
        _putenv("GGML_VULKAN_PERF=1");
       
        // 3. Llama Logger umleiten
        llama_log_set(CustomLlamaLogger, nullptr);
    }
    else {
        Log("ERROR: Could not create tkd.log!");
    }
}

void ShutdownBadassLogging() {
    if (g_tkd_file.is_open()) {
        g_tkd_file << "=== TKD LOG END ===\n";
        g_tkd_file.close();
    }
}

llama_sampler* SamplerChain_New(const llama_vocab* vocab, uint32_t seed) {
    llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
    sparams.no_perf = true;

    llama_sampler* chain = llama_sampler_chain_init(sparams);

    llama_sampler_chain_add(chain, llama_sampler_init_penalties(
        g_repeat_last_n,
        static_cast<float>(ConfigReader::g_Settings.repeat_penalty),
        0.0f, 
        0.0f  
    ));
    llama_sampler_chain_add(chain, llama_sampler_init_temp(ConfigReader::g_Settings.temp));
    llama_sampler_chain_add(chain, llama_sampler_init_top_p(ConfigReader::g_Settings.top_p, 1));
    llama_sampler_chain_add(chain, llama_sampler_init_min_p(ConfigReader::g_Settings.min_p, 1));
    llama_sampler_chain_add(chain, llama_sampler_init_dist(seed));
        return chain;
}

///////////////
///---------///
///---EOF---///
///---------///
///////////////