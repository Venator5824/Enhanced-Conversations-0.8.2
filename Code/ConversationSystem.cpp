#include <unordered_map>
#include <mutex>
#include <vector>
#include <shared_mutex>
#include <string>
#include <chrono>

#include "main.h"
#include "ConversationSystem.h"
#include "AbstractCalls.h"
#include "EntityRegistry.h"
#include "ConfigReader.h" // Required for Janitor settings

using namespace AbstractGame;
using namespace AbstractTypes;

// --- STORAGE ---
static std::unordered_map<ChatID, ConversationData> g_activeChats;
static std::unordered_map<ChatID, ConversationData> g_archivedChats;
static std::unordered_map<PersistID, ChatID> g_participantToChatMap;
static std::unordered_map<uint64_t, std::string> g_historyIndex;

// --- THREAD SAFETY ---
static std::shared_mutex g_convoMutex;
static std::mutex g_idGenMutex;

// --- HELPERS ---
inline uint64_t MakePairKey(PersistID p1, PersistID p2) {
    if (p1 > p2) std::swap(p1, p2);
    return p1 ^ (p2 + 0x9e3779b9 + (p1 << 6) + (p1 >> 2));
}

// --- ID LOGIC ---
PersistID ConvoManager::GetPersistIDForHandle(GameHandle handle) {
    return EntityRegistry::RegisterNPC(handle);
}

ChatID ConvoManager::GenerateNewChatID() {
    static ChatID counter = 100;
    std::lock_guard<std::mutex> lock(g_idGenMutex);
    return counter++;
}

// --- LIFECYCLE ---
ChatID ConvoManager::InitiateConversation(GameHandle initiator, GameHandle target, ChatID forceID) {
    std::unique_lock<std::shared_mutex> lock(g_convoMutex);

    PersistID p1 = GetPersistIDForHandle(initiator);
    PersistID p2 = GetPersistIDForHandle(target);

    ChatID newID;
    if (forceID != 0) {
        if (g_activeChats.count(forceID)) return forceID;
        newID = forceID;
    }
    else {
        newID = GenerateNewChatID();
    }

    ConversationData data;
    data.chatID = newID;
    data.participants.push_back(p1);
    data.participants.push_back(p2);
    data.timestamp = GetTimeMs();
    data.isActive = true;

    // Load Memory (Previous Summary)
    uint64_t pairKey = MakePairKey(p1, p2);
    if (g_historyIndex.count(pairKey)) {
        data.history.push_back("<|system|>\n[MEMORY] Previous encounter: " + g_historyIndex[pairKey]);
    }

    g_activeChats[newID] = data;
    g_participantToChatMap[p1] = newID;
    g_participantToChatMap[p2] = newID;

    return newID;
}

void ConvoManager::CloseConversation(ChatID chatID) {
    std::unique_lock<std::shared_mutex> lock(g_convoMutex);

    auto it = g_activeChats.find(chatID);
    if (it != g_activeChats.end()) {
        ConversationData& data = it->second;
        data.isActive = false;
        data.timestamp = GetTimeMs(); // Update timestamp for deletion timer

        // Cleanup participants map
        for (auto p : data.participants) {
            g_participantToChatMap.erase(p);
        }

        g_archivedChats[chatID] = data;
        g_activeChats.erase(it);
    }
}

// --- DATA ACCESS ---
ChatID ConvoManager::GetActiveChatID(GameHandle handle) {
    std::shared_lock<std::shared_mutex> lock(g_convoMutex);
    PersistID targetPid = GetPersistIDForHandle(handle);

    auto it = g_participantToChatMap.find(targetPid);
    if (it != g_participantToChatMap.end()) {
        return it->second;
    }
    return 0;
}

void ConvoManager::AddMessageToChat(ChatID chatID, const std::string& senderName, const std::string& message) {
    std::unique_lock<std::shared_mutex> lock(g_convoMutex);

    auto it = g_activeChats.find(chatID);
    if (it != g_activeChats.end()) {
        std::string entry;
        if (senderName == "Player") entry = "<|user|>\n" + message;
        else entry = "<|assistant|>\n" + message;

        it->second.history.push_back(entry);
        it->second.timestamp = GetTimeMs();

        // Safety Limit
        int hardLimit = ConfigReader::g_Settings.MaxChatHistoryLines;
        if (hardLimit < 5) hardLimit = 10;

        if (it->second.history.size() > (size_t)(hardLimit + 5)) {
            if (it->second.history.size() > 1) {
                it->second.history.erase(it->second.history.begin() + 1);
            }
        }
    }
}

// --- THE CRITICAL FIX: SECRETARY SAVER ---
// This ensures the summary is saved even if the chat is already archived.
void ConvoManager::SetConversationSummary(ChatID chatID, const std::string& summary) {
    std::unique_lock<std::shared_mutex> lock(g_convoMutex);

    // 1. Try Active
    auto itActive = g_activeChats.find(chatID);
    if (itActive != g_activeChats.end()) {
        itActive->second.summary = summary;
        return;
    }

    // 2. Try Archive (Crucial for Secretary)
    auto itArch = g_archivedChats.find(chatID);
    if (itArch != g_archivedChats.end()) {
        ConversationData& data = itArch->second;
        data.summary = summary;

        // 3. Save to Permanent Index
        if (data.participants.size() >= 2) {
            uint64_t pairKey = MakePairKey(data.participants[0], data.participants[1]);
            g_historyIndex[pairKey] = summary;
        }
    }
}

void ConvoManager::ReplaceHistory(ChatID chatID, const std::vector<std::string>& newHistory) {
    std::unique_lock<std::shared_mutex> lock(g_convoMutex);
    auto it = g_activeChats.find(chatID);
    if (it != g_activeChats.end()) {
        it->second.history = newHistory;
    }
}

std::vector<std::string> ConvoManager::GetChatHistory(ChatID chatID) {
    std::shared_lock<std::shared_mutex> lock(g_convoMutex);
    auto it = g_activeChats.find(chatID);
    if (it != g_activeChats.end()) {
        return it->second.history;
    }
    return {};
}

std::string ConvoManager::GetLastSummaryBetween(PersistID p1, PersistID p2) {
    std::shared_lock<std::shared_mutex> lock(g_convoMutex);
    uint64_t pairKey = MakePairKey(p1, p2);
    if (g_historyIndex.count(pairKey)) {
        return g_historyIndex[pairKey];
    }
    return "";
}

bool ConvoManager::IsChatActive(ChatID chatID) {
    std::shared_lock<std::shared_mutex> lock(g_convoMutex);
    auto it = g_activeChats.find(chatID);
    return (it != g_activeChats.end() && it->second.isActive);
}

void ConvoManager::SetChatContext(ChatID chatID, const std::string& location, const std::string& weather) {
    std::unique_lock<std::shared_mutex> lock(g_convoMutex);
    auto it = g_activeChats.find(chatID);
    if (it != g_activeChats.end()) {
        it->second.cd_location = location;
        it->second.cd_weather = weather;
    }
}

void ConvoManager::RunMaintenance() {
    std::unique_lock<std::shared_mutex> lock(g_convoMutex);

    if (g_archivedChats.empty()) return;

    uint32_t now = GetTimeMs();
    int timeoutSec = ConfigReader::g_Settings.DeletionTimer;
    int maxHistory = ConfigReader::g_Settings.MaxAllowedChatHistory;

    // 1. Time-based Deletion
    for (auto it = g_archivedChats.begin(); it != g_archivedChats.end(); ) {
        long long elapsedSec = (now - it->second.timestamp) / 1000;

        if (timeoutSec > 0 && elapsedSec > timeoutSec) {
            // Delete from RAM, but Permanent Index remains
            it = g_archivedChats.erase(it);
        }
        else {
            ++it;
        }
    }

    // 2. Capacity-based Deletion
    if (maxHistory > 0 && g_archivedChats.size() > (size_t)maxHistory) {
        while (g_archivedChats.size() > (size_t)maxHistory) {
            g_archivedChats.erase(g_archivedChats.begin());
        }
    }
}



// In ConversationSystem.cpp:
std::vector<std::string> ConvoManager::GetParticipantNames(ChatID chatID) {
    std::shared_lock<std::shared_mutex> lock(g_convoMutex);

    std::vector<PersistID> participants;

    auto itActive = g_activeChats.find(chatID);
    if (itActive != g_activeChats.end()) {
        participants = itActive->second.participants;
    }
    else {
 
        auto itArch = g_archivedChats.find(chatID);
        if (itArch != g_archivedChats.end()) {
            participants = itArch->second.participants;
        }
    }

    std::vector<std::string> names;
    if (participants.empty()) return names;

    for (PersistID pid : participants) {        std::string name = EntityRegistry::GetEntityName(pid);
        if (name.empty()) name = "Unknown Entity";
        names.push_back(name);
    }

    return names;
}

//EOF