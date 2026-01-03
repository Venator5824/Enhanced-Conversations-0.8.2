#pragma once



// ------------------------------------------------------------
// 1. ARCHITECTURE & PLATFORM
// ------------------------------------------------------------
#include "Platform.h" 
#include "PlatformSystem.h"

// ------------------------------------------------------------
// 2. STANDARD LIBS
// ------------------------------------------------------------
#include <string>
#include <vector>
#include <future>
#include <chrono>
#include <map>
#include <set>
#include <sstream>  
#include "AudioSystem.h"
// (Andere notwendige Standard-Libs)

// ------------------------------------------------------------
// 3. CORE TYPES & DEFINITIONS
// ------------------------------------------------------------
#include "AbstractTypes.h"
#include "ConfigReader.h" // Stellt sicher, dass NpcPersona etc. bekannt sind
#include "AudioSystem.h"
#include "babylon/babylon.h"
// ------------------------------------------------------------
// 4. EXTERNAL LIBS
// ------------------------------------------------------------
#include "llama.h"
 #include "llama-sampling.h" // Optional, je nach Bedarf
#include "whisper.h"
#include "miniaudio.h"
#include "FileEnums.h"

// ------------------------------------------------------------
// 5. DATA STRUCTURES (Projekt-spezifisch)
// ------------------------------------------------------------
extern std::string LOG_FILE_NAME;

// ------------ //
// 0. voice persona //
// -------------- //


struct VoiceSettings {
    std::string model;
    int id = -2000;
    float speed = 1.0f;
};

struct ConversationCache {
    NpcPersona npcPersona;
    NpcPersona playerPersona;
    std::string npcName;
    std::string playerName;
    std::string characterRelationship;
    std::string groupRelationship;
};

// ------------------------------------------------------------
// 6. GLOBAL VARIABLES 
// ------------------------------------------------------------

// System & State
extern bool g_isInitialized;
extern ConvoState g_convo_state;
extern  InputState g_input_state;
extern InferenceState g_llm_state;
extern int g_current_task_type;
// Conversation Data
extern AHandle g_target_ped;
extern std::string g_current_npc_name;
extern ChatID g_current_chat_ID;
extern ConversationCache g_convoCache; // <-- WICHTIG

// LLM
extern llama_model* g_model;
extern llama_context* g_ctx;
extern std::future<std::string> g_llm_future;
// ... (andere LLM externs)

// Audio (STT)
extern struct whisper_context* g_whisper_ctx;
extern std::vector<float> g_audio_buffer;
extern ma_device g_capture_device;
extern bool g_is_recording;
// ... (andere Audio externs)

// ------------------------------------------------------------
// 7. FUNCTION PROTOTYPES
// ------------------------------------------------------------

// Die Modul-Header werden hier includiert, um ihre Funktions-Prototypen bekannt zu machen
#include "AbstractCalls.h"      
#include "EntityRegistry.h"    
#include "ConversationSystem.h" 
#include "LLM_Inference.h"      
#include "ModHelpers.h"
#include "helperfunctions.h"
#include "OptChatMem.h"
#include "SubtitleManager.h"

// ------------------------------------------------------------
// 8. FUNCTION PROTOTYPES (Exports for ModMain)
// ------------------------------------------------------------

// i change here: Alle Prototypen sind jetzt explizit aufgelistet.

// Eigene Hilfsfunktion für den Conversation Cache
void FillConversationCache(AHandle targetPed, AHandle playerPed);
void LoadVoiceConfigs(const std::string& folderPath);
// Logging
void Log(const std::string& msg);
void LogM(const std::string& msg);
void LogA(const std::string& msg);
void LogSystemMetrics(const std::string& ctx);

// Lifecycle
GAME_API void ScriptMain(); // Verwendet GAME_API Makro für Export
void TERMINATE();
void EndConversation();
bool IsGameInSafeMode();
std::string GetModRootPath();
bool DoesFileExist(const std::string& p);

// Logic
bool IsKeyJustPressed(int vk);

// Aus LLM_Inference.h
std::string GenerateLLMResponse(std::string fullPrompt, bool slowMode);
bool InitializeLLM(const char* model_path);
void ShutdownLLM();
std::string AssemblePrompt(const ConversationCache& cache, const std::vector<std::string>& chatHistory, AHandle playerPed);
std::string CleanupResponse(std::string text);
std::string PerformChatSummarization(const std::string& npcName, const std::vector<std::string>& history); // Annahme, dass diese in main.cpp ist

// Aus ModHelpers.h / helperfunctions.h
void StartNpcConversationTasks(AHandle npc, AHandle player);
void UpdateNpcConversationTasks(AHandle npc, AHandle player);
std::string WordWrap(const std::string& text, size_t limit);
std::string NormalizeString(const std::string& input); // Annahme, dass diese existiert
std::string FindLoRAFile(const std::string& rootPath); // Annahme, dass diese existiert

// Aus Audio / Whisper Teil (angenommen in LLM_Inference.cpp oder ähnlich)
bool InitializeWhisper(const char* model_path);
bool InitializeAudioCaptureDevice();
void StartAudioRecording();
void StopAudioRecording();
std::string TranscribeAudio(std::vector<float> pcm_data);

