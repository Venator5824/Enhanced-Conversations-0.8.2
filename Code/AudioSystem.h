#pragma once
#include "PlatformSystem.h"
#include "main.h"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>

// Forward declarations
namespace DeepPhonemizer { class Session; }
namespace Vits { class Session; }
struct ma_device;
enum OrtLoggingLevel;
enum class AudioState { UNINITIALIZED, IDLE, PLAYING };

class AudioSystem {
public:
    static bool Initialize(const std::string& g2pModelPath, int hardwareRate = 22050);
    static void Shutdown();
    static int GetSampleRate();
    static std::vector<int16_t> Generate(
        const std::string& text,
        Vits::Session* voiceSession,
        int speakerID = 0,
        float speed = 1.0f,
        float noise = 0.667f,
        float noise_w = 0.8f
    );
    void OnnxLogCallback(void* param, OrtLoggingLevel severity, const char* category,         const char* logid, const char* code_location, const char* message);
    
    static void PlayBuffer(const std::vector<int16_t>& pcmData, int modelRate = 22050);
    static void Stop();

    static bool IsInitialized();
    static AudioState GetState();
    static void OnAudioData(float* pOutput, size_t frameCount, int channels);

private:
    static std::unique_ptr<DeepPhonemizer::Session> s_g2p_session;
    static std::unique_ptr<ma_device> s_device;
    static std::mutex s_generationMutex;
    static bool s_isInitialized;

    static std::vector<int16_t> s_audioBuffer;
    static float s_playCursor;      // Geändert auf float für Resampling
    static float s_resampleStep;    // Neu hinzugefügt
    static std::mutex s_bufferMutex;
    static std::atomic<AudioState> s_state;
};