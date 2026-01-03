#include "SubtitleManager.h"
#include "main.h" 
#include "AbstractCalls.h" 
#include "AudioSystem.h"
#include <sstream>
#include <algorithm>
#include <vector>

// ---------------------------------------------------------
// GLOBAL INSTANCE
// ---------------------------------------------------------
SubtitleManager g_Subtitles;

// ---------------------------------------------------------
// CONSTANTS & CONFIGURATION
// ---------------------------------------------------------
const size_t MAX_VISIBLE_ITEMS = 5;       // Max messages on stack
const float FONT_SCALE = 0.45f;           // Text size
const float LINE_HEIGHT = 0.035f;         // Gap between lines
const float START_Y = 0.85f;              // Screen Y position (bottom)
const uint32_t APPEND_WINDOW_MS = 1500;   // Smart-Append window
const uint32_t MIN_DISPLAY_TIME = 3000;   // Min duration
const uint32_t BASE_FADE_MS = 1000;       // Fade out time
const size_t CHAR_LIMIT_PER_LINE = 80;    // CRITICAL: Keep under 99 for GTA V!

// ---------------------------------------------------------
// HELPER: SPLIT STRING BY DELIMITER
// ---------------------------------------------------------
// We need this to render line-by-line to bypass the engine's char limit
std::vector<std::string> SplitString2(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

// ---------------------------------------------------------
// HELPER: WORD WRAP
// ---------------------------------------------------------
// Ensures text fits within the safe rendering area
std::string SubtitleManager::PerformWordWrap(const std::string& text, int& outLineCount) {
    std::stringstream ss(text);
    std::string word, result, currentLine;
    int lines = 1;

    while (ss >> word) {
        // Check if adding the word exceeds the GTA V safe limit
        if (currentLine.length() + word.length() + 1 > CHAR_LIMIT_PER_LINE) {
            result += currentLine + "\n";
            currentLine = word;
            lines++;
        }
        else {
            if (!currentLine.empty()) currentLine += " ";
            currentLine += word;
        }
    }
    result += currentLine;
    outLineCount = lines;
    return result;
}

// ---------------------------------------------------------
// HELPER: DURATION CALCULATION
// ---------------------------------------------------------
// Dynamic duration based on text length
uint32_t SubtitleManager::CalculateDuration(const std::string& text) {
    uint32_t needed = (uint32_t)(text.length() * 80);
    if (needed < MIN_DISPLAY_TIME) return MIN_DISPLAY_TIME;
    if (needed > 15000) return 15000;
    return needed;
}

// ---------------------------------------------------------
// PUBLIC: SHOW MESSAGE
// ---------------------------------------------------------
// Adds text to the UI stack and queues audio for synchronization

void SubtitleManager::ShowMessage(const std::string& name, const std::string& chunk,
    const std::vector<int16_t>& pcm, int rate) {
    if (chunk.empty()) return;

    std::lock_guard<std::mutex> lock(m_Mutex);

    uint32_t now = (uint32_t)AbstractGame::GetTimeMs();
    bool appended = false;

    // 1. Try to append to existing message (Smart Append)
    if (!m_Queue.empty()) {
        SubEntry& last = m_Queue.back();

        // Wenn der gleiche Sprecher weiterredet (oder wir ein Update machen):
        if (last.speaker == name && (now - last.lastUpdateTime < APPEND_WINDOW_MS)) {

            // --- FIX 1: Doppelten Text verhindern ---
            // Wir hängen den Text NUR an, wenn er wirklich neu ist.
            // Wenn der eingehende Text (chunk) identisch mit dem existierenden ist, machen wir nichts.
            if (chunk != last.fullText) {
                // Optional: Prüfen ob es Streaming ist (append) oder Korrektur
                // Hier gehen wir sicher: Nur anhängen, wenn es nicht das Gleiche ist.
                last.fullText += chunk;
            }

            // --- FIX 2: Audio nachträglich injizieren ---
            // Wenn der Eintrag noch kein Audio hat, aber wir jetzt welches bekommen:
            if (last.pcmData.empty() && !pcm.empty()) {
                last.pcmData = pcm;
                last.sampleRate = rate;
                last.playedAudio = false; // WICHTIG: Zurücksetzen, damit AudioSystem es abspielt!
                last.creationTime = now;  // Zeit resetten, damit der Sync-Check (1100ms) durchgeht
            }

            // Layout neu berechnen
            std::string combined = last.speaker + ": " + last.fullText;
            last.wrappedText = PerformWordWrap(combined, last.lineCount);

            last.displayUntil = now + CalculateDuration(last.fullText);
            last.lastUpdateTime = now;
            last.alpha = 255.0f;

            appended = true;
        }
    }

    // 2. Create new entry if not appended
    if (!appended) {
        SubEntry entry;
        entry.speaker = name;
        entry.fullText = chunk;

        // Store Audio Data for Sync
        entry.pcmData = pcm;
        entry.sampleRate = rate;
        entry.playedAudio = false;
        entry.creationTime = now;

        std::string combined = name + ": " + chunk;
        entry.wrappedText = PerformWordWrap(combined, entry.lineCount);

        entry.lastUpdateTime = now;
        entry.displayUntil = now + CalculateDuration(chunk);
        entry.alpha = 255.0f;

        m_Queue.push_back(entry);
    }

    // 3. Cleanup queue size
    while (m_Queue.size() > MAX_VISIBLE_ITEMS) {
        m_Queue.erase(m_Queue.begin());
    }
}

// ---------------------------------------------------------
// PUBLIC: UPDATE AND RENDER
// ---------------------------------------------------------
// Called every frame to handle logic, audio triggers, and drawing
void SubtitleManager::UpdateAndRender() {
    if (m_Queue.empty()) return;

    uint32_t now = (uint32_t)AbstractGame::GetTimeMs();

    // 1. Logic Loop: Handle Audio & Expiry
    for (auto it = m_Queue.begin(); it != m_Queue.end(); ) {

        // --- AUDIO SYNC TRIGGER ---
        if (!it->playedAudio && !it->pcmData.empty()) {
            // SYNC CHECK: Only play if we are within 1100ms of the text appearing.
            // This prevents old audio from playing if the game lagged/paused.
            if (now <= it->creationTime + 1100) {
                AudioSystem::PlayBuffer(it->pcmData, it->sampleRate);
            }
            it->playedAudio = true; // Mark handled to prevent re-triggering
        }

        // --- TIMEOUT & FADING ---
        if (now > it->displayUntil) {
            it = m_Queue.erase(it);
        }
        else {
            // Fade out logic
            uint32_t timeRemaining = it->displayUntil - now;
            if (timeRemaining < BASE_FADE_MS) {
                float ratio = (float)timeRemaining / (float)BASE_FADE_MS;
                it->alpha = ratio * 255.0f;
            }
            else {
                it->alpha = 255.0f;
            }
            ++it;
        }
    }

    if (m_Queue.empty()) return;

    // 2. Render Loop (Bottom-Up Stack)
    float currentY = START_Y;

    // Iterate backwards (Newest message at bottom)
    for (int i = (int)m_Queue.size() - 1; i >= 0; --i) {
        const auto& entry = m_Queue[i];

        // Split wrapped text into renderable lines
        std::vector<std::string> lines = SplitString2(entry.wrappedText, '\n');

        float blockHeight = (lines.size() * LINE_HEIGHT);
        float blockStartY = currentY - blockHeight + LINE_HEIGHT;

        for (size_t lineIdx = 0; lineIdx < lines.size(); ++lineIdx) {
            AbstractGame::DrawText2D(
                lines[lineIdx],
                0.5f,                                  // X (Center)
                blockStartY + (lineIdx * LINE_HEIGHT), // Y
                FONT_SCALE,
                255, 255, 255,                         // RGB
                (int)entry.alpha                       // Alpha
            );
        }
        // Move cursor up for the next message block
        currentY -= (blockHeight + 0.01f);
    }
}