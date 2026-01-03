// OptChatMem.cpp
#include "main.h"
#include "AbstractCalls.h"
#include "llama.h" 
#include <thread>
#include <chrono>
#include <sstream>
#include "OptChatMem.h"
#include "ConfigReader.h" // Needed to read specific settings

using namespace AbstractGame;

// --- GLOBALS ---
static std::future<std::string> g_optimizationFuture;
static bool g_isOptimizing = false;
static int g_linesBeingSummarized = 0;
static std::map<ChatID, OptimizationProfile> g_profiles;

// ---------------------------------------------------------
// 1. VRAM CHECKER (Hardware Safety)
// ---------------------------------------------------------
float ChatOptimizer::GetAvailableVRAM_MB() {
    IDXGIFactory4* pFactory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&pFactory))) return 0.0f;

    IDXGIAdapter* pAdapter = nullptr;
    float freeVRAM = 0.0f;

    if (SUCCEEDED(pFactory->EnumAdapters(0, &pAdapter))) {
        IDXGIAdapter3* pAdapter3 = nullptr;
        if (SUCCEEDED(pAdapter->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&pAdapter3))) {
            DXGI_QUERY_VIDEO_MEMORY_INFO memInfo;
            if (SUCCEEDED(pAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memInfo))) {
                uint64_t available = memInfo.Budget - memInfo.CurrentUsage;
                freeVRAM = (float)(available / 1024 / 1024);
            }
            pAdapter3->Release();
        }
        pAdapter->Release();
    }
    pFactory->Release();
    return freeVRAM;
}

// ---------------------------------------------------------
// 2. MAIN CHECK LOGIC (When to Optimize)
// ---------------------------------------------------------
bool ChatOptimizer::CheckAndOptimize(ChatID chatID, std::vector<std::string>& history, const std::string& npcName, const std::string& playerName) {
    if (g_isOptimizing) return false;

    // 1. Check Settings
    int level = ConfigReader::g_Settings.Level_Optimization_Chat_Going;
    if (g_profiles.count(chatID)) level = g_profiles[chatID].level;
    if (level == 0) return false;

    size_t historySize = history.size();
    int triggerLineCount = 10;

    // 2. Auto-Leveling (VRAM Aware)
    if (level == 3) {
        float freeVram = GetAvailableVRAM_MB();
        // If VRAM is tight (< 2GB free), optimize sooner (Aggressive)
        if (freeVram > 0 && freeVram < 2000) level = 2;
        else level = 1;
    }

    if (level == 2) triggerLineCount = 6; // Aggressive mode triggers sooner

    if (historySize <= triggerLineCount) return false;

    // 3. Define Chunk (Keep System Prompt at 0, Summarize middle, Keep last 4 lines context)
    int startIdx = 1;
    int endIdx = (int)historySize - 4;

    if (endIdx <= startIdx) return false;

    std::vector<std::string> chunkToSummarize;
    for (int i = startIdx; i < endIdx; i++) {
        chunkToSummarize.push_back(history[i]);
    }

    g_linesBeingSummarized = (endIdx - startIdx);
    g_isOptimizing = true;

    // 4. Dynamic Throttle (Don't hog CPU)
    extern float g_current_tps;
    float currentTPS = g_current_tps;
    if (currentTPS < 5.0f) currentTPS = 5.0f;

    float targetSpeed = (level == 1) ? (currentTPS / 3.14f) : (currentTPS / 2.68f);
    if (targetSpeed < 10.0f) targetSpeed = 10.0f;
    if (targetSpeed > 45.0f) targetSpeed = 45.0f;

    int throttleInt = static_cast<int>(targetSpeed);

    // 5. Launch Background Task
    g_optimizationFuture = std::async(std::launch::async,
        &ChatOptimizer::BackgroundSummarizerTask,
        chunkToSummarize, npcName, playerName, throttleInt
    );

    return true;
}

// ---------------------------------------------------------
// 3. BACKGROUND WORKER (The Brain)
// ---------------------------------------------------------
std::string ChatOptimizer::BackgroundSummarizerTask(std::vector<std::string> lines, std::string npcName, std::string playerName, int throttleSpeed) {
    if (!g_model) return "";

    // === UPGRADED LOGIC: Using "Secretary" Prompt ===
    std::stringstream ss;
    ss << "<|system|>\n";
    ss << "You are a secretary. Summarize the following conversation segment between '" << npcName << "' and '" << playerName << "'.\n";
    ss << "RULES:\n";
    ss << "- Keep facts, names, and agreements.\n";
    ss << "- Remove chatter.\n";
    // Respect the user's specific size settings for memory chunks
    ss << "- Keep summary under " << ConfigReader::g_Settings.MAX_PCSREMEMBER_SIZE << " chars.\n";
    ss << "<|end|>\n";

    ss << "<|user|>\nCONVERSATION CHUNK:\n";
    for (const auto& line : lines) { ss << line << "\n"; }
    ss << "<|end|>\n<|assistant|>\n";

    std::string prompt = ss.str();
    // ================================================

    // 1. Setup Context
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = 2048; // Ensure enough space for the summarization task
    ctx_params.n_batch = 512;

    llama_context* ctx_sum = llama_init_from_model(g_model, ctx_params);
    if (!ctx_sum) return "";

    // 2. Tokenize
    const llama_vocab* vocab = llama_model_get_vocab(g_model);
    std::vector<llama_token> tokens_list(ctx_params.n_ctx);
    int32_t n_tokens = llama_tokenize(vocab, prompt.c_str(), (int32_t)prompt.length(), tokens_list.data(), (int32_t)tokens_list.size(), true, false);

    // 3. Batch Init
    llama_batch batch = llama_batch_init(512, 0, 1);
    std::string result = "";

    // 4. Fill Batch (Prefill)
    batch.n_tokens = n_tokens;
    for (int i = 0; i < n_tokens; i++) {
        batch.token[i] = tokens_list[i];
        batch.pos[i] = i;
        batch.n_seq_id[i] = 1;
        batch.seq_id[i][0] = 0;
        batch.logits[i] = (i == n_tokens - 1);
    }

    if (llama_decode(ctx_sum, batch) == 0) {
        int n_cur = n_tokens;
        int max_gen = 100; // Allow sufficient length for the summary
        int sleepMs = (throttleSpeed > 0) ? (1000 / throttleSpeed) : 0;

        for (int i = 0; i < max_gen; i++) {
            if (sleepMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));

            // Manual Greedy Sampling
            auto* logits = llama_get_logits_ith(ctx_sum, batch.n_tokens - 1);
            llama_token id = 0;
            float max_val = -1e9;
            int32_t n_vocab = llama_vocab_n_tokens(vocab);

            for (int v = 0; v < n_vocab; v++) {
                if (logits[v] > max_val) { max_val = logits[v]; id = v; }
            }

            if (llama_vocab_is_eog(vocab, id)) break;

            char buf[256];
            int n = llama_token_to_piece(vocab, id, buf, 256, 0, true);
            if (n > 0) result += std::string(buf, n);

            // Reset Batch
            batch.n_tokens = 1;
            batch.token[0] = id;
            batch.pos[0] = n_cur;
            batch.n_seq_id[0] = 1;
            batch.seq_id[0][0] = 0;
            batch.logits[0] = true;

            if (llama_decode(ctx_sum, batch) != 0) break;
            n_cur++;
        }
    }

    llama_batch_free(batch);
    llama_free(ctx_sum);
    return result;
}

// ---------------------------------------------------------
// 4. APPLY RESULT (Thread Safe Injection)
// ---------------------------------------------------------
bool ChatOptimizer::ApplyPendingOptimizations(std::vector<std::string>& history) {
    if (!g_isOptimizing) return false;

    // Non-blocking check
    if (g_optimizationFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::string summary = g_optimizationFuture.get();
        g_isOptimizing = false;

        if (summary.empty() || summary.length() < 5) return false;

        Log("OPTIMIZER: Applied intermediate summary: " + summary);
        int removeCount = g_linesBeingSummarized;

        // Safety check to ensure history hasn't changed drastically while we were working
        if (history.size() > removeCount + 1) {
            auto startIt = history.begin() + 1;
            auto endIt = startIt + removeCount;
            history.erase(startIt, endIt);

            // Insert the new summary block
            history.insert(history.begin() + 1, "<|system|>\n[INTERMEDIATE SUMMARY]: " + summary);
            return true;
        }
    }
    return false;
}

void ChatOptimizer::SetConversationProfile(ChatID chatID, int level) {
    g_profiles[chatID].level = level;
    g_profiles[chatID].isActive = true;
}

//EOF