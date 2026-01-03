#pragma once
#include "main.h"
#include <vector>
#include <string>
#include <shared_mutex>

// Define types if not already in main.h or AbstractTypes
typedef uint64_t PersistID;
typedef uint64_t ChatID;

const PersistID PID_PLAYER = 0x000001;
const PersistID PID_NPC_START = 0x001001;

struct ConversationData {
    ChatID chatID;
    std::vector<PersistID> participants;
    std::vector<std::string> history;
    std::string summary; // <--- This is where the memory lives
    AbstractTypes::TimeMillis timestamp;
    std::string cd_location;
    std::string cd_weather;
    bool isActive;
};

class ConvoManager {
public:
    // --- LIFECYCLE ---
    // Links to EntityRegistry to get the true ID
    static PersistID GetPersistIDForHandle(AbstractTypes::GameHandle handle);

    // Starts a chat. forceID allows loading saved states (default 0 = random)
    static ChatID InitiateConversation(AbstractTypes::GameHandle initiator, AbstractTypes::GameHandle target, ChatID forceID = 0);

    static void CloseConversation(ChatID chatID);

    // --- DATA MANAGEMENT ---
    // Note: 'const std::string&' is faster than 'std::string'
    static void AddMessageToChat(ChatID chatID, const std::string& senderName, const std::string& message);
    std::vector<std::string>GetParticipantNames(ChatID chatID);
    static std::vector<std::string> GetChatHistory(ChatID chatID);

    // --- MEMORY SYSTEM ---
    // Retrieves the summary of the PREVIOUS conversation between these two
    static std::string GetLastSummaryBetween(PersistID p1, PersistID p2);

    // NEW: Allows ModMain to save the summary after the LLM generates it
    static void SetConversationSummary(ChatID chatID, const std::string& summary);

    // --- STATE CHECKS ---
    static ChatID GetActiveChatID(AbstractTypes::GameHandle handle);
    static bool IsChatActive(ChatID chatID);

    // --- CONTEXT INJECTION ---
    static void SetChatContext(ChatID chatID, const std::string& location, const std::string& weather);
    static void ReplaceHistory(ChatID chatID, const std::vector<std::string>& newHistory);
    static void RunMaintenance();

private:
    static ChatID GenerateNewChatID();
};