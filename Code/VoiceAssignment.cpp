#include "main.h"
#include "EntityRegistry.h" // Muss 'struct VoiceSettings' enthalten
#include "AudioSystem.h"
#include <filesystem>
#include <fstream>
#include <vector>

// ----------------------------------------------------------------
// INTERNE STRUKTUREN
// ----------------------------------------------------------------

struct VoiceEntry {
    int iniID;
    int repInternalID = -1;
    std::string assignedModelName;

    std::string gender;     // "m", "f", "n"
    std::string age;        // "y", "m", "o"
    std::string voice;
    std::string special;

    int GetRealID() const {
        return (repInternalID != -1) ? repInternalID : iniID;
    }
};

static std::vector<VoiceEntry> g_LoadedVoiceConfigs;

// ----------------------------------------------------------------
// VORWÄRTSDEKLARATIONEN DER 4 MATCH FUNKTIONEN
// ----------------------------------------------------------------
bool GetAgeMatch(const NpcPersona& npc, const VoiceEntry& entry);
bool GetGenderMatch(const NpcPersona& npc, const VoiceEntry& entry);
bool GetVoiceMatch(const NpcPersona& npc, const VoiceEntry& entry);
bool GetSpecialMatch(const NpcPersona& npc, const VoiceEntry& entry);


// ----------------------------------------------------------------
// 1. PARSER
// ----------------------------------------------------------------
void LoadVoiceConfigs(const std::string& folderPath) {
    g_LoadedVoiceConfigs.clear();
    if (!std::filesystem::exists(folderPath)) return;

    for (const auto& entry : std::filesystem::directory_iterator(folderPath)) {
        if (!entry.is_regular_file()) continue;
        std::string filename = entry.path().filename().string();

        if (filename.find("EC_Voices_list_") == 0 && entry.path().extension() == ".ini") {
            std::ifstream file(entry.path());
            std::string line;
            std::string currentFileModel;
            VoiceEntry currentEntry;
            bool hasEntry = false;

            while (std::getline(file, line)) {
                if (line.empty() || line[0] == ';') continue;
                if (!line.empty() && line.back() == '\r') line.pop_back();

                if (line.find("model_name") != std::string::npos && line.find("=") != std::string::npos) {
                    currentFileModel = line.substr(line.find("=") + 1);
                    while (!currentFileModel.empty() && currentFileModel.front() == ' ') currentFileModel.erase(0, 1);
                }
                else if (line.find("[") == 0 && line.find("]") != std::string::npos) {
                    if (hasEntry) {
                        currentEntry.assignedModelName = currentFileModel;
                        g_LoadedVoiceConfigs.push_back(currentEntry);
                    }
                    currentEntry = VoiceEntry();
                    hasEntry = true;
                    try { currentEntry.iniID = std::stoi(line.substr(1, line.find("]") - 1)); }
                    catch (...) { hasEntry = false; }
                }
                else if (hasEntry) {
                    if (line.find("gender") != std::string::npos) {
                        if (line.find("m") != std::string::npos) currentEntry.gender = "m";
                        else if (line.find("f") != std::string::npos) currentEntry.gender = "f";
                        else currentEntry.gender = "n";
                    }
                    else if (line.find("age") != std::string::npos) {
                        if (line.find("y") != std::string::npos) currentEntry.age = "y";
                        else if (line.find("o") != std::string::npos) currentEntry.age = "o";
                        else currentEntry.age = "m";
                    }
                    else if (line.find("repInternalID") != std::string::npos) {
                        size_t eq = line.find("=");
                        if (eq != std::string::npos) {
                            try { currentEntry.repInternalID = std::stoi(line.substr(eq + 1)); }
                            catch (...) {}
                        }
                    }
                }
            }
            if (hasEntry) {
                currentEntry.assignedModelName = currentFileModel;
                g_LoadedVoiceConfigs.push_back(currentEntry);
            }
        }
    }
}


// ----------------------------------------------------------------
// 2. MATCH LOGIK (DIE 4 FUNKTIONEN)
// ----------------------------------------------------------------

// 1. AGE (Return true wie gewünscht)
bool GetAgeMatch(const NpcPersona& npc, const VoiceEntry& entry) {
    return true;
}

// 2. GENDER (Die Bedingung, die stimmen muss)
bool GetGenderMatch(const NpcPersona& npc, const VoiceEntry& entry) {
    // Wenn INI leer ist, passt es auf alles
    if (entry.gender.empty()) return true;

    // Mapping deiner Persona Strings zu INI chars (m/f)
    // ACHTUNG: Hier greife ich auf npc.gender zu, weil das im Registry-Code so stand.
    // Falls der Member anders heißt (z.B. p_gender), musst du das hier anpassen.
    char target = 'n';
    if (npc.gender == "Male") target = 'm';
    else if (npc.gender == "Female") target = 'f';

    return entry.gender[0] == target;
}

// 3. VOICE (Return true wie gewünscht)
bool GetVoiceMatch(const NpcPersona& npc, const VoiceEntry& entry) {
    return true;
}

// 4. SPECIAL (Return true wie gewünscht)
bool GetSpecialMatch(const NpcPersona& npc, const VoiceEntry& entry) {
    return true;
}


// ----------------------------------------------------------------
// 3. SELECTION (PickVoiceID)
// ----------------------------------------------------------------

// Gibt Pointer auf den Eintrag zurück oder nullptr (-2000)
const VoiceEntry* PickVoiceID(const NpcPersona& npc, const std::vector<VoiceEntry>& voiceList) {
    std::vector<int> validIndices;

    // A. FILTERN (Die 4 Matcher aufrufen)
    for (size_t i = 0; i < voiceList.size(); ++i) {
        if (!GetAgeMatch(npc, voiceList[i])) continue;
        if (!GetGenderMatch(npc, voiceList[i])) continue; // Der wichtige Check
        if (!GetVoiceMatch(npc, voiceList[i])) continue;
        if (!GetSpecialMatch(npc, voiceList[i])) continue;

        validIndices.push_back(i);
    }

    if (validIndices.empty()) return nullptr;

    // B. KONFLIKT-CHECK
    std::vector<int> freeIndices;
    for (int idx : validIndices) {
        const auto& e = voiceList[idx];
        if (!EntityRegistry::IsVoiceActive(e.assignedModelName, e.GetRealID())) {
            freeIndices.push_back(idx);
        }
    }

    // C. WÜRFELN
    int finalIdx = -1;
    if (!freeIndices.empty()) {
        finalIdx = freeIndices[rand() % freeIndices.size()];
    } 
    else {
        finalIdx = validIndices[rand() % validIndices.size()];
    }

    return &voiceList[finalIdx];
}


// ----------------------------------------------------------------
// 4. HAUPT ZUWEISUNG (PickVoiceForPersona)
// ----------------------------------------------------------------

VoiceSettings PickVoiceForPersona(const NpcPersona& p) {
    VoiceSettings v; // Aus EntityRegistry.h
    v.speed = 1.0f;  // Default

    
    if (!p.p_audio_model.empty()) {
        v.model = p.p_audio_model;
        v.id = (p.p_audio_ID != -1) ? p.p_audio_ID : 0;
        return v;
    }

    
    const VoiceEntry* match = PickVoiceID(p, g_LoadedVoiceConfigs);

    if (match != nullptr) {
        
        if (!match->assignedModelName.empty()) {
            v.model = match->assignedModelName;
        }
        else {
            
            v.model = ConfigReader::g_Settings.TTS_MODEL_ALT_NAME;
        }
        v.id = match->GetRealID();
    }
    else {
        
        v.model = "NONE";
        v.id = -2000;
    }
    return v;
}

VoiceSettings EntityRegistry::GetVoiceSettings(PersistID id) {
    return VoiceSettings();
}