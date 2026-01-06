# EC Audio Info

*"For Custom TTS (ElevenLabs/Azure): Call StartConversation with allow_TTS=false. Retrieve the text via API_GetLastResponse and pipe it to your external TTS API in your C# script."*


# Audio System Guide
## Enhanced Conversations Mod (GTA V)

This document explains how the audio engine works, how to add new local voice models, and how to integrate external high-quality TTS (like ElevenLabs) via scripts.

---

### 1. The Built-in System (Local TTS)
By default, this mod uses **Babylon.cpp** (a VITS/Piper engine) to generate speech.
*   **Pros:** Zero latency (instant response), works offline, free, low performance cost.
*   **Cons:** Voices can sound mechanical/robotic compared to cloud AI.
*   **Format:** The mod uses heavily modified `.onnx` files.

---

### 2. Adding New Local Voices
**IMPORTANT:** You cannot simply download a standard Piper `.onnx` model and drop it in. It will crash the game. You must convert it first using the included Python script.

#### Step A: Download & Convert
1.  Download a VITS/Piper model (you need the `.onnx` file and the `.json` config).
2.  Go to: `Grand Theft Auto V/ECmod/EC_binaries/`
3.  Open `piper_to_babylon.py` with a text editor.
4.  Edit the top lines to match your downloaded filenames:
    ```python
    onnx_file_path = './your_downloaded_model.onnx'
    config_file_path = './your_downloaded_model.onnx.json'
    # ...
    onnx.save(onnx_model, "./my_converted_model.onnx")
    ```
5.  Run the script: `python piper_to_babylon.py`
6.  Take the resulting **converted** `.onnx` file.

#### Step B: Install the File
Move the converted file to: `Grand Theft Auto V/ECmod/AudioModels/`
*(You can create subfolders here to organize voice packs).*

#### Step C: Register the Voice
You must tell the mod the voice exists by adding it to an `.ini` list.
Location: `Grand Theft Auto V/ECmod/AudioDataFiles/`
File: Any file starting with `EC_Voices_list_` (e.g., `EC_Voices_list_Custom.ini`).

**INI Structure:**
```ini
; Define the folder where your models are stored
model_path=root/ECMod/AudioModels/

[1]
; Filename of the CONVERTED model
path=my_converted_model.onnx
; Gender (m/f) is mandatory for auto-assignment
gender=f
; Set to 1 to enable
enable=1
; Set to 1 if you DON'T want random pedestrians using this voice
is_restricted=0
```

---

### 3. For Developers: Using External TTS (ElevenLabs, Azure)
If you are a C# developer or creating an LSPDFR plugin and want ultra-realistic voices (ElevenLabs, XTTS, Azure), you can bypass the mod's internal audio engine entirely.

**The Workflow:**
1.  Disable the mod's internal TTS for the specific conversation.
2.  Wait for the text generation to finish.
3.  Retrieve the text string.
4.  Send the text to your external API (e.g., ElevenLabs) via your C# script.
5.  Play the resulting MP3/WAV using NAudio or GTA natives.

**C# Implementation Example:**

```csharp
// 1. Start the conversation with allow_TTS = false
// This tells the DLL to generate text but NOT generate audio/visemes
EC_Wrapper.API_StartConversation(targetPed.Handle, null, "Context...", 1, false);

// 2. In your update loop, wait for the AI to finish "thinking"
if (!EC_Wrapper.API_IsBusy()) 
{
    StringBuilder buffer = new StringBuilder(2048);
    
    // 3. Retrieve the generated text
    if (EC_Wrapper.API_GetLastResponse(buffer, 2048))
    {
        string aiResponse = buffer.ToString();
        
        // 4. Send 'aiResponse' to ElevenLabs API here...
        // 5. Play the returned audio file...
    }
}
```

This method allows Streamers and Content Creators to have high-quality audio while still using the mod's local LLM logic, memory, and conversation management.

---

### 4. Troubleshooting
*   **Game Crashes on Load:** You likely tried to load a raw Piper model without converting it via the Python script.
*   **NPC is Silent:** The `.onnx` file might be missing from the path defined in the `.ini`, or `enable=0` is set.
*   **Voices sound static/noisy:** This is a limitation of the current VITS engine to ensure it runs fast enough for real-time gameplay without lag.