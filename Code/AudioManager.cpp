#include "AudioManager.h"
#include "ConfigReader.h"
#include "main.h"
#include <filesystem>
#include <fstream>
#include <cctype>
#include <algorithm>
#include "memory"
#include "babylon/babylon.h" 

namespace fs = std::filesystem;

std::vector<AudioManager::VoiceEntry> AudioManager::s_voiceDatabase;
std::unordered_map<std::string, std::string> AudioManager::s_modelPaths;
std::unordered_map<std::string, std::unique_ptr<Vits::Session>> AudioManager::s_activeSessions;
std::shared_mutex AudioManager::s_sessionMutex;
std::string AudioManager::s_rootPath;

void AudioManager::Initialize(const std::string& gameRoot) {
    std::unique_lock lock(s_sessionMutex);
    s_rootPath = gameRoot;
    s_voiceDatabase.clear();
    s_modelPaths.clear();

    std::string configFolder = gameRoot + "/ECmod/AudioDataFiles";

    if (!fs::exists(configFolder)) {
        Log("AudioManager: Config folder missing: " + configFolder);
        return;
    }

    for (const auto& entry : fs::directory_iterator(configFolder)) {
        if (entry.path().extension() == ".ini" &&
            entry.path().filename().string().find("EC_Voices_list_") == 0) {

            std::ifstream file(entry.path());
            std::string line;
            std::string currentBasePath;

            // Temporäre Speicher für den aktuellen Parsing-Block
            VoiceEntry currentEntry;
            bool entryOpen = false;

            while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                line.erase(0, line.find_first_not_of(" \t"));
                if (line.empty() || line[0] == ';') continue;

                // --- 1. GLOBAL SETTINGS (model_path) ---
                if (line.find("model_path") == 0) {
                    size_t sep = line.find('=');
                    if (sep != std::string::npos) {
                        std::string val = line.substr(sep + 1);
                        val.erase(0, val.find_first_not_of(" \t"));

                        if (val.find("root/") == 0) val = s_rootPath + "/" + val.substr(5);

                        std::replace(val.begin(), val.end(), '\\', '/');
                        if (!val.empty() && val.back() != '/') val += '/';
                        currentBasePath = val;
                    }
                }

                // --- 2. START NEUER BLOCK [ID] ---
                else if (line[0] == '[' && line.find(']') != std::string::npos) {
                    // Vorherigen Eintrag abschließen
                    if (entryOpen) {
                        s_voiceDatabase.push_back(currentEntry);
                    }

                    // Reset
                    currentEntry = VoiceEntry();
                    entryOpen = true;

                    // ID aus Klammer [1]
                    std::string content = line.substr(1, line.find(']') - 1);
                    // Filter nur Ziffern (falls "[ID 1]" statt "[1]")
                    std::string digits;
                    for (char c : content) if (isdigit(c)) digits += c;

                    try { currentEntry.id = std::stoi(digits); }
                    catch (...) { currentEntry.id = 0; }

                    // Defaults
                    currentEntry.internal_id = 0;
                    currentEntry.isReserved = false;
                }

                // --- 3. ATTRIBUTE LESEN ---
                else if (entryOpen) {
                    size_t sep = line.find_first_of("=:"); // Unterstützt = und :
                    if (sep == std::string::npos) continue;

                    std::string key = line.substr(0, sep);
                    std::string val = line.substr(sep + 1);

                    // Trim Key & Val
                    key.erase(key.find_last_not_of(" \t") + 1);
                    val.erase(0, val.find_first_not_of(" \t"));

                    // Mapping
                    if (key == "rep_internal_id" || key == "int_id") {
                        try { currentEntry.internal_id = std::stoi(val); }
                        catch (...) { currentEntry.internal_id = 0; }
                    }
                    else if (key == "name")   currentEntry.modelName = val;
                    else if (key == "path")   currentEntry.path_raw = val;
                    else if (key == "gender") currentEntry.gender = val;
                    else if (key == "age")    currentEntry.age = val;
                    else if (key == "voice")  currentEntry.voicetype = val;
                    else if (key == "enable" && val == "0") entryOpen = false; // Disable -> nicht speichern
                }
            }
            // Letzten Eintrag speichern
            if (entryOpen) {
                s_voiceDatabase.push_back(currentEntry);
            }

            // --- 4. PFADE AUFLÖSEN (Nach dem Parsen) ---
            for (auto& ve : s_voiceDatabase) {
                // UID festlegen: Wenn Name leer, nimm die Listen-ID als String ("1")
                std::string uid = ve.modelName;
                if (uid.empty()) uid = std::to_string(ve.id);
                ve.modelName = uid; // Speichern für später

                // Pfad bauen
                std::string finalPath = "";

                // Fall A: Eintrag hat einen eigenen Pfad (Single File)
                if (!ve.path_raw.empty()) {
                    finalPath = currentBasePath + ve.path_raw;
                }
                // Fall B: Kein eigener Pfad -> Nutze Basis-Pfad (Multi-Speaker ONNX)
                // (Hier müsste man eigentlich wissen, wie die Hauptdatei heißt. 
                //  Oft ist currentBasePath dann der Pfad zur Datei, nicht zum Ordner.
                //  Aber lassen wir es erstmal bei Fall A, das deckt deine Piper-Liste ab.)

                if (!finalPath.empty()) {
                    // .onnx Logik
                    if (finalPath.find(".onnx") == std::string::npos) finalPath += ".onnx";

                    // Windows Fix
                    std::replace(finalPath.begin(), finalPath.end(), '/', '\\');

                    // Check
                    if (fs::exists(finalPath)) {
                        s_modelPaths[uid] = finalPath;
                    }
                    else {
                        Log("AudioManager: WARNING - File missing for Voice " + uid + ": " + finalPath);
                    }
                }
            }
        }
    }
    Log("AudioManager: Loaded " + std::to_string(s_voiceDatabase.size()) + " voices.");
}

VoiceSettings AudioManager::GetVoiceForNPC(const NpcPersona& persona) {
    // 1. Manuelle Zuweisung (Prio 1)
    if (!persona.p_audio_model.empty()) {
        // Wir nehmen an, p_audio_model ist der Name (z.B. "Alan" oder "1")
        return { persona.p_audio_model, persona.p_audio_ID, 1.0f };
    }

    // 2. Matching
    std::vector<const VoiceEntry*> candidates;
    for (const auto& entry : s_voiceDatabase) {
        if (entry.isReserved) continue;

        if (MatchesGender(persona.n_gender, entry.gender) &&
            MatchesAge(persona.n_age, entry.age) &&
            MatchesVoiceType(persona.n_voicetype, entry.voicetype)) {
            candidates.push_back(&entry);
        }
    }

    if (!candidates.empty()) {
        const auto* picked = candidates[rand() % candidates.size()];
        // Wir geben zurück:
        // model = UID (Key für Pfad-Map)
        // id = Interne Speaker ID (0 für Single, >0 für Multi)
        return { picked->modelName, picked->internal_id, 1.0f };
    }

    // Fallback
    return { "DEFAULT", 0, 1.0f };
}

std::string AudioManager::ResolveModelPath(const std::string& modelName) {
    auto it = s_modelPaths.find(modelName);
    return (it != s_modelPaths.end()) ? it->second : "";
}

// Helpers
static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}
bool AudioManager::MatchesGender(const std::string& npc, const std::string& voice) {
    if (voice == "n" || voice.empty() || npc.empty()) return true;
    return ToLower(npc).substr(0, 1) == ToLower(voice).substr(0, 1);
}
bool AudioManager::MatchesAge(const std::string& npc, const std::string& voice) {
    if (voice.empty() || npc.empty()) return true;
    return ToLower(npc) == ToLower(voice);
}
bool AudioManager::MatchesVoiceType(const std::string& npc, const std::string& voice) {
    if (voice.empty() || npc.empty()) return true;
    return ToLower(npc) == ToLower(voice);
}


bool AudioManager::LoadAudioModel(const std::string& modelName) {
    std::unique_lock lock(s_sessionMutex);

    if (s_activeSessions.count(modelName)) return true;

    std::string path = ResolveModelPath(modelName);

    // LOGGEN, was wir versuchen
    Log("AudioManager: [LOAD] Request to load model: " + modelName);
    Log("AudioManager: [LOAD] Path: " + path);

    if (path.empty() || !fs::exists(path)) {
        Log("AudioManager: [ERROR] Path invalid or file missing!");
        return false;
    }

    try {
        // HIER passiert das Laden mit dem gefixten Konstruktor aus Schritt 1
        s_activeSessions[modelName] = std::make_unique<Vits::Session>(path);

        // WICHTIG: Wenn wir hier ankommen, war Schritt 1 erfolgreich!
        Log("AudioManager: [SUCCESS] Vits::Session loaded successfully for: " + modelName);
        return true;
    }
    catch (const std::exception& e) {
        Log("AudioManager: [CRITICAL FAILURE] " + std::string(e.what()));
        return false;
    }
    catch (...) {
        Log("AudioManager: [CRITICAL FAILURE] Unknown crash loading VITS.");
        return false;
    }
}

void AudioManager::UnloadAudioModel(const std::string& modelName) {
    std::unique_lock lock(s_sessionMutex);
    s_activeSessions.erase(modelName);
}
void AudioManager::LoadVoiceConfigs(const std::string& folderPath) { Initialize(folderPath); }
void AudioManager::UnloadAllAudioModels() {
    std::unique_lock lock(s_sessionMutex);
    s_activeSessions.clear();
}
Vits::Session* AudioManager::GetSession(const std::string& modelName) {
    std::shared_lock lock(s_sessionMutex);
    auto it = s_activeSessions.find(modelName);
    return (it != s_activeSessions.end()) ? it->second.get() : nullptr;
}