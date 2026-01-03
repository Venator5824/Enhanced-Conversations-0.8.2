#pragma once

// LLM INFERENCE.h

#include <string>
#include <vector>
#include <future>
#include <chrono>
#include "AbstractTypes.h"
#include "ConfigReader.h"
#include "FileEnums.h"

extern std::string LOG_FILE_NAME3;

// Global Variables (Externs)
extern InferenceState g_llm_state;
extern std::future<std::string> g_llm_future;
extern std::string g_llm_response;
extern std::chrono::high_resolution_clock::time_point g_response_start_time;
extern std::chrono::high_resolution_clock::time_point g_llm_start_time;

struct llama_adapter_lora;


extern struct llama_adapter_lora* g_lora_adapter;
extern float g_current_tps;

bool InitializeLLM(const char* model_path);
void ShutdownLLM();
std::string GenerateLLMResponse(std::string fullPrompt, bool slowMode);
std::string AssemblePrompt(AHandle targetPed, AHandle playerPed, const std::vector<std::string>& chatHistory);
std::string CleanupResponse(std::string text);
std::string PerformChatSummarization(const std::string& npcName, const std::vector<std::string>& history);
std::string GenerateNpcName(const NpcPersona& persona);
void LogLLM(const std::string& message);
void LogMemoryStats();
void UpdateTPS(int tokensGenerated, double elapsedSeconds);
std::string GetCurrentTimeState();
std::string GetCurrentWeatherState();
bool InitializeWhisper(const char* model_path);
bool InitializeAudioCaptureDevice();
void StartAudioRecording();
void StopAudioRecording();
std::string TranscribeAudio(std::vector<float> pcm_data);
void ShutdownWhisper();
void ShutdownAudioDevice();
void ShutdownBadassLogging();
extern bool g_is_recording;
extern std::future<std::string> g_stt_future;
extern std::vector<float> g_audio_buffer;
void UpdateTPS(int tokensGenerated, double elapsedSeconds);
void GetBadassLogging();
//EOF