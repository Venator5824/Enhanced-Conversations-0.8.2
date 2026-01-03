#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>

struct SubEntry {
    std::string speaker;
    std::string fullText;
    std::string wrappedText;
    uint32_t displayUntil;
    uint32_t lastUpdateTime;
    uint32_t creationTime;
    float alpha;
    int lineCount;

    // Audio Integration (RAM based for performance)
    std::vector<int16_t> pcmData;
    int sampleRate = 22050;
    bool playedAudio = false;
};

class SubtitleManager {
public:
    // Updated signature: takes PCM vector instead of a string path
    void ShowMessage(const std::string& name, const std::string& chunk,
        const std::vector<int16_t>& pcm = {}, int rate = 22050);

    void UpdateAndRender();

private:
    std::vector<SubEntry> m_Queue;
    std::mutex m_Mutex;

    std::string PerformWordWrap(const std::string& text, int& outLineCount);
    uint32_t CalculateDuration(const std::string& text);
};

// Global instance (typo fixed from g_Subtiltes)
extern SubtitleManager g_Subtitles;