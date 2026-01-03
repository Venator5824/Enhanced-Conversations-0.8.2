#pragma once
#include <vector>
#include <string>
#include <future>
#include <mutex>
#include "AbstractTypes.h"


typedef uint64_t ChatID;

struct OptimizationProfile {
    int level = 0; 
    // 0=Off, 1=Light, 2=Aggressive, 3=Auto
    int tokensPerSecondLimit = 10;
    bool isActive = false;
};

class ChatOptimizer {
public:

    static bool CheckAndOptimize(
        ChatID chatID,
        std::vector<std::string>& history,
        const std::string& npcName,
        const std::string& playerName
    );

    static bool ApplyPendingOptimizations(std::vector<std::string>& history);

     static void SetConversationProfile(ChatID chatID, int level);

private:
    static std::string BackgroundSummarizerTask(
        std::vector<std::string> linesToSummarize,
        std::string npcName,
        std::string playerName,
        int throttleSpeed
    );

    static float GetAvailableVRAM_MB();
};