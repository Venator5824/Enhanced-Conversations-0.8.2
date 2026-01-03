#pragma once
#include <string>

// Logging
void Log(const std::string& msg);
void LogM(const std::string& msg);
void LogA(const std::string& msg);
void LogConfig(const std::string& message);
extern std::string g_renderText;
// Utilities
std::string FindLoRAFile(const std::string& root_path);
std::string WordWrap(const std::string& text, size_t limit = 50);
std::string NormalizeString(const std::string& input); 
float GetFreeVRAM_MB(); 
float GET_VRAM_Usage_MB();
uint64_t GetProcessRAMUsage();
uint64_t GetFreeSystemRAM();

