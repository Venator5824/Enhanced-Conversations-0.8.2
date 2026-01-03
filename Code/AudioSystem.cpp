#include "AudioSystem.h"
#include "AudioManager.h"
#include "babylon/babylon.h"
#include "main.h"
#include <cstring>
#include <algorithm>
#include <onnxruntime_cxx_api.h>
#include <onnxruntime_c_api.h>

std::unique_ptr<DeepPhonemizer::Session> AudioSystem::s_g2p_session = nullptr;
std::unique_ptr<ma_device> AudioSystem::s_device = nullptr;
bool AudioSystem::s_isInitialized = false;
std::mutex AudioSystem::s_generationMutex;

std::vector<int16_t> AudioSystem::s_audioBuffer;
float AudioSystem::s_playCursor = 0.0f;
float AudioSystem::s_resampleStep = 1.0f;
std::mutex AudioSystem::s_bufferMutex;
std::atomic<AudioState> AudioSystem::s_state(AudioState::UNINITIALIZED);




struct WAV_HEADER {
    char riff[4] = { 'R', 'I', 'F', 'F' };
    uint32_t overall_size;
    char wave[4] = { 'W', 'A', 'V', 'E' };
    char fmt_chunk_marker[4] = { 'f', 'm', 't', ' ' };
    uint32_t length_of_fmt = 16;
    uint16_t format_type = 1; // PCM
    uint16_t channels = 1;    // Mono 
    uint32_t sample_rate;
    uint32_t byterate;
    uint16_t block_align;
    uint16_t bits_per_sample = 16;
    char data_chunk_header[4] = { 'd', 'a', 't', 'a' };
    uint32_t data_size;
};


void WriteDebugWav(const std::string& filename, const std::vector<int16_t>& data, int sampleRate) {
    if (data.empty()) return;

    std::ofstream f(filename, std::ios::binary);
    if (!f.is_open()) return;

    WAV_HEADER header;
    header.sample_rate = sampleRate;
    header.bits_per_sample = 16;
    header.channels = 1;
    header.data_size = data.size() * sizeof(int16_t);
    header.overall_size = header.data_size + 36;
    header.block_align = header.channels * header.bits_per_sample / 8;
    header.byterate = header.sample_rate * header.block_align;

    f.write((char*)&header, sizeof(WAV_HEADER));
    f.write((char*)data.data(), header.data_size);
    f.close();

    Log("AudioSystem: [DEBUG] Saved " + filename + " (" + std::to_string(data.size()) + " samples)");
}

void ORT_API_CALL OnnxLogCallback(void* param, OrtLoggingLevel severity, const char* category,
    const char* logid, const char* code_location, const char* message);

void miniaudio_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    AudioSystem::OnAudioData((float*)pOutput, frameCount, pDevice->playback.channels);
}
bool AudioSystem::Initialize(const std::string& g2pModelPath, int hardwareRate) {
    if (s_isInitialized) return true;

    Log("AudioSystem: [STEP 1] Entering Initialize");
    Log("AudioSystem: [STEP 1.1] Path used: " + g2pModelPath);

    try {
        // --- TEST A: DATEI-ZUGRIFF ---
        if (!std::filesystem::exists(g2pModelPath)) {
            Log("AudioSystem: [ERROR] File does not exist!");
            return false;
        }
        Log("AudioSystem: [STEP 2] File exists check passed");

        // --- TEST B: BABYLON KONSTRUKTOR ---
        Log("AudioSystem: [STEP 3] Attempting to create DeepPhonemizer Session...");
        // Wir nutzen hier testweise den aller-simpelsten Aufruf
        s_g2p_session = std::make_unique<DeepPhonemizer::Session>(g2pModelPath);

        Log("AudioSystem: [STEP 4] DeepPhonemizer Session created successfully");

        // --- TEST C: MINIAUDIO ---
        Log("AudioSystem: [STEP 5] Initializing Miniaudio device...");
        s_device = std::make_unique<ma_device>();

        Log("AudioSystem: [STEP 6] ma_device allocated");
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = 2;
        config.sampleRate = hardwareRate;
        config.dataCallback = miniaudio_callback;

        Log("AudioSystem: [STEP 7] Calling ma_device_init...");
        if (ma_device_init(NULL, &config, s_device.get()) != MA_SUCCESS) {
            Log("AudioSystem: [ERROR] ma_device_init failed");
            return false;
        }

        Log("AudioSystem: [STEP 8] Calling ma_device_start...");
        if (ma_device_start(s_device.get()) != MA_SUCCESS) {
            Log("AudioSystem: [ERROR] ma_device_start failed");
            return false;
        }

        Log("AudioSystem: [STEP 9] Audio Engine started");
        s_state = AudioState::IDLE;
        s_isInitialized = true;
        return true;

    }
    catch (const std::exception& e) {
        Log("AudioSystem: [FATAL EXCEPTION] Standard: " + std::string(e.what()));
        return false;
    }
    catch (...) {
        Log("AudioSystem: [FATAL ERROR] Unknown crash during initialization!");
        return false;
    }
}

void AudioSystem::PlayBuffer(const std::vector<int16_t>& pcmData, int modelRate) {
    if (pcmData.empty()) return;
    std::lock_guard<std::mutex> lock(s_bufferMutex);

    s_audioBuffer = pcmData;
    s_playCursor = 0.0f;


    if (s_device) {
        s_resampleStep = (float)modelRate / (float)s_device->sampleRate;
    }
    s_state = AudioState::PLAYING;
}

std::vector<int16_t> AudioSystem::Generate(const std::string& text, Vits::Session* voiceSession, int speakerID, float speed, float noise, float noise_w) {
    // Sicherheitschecks
    if (!s_isInitialized || !voiceSession || text.empty()) return {};

    std::lock_guard<std::mutex> lock(s_generationMutex);
    try {
        // 1. Text zu Phonemen
        std::vector<std::string> phonemes = s_g2p_session->g2p(text);

        // 2. Phoneme zu Audio (PCM)
        std::vector<int16_t> pcm = voiceSession->tts_to_memory(phonemes, speakerID, speed, noise, noise_w);

        // --- DEBUG: SPEICHERN AUF FESTPLATTE ---
        if (!pcm.empty()) {
            static int counter = 0;
            std::string fname = "debug_tts_" + std::to_string(counter++ % 5) + ".wav";

            int rate = 22050;

            WriteDebugWav(fname, pcm, rate);
        }
        // ---------------------------------------
        return pcm;
    }
    catch (const std::exception& e) {
        Log("AudioSystem: [ERROR] Exception in Generate: " + std::string(e.what()));
        return {};
    }
    catch (...) {
        return {};
    }
}

void AudioSystem::OnAudioData(float* pOutput, size_t frameCount, int channels) {
    if (s_state != AudioState::PLAYING) {
        std::memset(pOutput, 0, frameCount * channels * sizeof(float));
        return;
    }

    std::lock_guard<std::mutex> lock(s_bufferMutex);
    for (size_t i = 0; i < frameCount; ++i) {
        float sampleVal = 0.0f;
        size_t idx = (size_t)s_playCursor;

        if (idx < s_audioBuffer.size()) {
            sampleVal = s_audioBuffer[idx] / 32768.0f;
            s_playCursor += s_resampleStep;
        }

        for (int c = 0; c < channels; ++c) pOutput[i * channels + c] = sampleVal;
    }

    if ((size_t)s_playCursor >= s_audioBuffer.size()) s_state = AudioState::IDLE;
}

void AudioSystem::Stop() {
    std::lock_guard<std::mutex> lock(s_bufferMutex);
    s_audioBuffer.clear();
    s_playCursor = 0.0f;
    s_state = AudioState::IDLE;
}

void AudioSystem::Shutdown() {
    Stop();
    if (s_device) {
        ma_device_uninit(s_device.get());
        s_device.reset(); 
    }
    s_g2p_session.reset();
    s_isInitialized = false;
    s_state = AudioState::UNINITIALIZED;
}

int AudioSystem::GetSampleRate() {
    if (s_isInitialized && s_device) {
        return s_device->sampleRate;
    }
    return 0;
}

bool AudioSystem::IsInitialized() { 
    return s_isInitialized; 
}

AudioState AudioSystem::GetState() { 
    return s_state.load(); 
}

void ORT_API_CALL OnnxLogCallback(void* param, OrtLoggingLevel severity, const char* category,
    const char* logid, const char* code_location, const char* message) {
    std::string levelStr;
    switch (severity) {
    case ORT_LOGGING_LEVEL_VERBOSE: levelStr = "[ORT-VERBOSE]"; break;
    case ORT_LOGGING_LEVEL_INFO:    levelStr = "[ORT-INFO]";    break;
    case ORT_LOGGING_LEVEL_WARNING: levelStr = "[ORT-WARN]";    break;
    case ORT_LOGGING_LEVEL_ERROR:   levelStr = "[ORT-ERROR]";   break;
    case ORT_LOGGING_LEVEL_FATAL:   levelStr = "[ORT-FATAL]";   break;
    default:                        levelStr = "[ORT-UNKNOWN]"; break;
    }
    // Schreibe direkt in dein bestehendes Log
    Log(levelStr + " (" + category + "): " + message);
}

///  EOF  ///