#pragma once
#include "main.h"
#include "AbstractTypes.h"
#include "EntityRegistry.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <memory>
#include <filesystem>

namespace Vits {
    class Session;
}

class AudioManager {
public:
    static void Initialize(const std::string& rootPath);
    static VoiceSettings GetVoiceForNPC(const NpcPersona& persona);
    static std::string ResolveModelPath(const std::string& modelKey);

    static bool LoadAudioModel(const std::string& modelKey);
    static void UnloadAudioModel(const std::string& modelKey);
    static void UnloadAllAudioModels();

    static Vits::Session* GetSession(const std::string& modelKey);

   
    struct VoiceEntry {
        int id;                
        std::string modelName; 

        // --- HIER WAREN DIE FEHLENDEN VARIABLEN ---
        std::string path_raw;   
        int internal_id;        
        // ------------------------------------------

        std::string gender;
        std::string voicetype;
        std::string age;
        std::string quality;
        bool isReserved = false;
    };

private:
    static std::vector<VoiceEntry> s_voiceDatabase;
    static std::unordered_map<std::string, std::string> s_modelPaths;
    static std::unordered_map<std::string, std::unique_ptr<Vits::Session>> s_activeSessions;

    static std::shared_mutex s_sessionMutex;
    static std::string s_rootPath;

    
    static bool MatchesGender(const std::string& npcGender, const std::string& voiceGender);
    static bool MatchesVoiceType(const std::string& npcType, const std::string& voiceType);
    static bool MatchesAge(const std::string& npcAge, const std::string& voiceAge);
    static bool MatchesSomeSpecial();

    
    static void LoadVoiceConfigs(const std::string& folderPath);
};