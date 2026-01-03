
#include "main.h"

#include "AbstractCalls.h"
#include "AbstractTypes.h"
#include "AudioSystem.h"
#include "AudioManager.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <future>

#include "EntityRegistry.h"
#include "ConversationSystem.h"
#include "SharedData.h"
#include "LLM_Inference.h"
#include "SubtitleManager.h"


#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#undef max
#undef min

using namespace AbstractGame;
using namespace AbstractTypes;

// ------------------------------------------------------------
// GLOBAL VARIABLES
// ------------------------------------------------------------

ChatID g_current_chat_ID = 0;
bool g_isInitialized = false;
const float TTS_NOISE = 0.6f;
const float TTS_NOISE_W = 0.6f;
VoiceBridge* bridge = nullptr;

std::string g_renderText;
uint32_t g_renderEndTime = 0;
int checker_loaded = 1;


// Audio ID to sort the Dialoges
uint64_t g_nextDialogeId = 1;


void* g_gameHWND = nullptr;
bool g_wasFullscreen = false;


std::string LOG_FILE_NAME = "kkamel.log";
ConvoState g_convo_state = ConvoState::IDLE;
InputState g_input_state = InputState::IDLE;

AHandle g_target_ped = 0;
std::string g_current_npc_name;

std::vector<std::future<void>> g_backgroundTasks;
ConversationCache g_ConvoCache;
ULONGLONG g_stt_start_time = 0;
std::chrono::high_resolution_clock::time_point g_llm_start_time;

int g_current_task_type = 1;

// ------------------------------------------------------------
// METRICS & LOGGING
// ------------------------------------------------------------

void LogSystemMetrics(const std::string& ctx) {
    LogM("--- BENCHMARK [" + ctx + "] ---");


    uint64_t ramBytes = AOS::GetProcessRAMUsage();
    float ramMB = static_cast<float>(ramBytes) / 1024.f / 1024.f;
    LogM("RAM: " + std::to_string(ramMB) + " MB");


    AOS::VRAMInfo vram = AOS::GetVRAMUsage();
    if (vram.valid) {
        float usedMB = static_cast<float>(vram.usage) / 1024.f / 1024.f;
        float budgetMB = static_cast<float>(vram.budget) / 1024.f / 1024.f;
        LogM("VRAM: " + std::to_string(usedMB) + " / " + std::to_string(budgetMB) + " MB");
    }
    else {
        LogM("VRAM: Check failed or unsupported.");
    }

    LogM("--- END BENCHMARK ---");
}

// ------------------------------------------------------------
// LOGIC: VOICE & KEYS
// ------------------------------------------------------------

static bool g_keyStates[256] = { false };

bool IsKeyJustPressed(int vk) {
    if (vk <= 0 || vk >= 256) return false;


    bool pressed = AOS::IsKeyPressed(vk);

    bool just = pressed && !g_keyStates[vk];
    g_keyStates[vk] = pressed;
    return just;
}

std::string GetModRootPath() {
    // [FIX] Abstraction: Using AOS
    std::string exe = AOS::GetExecutablePath();
    if (!exe.empty()) {
        // Return folder path (strip filename)
        return exe.substr(0, exe.find_last_of("/\\")) + PATH_SEPARATOR;
    }
    return "";
}

bool DoesFileExist(const std::string& p) {
    std::ifstream f(p.c_str());
    return f.good();
}

void EndConversation() {
    Log("EndConversation() called.");

    if (g_is_recording) {
        StopAudioRecording();
        LogA("Forced audio stop.");
    }
    if (g_current_task_type == 1 || g_current_task_type == 2) {
        AbstractGame::ClearTasks(g_target_ped);
    }

    // --- NEW & CORRECTED LOGIC ---
    if (g_current_chat_ID != 0) {
        ChatID savedID = g_current_chat_ID;
        // Get the final, complete history from the manager.
        std::vector<std::string> historySnapshot = ConvoManager::GetChatHistory(savedID);
        std::string savedName = g_current_npc_name;

        ConvoManager::CloseConversation(savedID);
        Log("PERSISTENCE: Chat " + std::to_string(savedID) + " closed.");

        // Dispatch background summarization task if needed.
        if (ConfigReader::g_Settings.TrySummarizeChat && historySnapshot.size() > 4) {
            Log("Dispatching background summary task for " + savedName);
            g_backgroundTasks.push_back(std::async(std::launch::async, [savedID, historySnapshot, savedName]() {
                std::string summary = PerformChatSummarization(savedName, historySnapshot);
                if (!summary.empty() && summary.find("LLM_ERROR") == std::string::npos) {
                    ConvoManager::SetConversationSummary(savedID, summary);
                }
                }));
        }
    }

    // Reset all global state variables to clean up.
    g_target_ped = 0;
    g_current_chat_ID = 0;
    g_current_npc_name.clear();
    g_convo_state = ConvoState::IDLE;
    g_input_state = InputState::IDLE;
    g_llm_state = InferenceState::IDLE;
    g_renderText.clear();
    // setting conversation task back to 1, default
    g_current_task_type = 1;
    // Reset futures
    if (g_llm_future.valid()) g_llm_future = std::future<std::string>();
    if (g_stt_future.valid()) g_stt_future = std::future<std::string>();
}

bool IsGameInSafeMode() {
    if (!g_isInitialized || !ConfigReader::g_Settings.Enabled) return false;
    AHandle p = GetPlayerHandle();
    if (AbstractGame::IsGamePausedOrLoading()) return false;
    if (IsEntityInCombat(p)) return false;
    if (IsEntitySwimming(p) || IsEntityJumping(p)) return false;
    if (IsEntityInVehicle(p)) {
        AHandle v = GetVehicleOfEntity(p);
        if (IsEntityDriver(v, p)) return false;
    }
    if (g_llm_state != InferenceState::IDLE || g_input_state != InputState::IDLE || g_convo_state != ConvoState::IDLE) return false;
    return true;
}

void* FindGameWindowHandle() {
    const char* possibleTitles[] = {
        "Grand Theft Auto V",
        "Enhanced Conversations",
        "Rockstar Games"
    };

    for (const char* title : possibleTitles) {
        // [FIX] Abstraction: Using AOS
        void* hwnd = AOS::FindWindowByName("", title); // Classname empty, search by title
        if (hwnd != nullptr) {
            Log("Found game window handle with title: " + std::string(title));
            return hwnd;
        }
    }
    Log("FATAL: Could not find game window handle.");
    return nullptr;
}

void HandleFullscreenYield() {
    if (g_gameHWND == nullptr) return;

    // [FIX] Abstraction: Using AOS for System Metrics and Window Rect
    // We assume index 0 = Width, 1 = Height (Standard SM_CXSCREEN/CYSCREEN)
    int screenWidth = AOS::GetSystemMetric(0);
    int screenHeight = AOS::GetSystemMetric(1);

    AOS::Rect windowRect;
    if (!AOS::GetWindowPosition(g_gameHWND, windowRect)) return;

    bool isFullscreen = (windowRect.left == 0 &&
        windowRect.top == 0 &&
        windowRect.right == screenWidth &&
        windowRect.bottom == screenHeight);

    if (isFullscreen == g_wasFullscreen) {
        g_wasFullscreen = isFullscreen;
        return;
    }

    ULONGLONG yieldMs = 500;

    if (g_llm_state == InferenceState::RUNNING || g_convo_state == ConvoState::IN_CONVERSATION) {
        LogM("GPU Yield: LLM is BUSY. Blocking thread for " + std::to_string(yieldMs) + "ms for safety.");
        AbstractGame::SystemWait(static_cast<int>(yieldMs));
    }
    else {
        LogM("GPU Yield: IDLE state. Pausing script for " + std::to_string(yieldMs) + "ms.");
        AbstractGame::SystemWait(static_cast<int>(yieldMs));
    }

    g_wasFullscreen = isFullscreen;
}

GAME_API void ScriptMain() {
    try {
         if (!g_isInitialized) {
            Log("ScriptMain: Initialising...");
            auto t0 = std::chrono::high_resolution_clock::now();

            // [FIX] Abstraction: Using AOS (returns void* which is safe)
            g_gameHWND = FindGameWindowHandle();
            if (g_gameHWND == nullptr) {
                Log("FATAL: Cannot find window handle. Mod may crash on fullscreen change.");
            }
            GetBadassLogging();
            // 1. CONFIG
            try {
                ConfigReader::LoadAllConfigs();
                std::string rootPath = GetModRootPath();
                AudioManager::Initialize(rootPath);
                std::string g2pModelPath = rootPath + "ECMod\\AudioModels\\deep_phonemizer.onnx";
                if (!AudioSystem::Initialize(g2pModelPath, 22050)) {
                    Log("ERROR: AudioSystem (G2P engine) failed to initialize. TTS will not function.");
                    
                }
                Log("Configuration and Audio Subsystems initialized.");
            }
            catch (const std::exception& e) {
                Log("FATAL CONFIG: " + std::string(e.what()));
                g_Subtitles.ShowMessage("System", "Config Error: " + std::string(e.what()));
                TERMINATE(); return;
            }
            LogSystemMetrics("Baseline");
            
            bridge = new VoiceBridge(true);
            if (bridge && bridge->IsConnected()) {
                Log("Shared Memory Bridge initialized (Host Mode)");
            }
            else {
                Log("ERROR: Failed to init Shared Memory Bridge");
            }
            
            // 2. LLM (Phi-3)
            std::string root = GetModRootPath();
            std::string modelPath;
            const auto& cust = ConfigReader::g_Settings.MODEL_PATH;
            const auto& alt = ConfigReader::g_Settings.MODEL_ALT_NAME;
            const auto def = "Phi3.gguf";

            if (!cust.empty() && DoesFileExist(cust)) modelPath = cust;
            else if (!alt.empty() && DoesFileExist(root + alt)) modelPath = root + alt;
            else if (DoesFileExist(root + def)) modelPath = root + def;

            if (modelPath.empty()) {
                Log("FATAL: No LLM model found");
                TERMINATE(); return;
            }                                       
            Log("Using LLM: " + modelPath);
            if (!InitializeLLM(modelPath.c_str())) {                       
                Log("FATAL: InitializeLLM() failed");
                TERMINATE(); return;
            }
            AbstractGame::SystemWait(0);
            // LLM Context Setup
            enum ggml_type kv_type = GGML_TYPE_F16;
            llama_context_params ctx_params = llama_context_default_params();

            // Using Max_Working_Input as discussed in your config
            ctx_params.n_ctx = static_cast<uint32_t>(ConfigReader::g_Settings.Max_Working_Input);
            ctx_params.n_batch = static_cast<uint32_t>(ConfigReader::g_Settings.n_batch);
            ctx_params.n_ubatch = static_cast<uint32_t>(ConfigReader::g_Settings.n_ubatch);

            // Detailed Quantization Logging (Restored fully)
            if (ConfigReader::g_Settings.Allow_KV_Cache_Quantization_Type == 1) {

                switch (ConfigReader::g_Settings.KV_Cache_Quantization_Type)
                {
                case 2: // ~2.56 bits-per-weight
                    kv_type = GGML_TYPE_Q2_K;
                    Log("KV Cache Quantization: Using Q2_K (~2.56 bits)");
                    break;

                case 3: // ~3.43 bits-per-weight
                    kv_type = GGML_TYPE_Q3_K;
                    Log("KV Cache Quantization: Using Q3_K (~3.43 bits)");
                    break;

                case 4: // ~4.5 bits-per-weight
                    kv_type = GGML_TYPE_Q4_K;
                    Log("KV Cache Quantization: Using Q4_K (~4.5 bits)");
                    break;

                case 5: // ~5.5 bits-per-weight
                    kv_type = GGML_TYPE_Q5_K;
                    Log("KV Cache Quantization: Using Q5_K (~5.5 bits)");
                    break;

                case 6: // ~6.56 bits-per-weight
                    kv_type = GGML_TYPE_Q6_K;
                    Log("KV Cache Quantization: Using Q6_K (~6.56 bits)");
                    break;

                case 8: // 8.0 bits-per-weight
                    kv_type = GGML_TYPE_Q8_0;
                    Log("KV Cache Quantization: Using Q8_0 (8 bits)");
                    break;
                case 16:
                    kv_type = GGML_TYPE_F16;
                    Log("KV Cache Quantization: Using F16 (16 bits)");
                    break;


                default:
                    Log("KV Cache Quantization: Using default float type (F32/F16).");
                    break;
                }
            }

            ctx_params.type_k = kv_type;
            ctx_params.type_v = kv_type;
            AbstractGame::SystemWait(0);
            g_ctx = llama_init_from_model(g_model, ctx_params);
            if (g_ctx == nullptr) {
                Log("FATAL: llama_init_from_model failed. Cannot proceed with LLM context.");
                return;
            }
            AbstractGame::SystemWait(0);

            // LORA ADAPTER LOADING
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
                            Log("LoRA: ERROR: Failed to apply adapter. Reverting.");
                            llama_adapter_lora_free(g_lora_adapter);
                            g_lora_adapter = nullptr;
                        }
                    }
                    else {
                        Log("LoRA: ERROR: Failed to load adapter file.");
                    }
                }
            }
            
            // ------------------------------------------------------------
            // 3. WHISPER (STT) INITIALIZATION
            // ------------------------------------------------------------
            if (ConfigReader::g_Settings.StT_Enabled) {
                std::string sttPath;
                const auto& custSTT = ConfigReader::g_Settings.STT_MODEL_PATH;
                const auto& altSTT = ConfigReader::g_Settings.STT_MODEL_ALT_NAME;

                // Re-ensure root is valid
                if (root.empty()) root = GetModRootPath();

                if (!custSTT.empty() && DoesFileExist(custSTT)) sttPath = custSTT;
                else if (!altSTT.empty() && DoesFileExist(root + altSTT)) sttPath = root + altSTT;

                if (!sttPath.empty() && InitializeWhisper(sttPath.c_str()) && InitializeAudioCaptureDevice()) {
                    Log("Whisper + mic ready");
                }
                else {
                    Log("STT disabled - model or mic missing");
                    ConfigReader::g_Settings.StT_Enabled = false;
                }
            }
            else {
                Log("STT disabled in config");
            }
            
            // ------------------------------------------------------------
            // 4. TTS (TEXT TO SPEECH) CHECK
            // ------------------------------------------------------------
            
                if (ConfigReader::g_Settings.TtS_Enabled) {
                    Log("TTS: Feature enabled in INI. Checking models...");
                    if (root.empty()) root = GetModRootPath();
                    // Debug log to confirm path detection
                    if (!ConfigReader::g_Settings.TTS_MODEL_PATH.empty() && DoesFileExist(ConfigReader::g_Settings.TTS_MODEL_PATH)) {
                        Log("TTS: Custom model found.");
                    }AbstractGame::SystemWait(0);
                }
                else {
                    Log("TTS: Disabled in INI.");
                }
                Log("TTS Section finished ...");
                
            // ------------------------------------------------------------
            // FINALIZE INIT
            // ------------------------------------------------------------
            auto t1 = std::chrono::high_resolution_clock::now();
            LogM("INIT TIME: " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()) + " ms");
            LogSystemMetrics("Post-LLM");

            g_isInitialized = true;
            g_Subtitles.ShowMessage("System", "Enhanced Conversations Loaded");
        }

        // ----------------------------------------------------
        // MAIN GAME LOOP
        // ----------------------------------------------------
        while (true) {


            // Janitor Checking, Vulkan - DX safety net
            
            uint32_t now = (uint32_t)AOS::GetTimeMs();
            unsigned int u_janitor_check_timer_Ms = 7500; // 5 seconds to keep it good adn solid between answers. when idle its alright anyway
            unsigned int u_fullscreen_check_timer_Ms = 33; // the AI should never take more than 20 ms for responding, and 30fps rate for full screen check is enough
            static uint32_t last_janitor_run = 0;

            if (now > last_janitor_run + u_janitor_check_timer_Ms) {
                if (g_llm_state == InferenceState::IDLE) {
                    ConvoManager::RunMaintenance();
                    last_janitor_run = now;
                }
            }


            static uint32_t last_opt_check = 0;
            if (now > last_opt_check + 5000) {
                last_opt_check = now;
                if (g_current_chat_ID != 0 && g_llm_state == InferenceState::IDLE) {
                    std::vector<std::string> curHist = ConvoManager::GetChatHistory(g_current_chat_ID);
                    ChatOptimizer::CheckAndOptimize(g_current_chat_ID, curHist, g_current_npc_name, "Player");
                }
            }

            static uint32_t last_window_check = 0;
            if (now > last_window_check + u_fullscreen_check_timer_Ms) {
                HandleFullscreenYield();
                last_window_check = now;
            }

            if (g_current_chat_ID != 0 && g_llm_state == InferenceState::IDLE) {
                std::vector<std::string> curHist = ConvoManager::GetChatHistory(g_current_chat_ID);
                if (ChatOptimizer::ApplyPendingOptimizations(curHist)) {
                    ConvoManager::ReplaceHistory(g_current_chat_ID, curHist);
               
                }
            }



            // ----- 1. SUBTITLE RENDERING -----
            g_Subtitles.UpdateAndRender();


            AHandle playerPed = GetPlayerHandle();
            AVec3 playerPos = GetEntityPosition(playerPed);

            // ----- 2. CONVERSATION GUARD -----
            if (g_convo_state == ConvoState::IN_CONVERSATION) {

                // ==========================================
                // A. CHAT OPTIMIZER LOGIC (Manager Updated)
                // ==========================================
                if (g_current_chat_ID != 0) {
                    std::vector<std::string> currentHistory = ConvoManager::GetChatHistory(g_current_chat_ID);

                    // Apply optimizations if ready
                    if (ChatOptimizer::ApplyPendingOptimizations(currentHistory)) {
                        ConvoManager::ReplaceHistory(g_current_chat_ID, currentHistory);
                    }

                    // Trigger new check
                    static uint32_t last_opt_check = 0;
                    if (AOS::GetTimeMs() > last_opt_check + 5000) {
                        last_opt_check = (uint32_t)AOS::GetTimeMs();
                        ChatOptimizer::CheckAndOptimize(
                            g_current_chat_ID,
                            currentHistory,
                            g_current_npc_name,
                            "Player"
                        );
                    }
                }

                // ==========================================
                // B. STATUS CHECKS (Safety)
                // ==========================================
                if (!AbstractGame::IsEntityValid(g_target_ped) || AbstractGame::IsEntityDead(g_target_ped)) {
                    Log("Target dead/invalid -> end");
                    EndConversation();
                }
                else {
                    float dist = AbstractGame::GetDistanceBetweenEntities(playerPed, g_target_ped);
                    if (dist > ConfigReader::g_Settings.MaxConversationRadius * 1.5f) {
                        Log("Too far -> end");
                        EndConversation();
                    }
                }

                // Renew Tasks
                UpdateNpcConversationTasks(g_target_ped, playerPed);

                // ==========================================
                // C. STOP KEYS
                // ==========================================
                if (IsKeyJustPressed(ConfigReader::g_Settings.StopKey_Primary) ||
                    IsKeyJustPressed(ConfigReader::g_Settings.StopKey_Secondary)) {

                    // PTT Exception Check
                    bool isPtt = (ConfigReader::g_Settings.StopKey_Secondary == ConfigReader::g_Settings.StTRB_Activation_Key);

                    if (isPtt && AOS::IsKeyPressed(ConfigReader::g_Settings.StTRB_Activation_Key)) {
                        // User is holding PTT, do nothing
                    }
                    else {
                        Log("StopKey pressed -> end");
                        EndConversation();
                    }
                }
            }

            // ----- 3. LLM TIMEOUT CHECK -----
            if (g_llm_state == InferenceState::RUNNING) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - g_llm_start_time).count();
                if (elapsed > 30) {
                    Log("LLM Timeout -> discard");
                    if (g_llm_future.valid()) g_llm_future = std::future<std::string>();
                    g_llm_response = "LLM_TIMEOUT";
                    g_llm_state = InferenceState::COMPLETE;
                }
                else if (g_llm_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                    auto elapsed_delay = std::chrono::high_resolution_clock::now() - g_response_start_time;
                    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed_delay).count();

                    if (ms < ConfigReader::g_Settings.MinResponseDelayMs) {
                        AbstractGame::SystemWait(static_cast<uint32_t>(ConfigReader::g_Settings.MinResponseDelayMs - ms));
                    }
                    try { g_llm_response = g_llm_future.get(); }
                    catch (const std::exception& e) {
                        Log("LLM future exception: " + std::string(e.what()));
                        g_llm_response = "LLM_ERROR";
                    }
                    g_llm_future = std::future<std::string>();
                    g_llm_state = InferenceState::COMPLETE;
                }
            }

            // ----- 4. KEYBOARD INPUT -----
            if (g_input_state == InputState::WAITING_FOR_INPUT) {
                int kb = AbstractGame::UpdateKeyboardStatus();

                if (kb == 1) { // User pressed ENTER
                    std::string txt = AbstractGame::GetKeyboardResult();
                    if (!txt.empty()) {

                        ChatID activeID = ConvoManager::GetActiveChatID(playerPed);

                        if (activeID != 0) {
                            // Log User Message
                            ConvoManager::AddMessageToChat(activeID, "Player", txt);

                            // Update Context
                            std::string zone = AbstractGame::GetZoneName(playerPos);
                            ConvoManager::SetChatContext(activeID, zone, "Clear");

                            // Fetch History & Prompt
                            std::vector<std::string> history = ConvoManager::GetChatHistory(activeID);
                            std::string prompt = AssemblePrompt(g_target_ped, playerPed, history);

                            LogSystemMetrics("Pre-Inference (KB)");
                            g_response_start_time = std::chrono::high_resolution_clock::now();
                            g_llm_start_time = std::chrono::high_resolution_clock::now();

                            // Launch Async Generation
                            g_llm_future = std::async(std::launch::async, GenerateLLMResponse, prompt, false);
                            g_llm_state = InferenceState::RUNNING;
                            g_input_state = InputState::IDLE;
                        }
                        else {
                            Log("ERROR: Input received but no Active Chat ID found!");
                            EndConversation();
                        }
                    }
                    else {
                        // Re-open keyboard if empty
                        AbstractGame::OpenKeyboard("Talk", "", ConfigReader::g_Settings.MaxInputChars);
                    }
                }
                else if (kb == 2 || kb == 3) { // User Cancelled
                    Log("Keyboard cancelled");
                    EndConversation();
                }
            }

            // ----- 5. STT RECORDING -----
            if (g_input_state == InputState::RECORDING) {
                if (ConfigReader::g_Settings.StTRB_Activation_Key != 0 &&
                    !AOS::IsKeyPressed(ConfigReader::g_Settings.StTRB_Activation_Key)) {
                    LogA("PTT released -> stop");
                    StopAudioRecording();
                    g_stt_start_time = AOS::GetTimeMs();
                    g_input_state = InputState::TRANSCRIBING;
                    g_stt_future = std::async(std::launch::async, TranscribeAudio, g_audio_buffer);
                    g_Subtitles.ShowMessage("System", "Transcribing...");
                }
            }

            // ----- 6. STT TRANSCRIPTION DONE -----
            if (g_input_state == InputState::TRANSCRIBING) {
                // Check if finished
                if (g_stt_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                    std::string txt = g_stt_future.get();
                    g_stt_future = std::future<std::string>();

                    if (!txt.empty() && txt.length() > 2) {
                        if (g_current_chat_ID != 0) {
                            ConvoManager::AddMessageToChat(g_current_chat_ID, "Player", txt);
                            std::vector<std::string> history = ConvoManager::GetChatHistory(g_current_chat_ID);
                            std::string prompt = AssemblePrompt(g_target_ped, playerPed, history);

                            LogSystemMetrics("Pre-Inference (STT)");
                            g_response_start_time = std::chrono::high_resolution_clock::now();
                            g_llm_start_time = std::chrono::high_resolution_clock::now();
                            g_llm_future = std::async(std::launch::async, GenerateLLMResponse, prompt, false);
                            g_llm_state = InferenceState::RUNNING;
                        }
                    }
                    else {
                        Log("STT empty -> discard");
                    }
                    g_input_state = InputState::IDLE;
                }
                // Timeout check
                else if (AOS::GetTimeMs() > g_stt_start_time + 10000) {
                    Log("STT Timeout -> discard");
                    if (g_stt_future.valid()) g_stt_future = std::future<std::string>();
                    g_Subtitles.ShowMessage("System", "Transcription Timeout");
                    g_input_state = InputState::IDLE;
                }
            }

            // ----- 7. START CONVERSATION TRIGGER -----
            if (g_convo_state == ConvoState::IDLE && g_input_state == InputState::IDLE && g_llm_state == InferenceState::IDLE) {
                if (IsGameInSafeMode()) {
                    if (IsKeyJustPressed(ConfigReader::g_Settings.ActivationKey)) {

                        // 1. Find Target
                        AHandle tempTarget = 0;
                        AVec3 centre = AbstractGame::GetEntityPosition(playerPed);
                        tempTarget = AbstractGame::GetClosestPed(centre, ConfigReader::g_Settings.MaxConversationRadius, playerPed);

                        if (AbstractGame::IsEntityValid(tempTarget) && AbstractGame::IsEntityLivingEntity(tempTarget) && tempTarget != playerPed) {

                            // [NEW LOGIC] Check for "Hard Stoppers" (Denial Tasks)
                            // This checks: Combat, Shooting, Fleeing, Ragdoll, Arrests, Death
                            if (IsPedInDenialTask(tempTarget)) {
                                g_Subtitles.ShowMessage("System", "NPC is hostile or busy.");
                                // The loop continues, ignoring this NPC.
                            }
                            else {
                                Log("Conversation START -> AHandle " + std::to_string((uintptr_t)tempTarget));
                                g_target_ped = tempTarget;

                                // Cache Data
                                FillConversationCache(g_target_ped, playerPed);

                                // Create Session
                                g_current_chat_ID = ConvoManager::InitiateConversation(playerPed, g_target_ped);
                                Log("Started Chat ID: " + std::to_string(g_current_chat_ID) + " with " + g_current_npc_name);

                                // Set Status
                                g_convo_state = ConvoState::IN_CONVERSATION;

                                StartNpcConversationTasks(g_target_ped, playerPed, 1);

                                // Handle Input Method
                                if (ConfigReader::g_Settings.StT_Enabled) {
                                    g_Subtitles.ShowMessage("System", "Hold PTT to speak...");
                                    g_input_state = InputState::IDLE;
                                }
                                else {
                                    AbstractGame::OpenKeyboard("FMMC_KEY_TIP", "", ConfigReader::g_Settings.MaxInputChars);
                                    g_input_state = InputState::WAITING_FOR_INPUT;
                                }
                            }
                        }
                    }
                }
            }
            // PTT Trigger
            if (g_convo_state == ConvoState::IN_CONVERSATION &&
                g_input_state == InputState::IDLE &&
                g_llm_state == InferenceState::IDLE &&
                ConfigReader::g_Settings.StT_Enabled &&
                ConfigReader::g_Settings.StTRB_Activation_Key != 0 &&
                IsKeyJustPressed(ConfigReader::g_Settings.StTRB_Activation_Key))
            {
                LogA("PTT pressed -> start recording");
                StartAudioRecording();
                g_input_state = InputState::RECORDING;
                g_Subtitles.ShowMessage("System", "Recording...");
            }

            // ----- 8. LLM RESPONSE READY  -----
            if (g_llm_state == InferenceState::COMPLETE) {
                if (g_convo_state != ConvoState::IN_CONVERSATION) {
                    g_llm_state = InferenceState::IDLE;
                    continue;
                }

                // 1. Clean the text
                std::string clean = CleanupResponse(g_llm_response);
                if (g_llm_response.find("LLM_ERROR") != std::string::npos || clean.length() < 3) {
                    clean = "I can't talk right now.";
                }

                // 2. Decide if the conversation should end
                bool endConvo = (clean.find("Goodbye") != std::string::npos || clean.find("Bye") != std::string::npos);

                // 3. Display the text subtitle IMMEDIATELY
                g_Subtitles.ShowMessage(g_current_npc_name, clean);
                Log("RENDER: " + WordWrap(clean, 50));

                // 4. Save to chat history
                ChatID activeID = ConvoManager::GetActiveChatID(playerPed);
                if (activeID != 0) {
                    ConvoManager::AddMessageToChat(activeID, g_current_npc_name, clean);
                }

                // 5. Start the ENTIRE TTS process in a background thread

                if (ConfigReader::g_Settings.TtS_Enabled) {
                    Log("MAIN LOOP: Dispatching ASYNC audio task (Load + Generate)...");

                    // std::async startet eine neue Aufgabe im Hintergrund
                    g_backgroundTasks.push_back(std::async(std::launch::async, [=]() {
                        // ---- Dieser Codeblock läuft jetzt parallel und blockiert das Spiel NICHT ----

                        PersistID npcID = EntityRegistry::GetIDFromHandle(g_target_ped);
                        VoiceSettings vs = EntityRegistry::GetVoiceSettings(npcID);

                        if (vs.model.empty() || vs.model == "NONE") return;

                        if (AudioManager::LoadAudioModel(vs.model)) {

                            Vits::Session* session = AudioManager::GetSession(vs.model);
                            if (session) {
                                // Schritt B: Die blockierende Audio-Generierung (jetzt auch sicher)
                                std::vector<int16_t> pcmData = AudioSystem::Generate(clean, session, vs.id, vs.speed, TTS_NOISE, TTS_NOISE_W);

                                int finalRate = 22050; // Sicherer Standardwert

                                  g_Subtitles.ShowMessage(g_current_npc_name, clean, pcmData, finalRate);
                            }
                        }

                        }));
                }

                // 6. Decide the next input step
                if (endConvo) {
                    Log("LLM requested end. Closing session.");
                    EndConversation();
                }
                else {
                    if (ConfigReader::g_Settings.StT_Enabled) {
                        g_input_state = InputState::IDLE;
                        Log("LLM done -> STT idle");
                    }
                    else {
                        AbstractGame::OpenKeyboard("FMMC_KEY_TIP", "", ConfigReader::g_Settings.MaxInputChars);
                        g_input_state = InputState::WAITING_FOR_INPUT;
                    }
                }
                g_llm_state = InferenceState::IDLE;
            }

            AbstractGame::SystemWait(0);
        }
    }
    catch (const std::exception& e) {
        Log("SCRIPT EXCEPTION: " + std::string(e.what()));
        ShutdownLLM();
        AudioSystem::Shutdown();
        TERMINATE();
    }
    catch (...) {
        Log("UNKNOWN EXCEPTION");
        ShutdownLLM();
        AudioSystem::Shutdown();
        TERMINATE();
    }
}

void FinalShutdownHandler() {
    Log("--- FINAL SHUTDOWN HANDLER TRIGGERED ---");
    // We can't do complex logging here, but we can call our main shutdown logic
    if (g_isInitialized) {
        AudioManager::UnloadAllAudioModels();
        AudioSystem::Shutdown();
        ShutdownLLM();
    }
}

#ifdef PLATFORM_WINDOWS

BOOL APIENTRY DllMain(HMODULE hMod, uint32_t reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
    {
        // Clear log file on attach
        std::ofstream f(LOG_FILE_NAME, std::ios::trunc);
    }
    Log("DLL attached");
    // scriptRegister is from ScriptHookV, which is a Windows dependency
    scriptRegister(hMod, ScriptMain);
    std::atexit(FinalShutdownHandler); 
    break;

    case DLL_PROCESS_DETACH:
        Log("DLL detach – shutdown");
        if (g_isInitialized) {
            ShutdownLLM();
        }
        
        // Clean up bridge
        if (bridge) {
            delete bridge;
            bridge = nullptr;
        }
        
        scriptUnregister(hMod);
        break;
    }
    return true;
}
#endif 

std::string PerformChatSummarization(const std::string& npcName, const std::vector<std::string>& history) {
    if (!g_model || !g_ctx) return "";

    // ---------------------------------------------------------
    // 1. GATHER METADATA (C++ Side)
    // ---------------------------------------------------------
    // Wir sammeln die Daten jetzt, hängen sie aber erst GANZ AM ENDE an den String.
    std::string timeStr = GetCurrentTimeState();
    std::string weatherStr = GetCurrentWeatherState();

    AHandle playerPed = GetPlayerHandle();
    AVec3 pos = AbstractGame::GetEntityPosition(playerPed);
    std::string zoneName = AbstractGame::GetZoneName(pos);

    // Dieser String wird später angehängt
    std::string contextFooter = "\n[METADATA]: Location: " + zoneName + ", Time: " + timeStr + ", " + weatherStr;

    // ---------------------------------------------------------
    // 2. BUILD PROMPT (Generic for N+1 Speakers)
    // ---------------------------------------------------------
    std::stringstream prompt;
    prompt << "<|system|>\n";
    prompt << "You are an automated log archivist.\n";
    prompt << "TASK: Identify all distinct speakers in the transcript and summarize their key points.\n";
    prompt << "RULES:\n";
    prompt << "- Format as: '[Speaker Name]: [Concise Summary]'\n";
    prompt << "- Capture agreements, names, and tasks.\n";
    prompt << "- Ignore small talk.\n";
    prompt << "- If multiple people speak, list them separately.\n";
    prompt << "<|end|>\n";

    // ---------------------------------------------------------
    // 3. INJECT HISTORY
    // ---------------------------------------------------------
    prompt << "<|user|>\nTRANSCRIPT:\n";

    // Safety Limit (ca. 80 Zeilen)
    size_t startIdx = 0;
    if (history.size() > 80) {
        startIdx = history.size() - 80;
    }

    for (size_t i = startIdx; i < history.size(); ++i) {
        std::string line = history[i];
        // Sanitization
        if (line.find("<|") == std::string::npos) {
            prompt << line << "\n";
        }
    }
    prompt << "<|end|>\n";
    prompt << "<|assistant|>\n";

    // ---------------------------------------------------------
    // 4. EXECUTE & APPEND
    // ---------------------------------------------------------
    // LLM generiert die Zusammenfassung der Gespräche
    std::string llmSummary = GenerateLLMResponse(prompt.str(), true);

    // Wir hängen die harten Fakten (Zeit/Ort) manuell an. 
    // Das ist präziser und kostet keine Rechenleistung.
    return llmSummary + contextFooter;
}

void FillConversationCache(AHandle targetPed, AHandle playerPed) {
    Log("Caching conversation context...");

    PersistID npcPid = EntityRegistry::RegisterNPC(targetPed);

    g_ConvoCache.npcPersona = ConfigReader::GetPersona(targetPed);
    g_ConvoCache.playerPersona = ConfigReader::GetPersona(playerPed);

    if (!g_ConvoCache.npcPersona.inGameName.empty()) {
        g_ConvoCache.npcName = g_ConvoCache.npcPersona.inGameName;
        EntityRegistry::AssignEntityName(npcPid, g_ConvoCache.npcName);
    }
    else if (EntityRegistry::HasAssignedName(npcPid)) {
        g_ConvoCache.npcName = EntityRegistry::GetEntityName(npcPid);
    }
    else {
        g_ConvoCache.npcName = GenerateNpcName(g_ConvoCache.npcPersona);
        EntityRegistry::AssignEntityName(npcPid, g_ConvoCache.npcName);
    }
    g_current_npc_name = g_ConvoCache.npcName;

    g_ConvoCache.playerName = "Stranger";
    g_ConvoCache.characterRelationship = "unknown";
    g_ConvoCache.groupRelationship = "unknown";

    if (!g_ConvoCache.npcPersona.inGameName.empty() && !g_ConvoCache.playerPersona.inGameName.empty()) {
        g_ConvoCache.characterRelationship = ConfigReader::GetRelationship(g_ConvoCache.npcPersona.inGameName, g_ConvoCache.playerPersona.inGameName);
    }
    if (!g_ConvoCache.npcPersona.subGroup.empty() && !g_ConvoCache.playerPersona.subGroup.empty()) {
        g_ConvoCache.groupRelationship = ConfigReader::GetRelationship(g_ConvoCache.npcPersona.subGroup, g_ConvoCache.playerPersona.subGroup);
    }
    if (g_ConvoCache.playerPersona.type == "PLAYER") {
        g_ConvoCache.playerName = g_ConvoCache.playerPersona.inGameName;
    }

    if (g_ConvoCache.playerPersona.type == "PLAYER") {
        g_ConvoCache.playerName = g_ConvoCache.playerPersona.inGameName;
    }

    // Die Initialisierungen für AudioManager und AudioSystem wurden entfernt,
    // da sie nur einmal in ScriptMain() erfolgen sollten.

    VoiceSettings vs = AudioManager::GetVoiceForNPC(g_ConvoCache.npcPersona);

    EntityRegistry::SetVoiceSettings(npcPid, vs);
    Log("Assigned Voice: Model=" + vs.model + ", ID=" + std::to_string(vs.id) + " to NPC " + g_ConvoCache.npcName);

    if (ConfigReader::g_Settings.TtS_Enabled && vs.model != "NONE" && !vs.model.empty()) {
        AudioManager::LoadAudioModel(vs.model);
    }

    Log("Conversation context cached.");
}

//EOF

