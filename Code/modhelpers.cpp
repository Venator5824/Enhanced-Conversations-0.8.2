#define _CRT_SECURE_NO_WARNINGS
#include "AbstractCalls.h"

using namespace AbstractGame;
using namespace AbstractTypes;
#include "main.h"
#include <fstream> 
#include <chrono> 
#include <iomanip> 
#include <ctime>
#include <string> 
#include "ModHelpers.h"



void LogHelpers(const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::ofstream log("kkamel.log", std::ios_base::app);
    if (log.is_open()) {
        log << "[" << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") << "] [ModHelpers] " << message << "\n";
        log.flush();
    }
}


void StartNpcConversationTasks(AHandle npc, AHandle player, int requestedType) {
    using namespace AbstractGame;

    LogHelpers("StartNpcConversationTasks: Request Type " + std::to_string(requestedType));

    if (!IsEntityValid(npc)) {
        LogHelpers("StartNpcConversationTasks: NPC Invalid.");
        return;
    }

    int finalType = requestedType;

    // --- AUTO-DETECTION LOGIC ---
    // Only run this if the API/User didn't force a specific behavior (e.g. didn't force 3 or 2)
    if (finalType <= 1) {

        // Check 1: Is this impossible? (Hard Stopper)
        if (IsPedInDenialTask(npc)) {
            LogHelpers("Auto-Detect: NPC in Denial Task (Combat/Fleeing). Aborting control.");
            finalType = 3; // Mode 3 = Hands off / Do nothing
        }
        // Check 2: Is this a soft task? (Soft Stopper)
        else if (IsPedInScriptTask(npc)) {
            LogHelpers("Auto-Detect: NPC in Script Task (Scenario/Phone). Switching to Mode 2.");
            finalType = 2; // Mode 2 = Maintain
        }
        // Check 3: Just walking/standing
        else {
            finalType = 1; // Mode 1 = Default
        }
    }

    g_current_task_type = finalType;

    // --- APPLY TASKS ---
    switch (finalType) {
        // MODE 3: EXTERNAL / DENIAL (Do Nothing)
    case 3:
        LogHelpers("Mode 3 Applied: No tasks.");
        break;

        // MODE 2: MAINTAIN (Soft Control)
    case 2:
        LogHelpers("Mode 2 Applied: Head tracking enabled, main task preserved.");
        SetPedConfigFlag(npc, 281, true); // Allow Head Turning
        SetPedConfigFlag(npc, 46, true);  // Allow Head Turning (Scripted)

        // Look at player (Head Only) -> Flag 2048 (Slow) | 4 (Head Only)
        TaskLookAtEntityAdvanced(npc, player, -1, 2048, 4);
        break;

        // MODE 1: DEFAULT (Hard Control)
    case 1:
    default:
        LogHelpers("Mode 1 Applied: Full control (Stop & Turn).");
        ClearTasks(npc);
        SystemWait(50);

        TaskTurnToFaceEntity(npc, player, 1000);
        SystemWait(50);

        // Look at player (Whole body/neck) -> Flag 0, Priority 2
        TaskLookAtEntityAdvanced(npc, player, -1, 0, 2);

        TaskStandStill(npc, -1);
        SetPedConfigFlag(npc, 281, true);
        break;
    }
}

void UpdateNpcConversationTasks(AHandle npc, AHandle player) {
    if (!IsEntityValid(npc)) {
        return;
    }

    if (g_current_task_type == 1) {
        TaskStandStill(npc, -1);
        TaskLookAtEntity(npc, player, -1);
    }

    if (g_current_task_type == 2) {
        TaskLookAtEntity(npc, player, -1);
    }

}


void Task_Behavior_Default(AHandle npc, AHandle player) {
    LogHelpers("Task_Behavior_Default: Taking full control.");

    // Your original logic
    ClearTasks(npc);
    AbstractGame::SystemWait(50);
    TaskLookAtEntity(npc, player, -1);
    AbstractGame::SystemWait(50);
    TaskStandStill(npc, -1);

    // Enable head turning
    AbstractGame::SetPedConfigFlag(npc, 281, true);
}

// [TYPE 2] MAINTAIN: Scenario/Sitting logic (Placeholders for now)
void Task_Behavior_Maintain(AHandle npc, AHandle player) {
    LogHelpers("Task_Behavior_Maintain: Keeping current scenario, enabling head look.");

    AbstractGame::SetPedConfigFlag(npc, 281, true);
}

// [TYPE 3] OVERTAKE/EXTERNAL: Hands-off approach
void Task_Behavior_External(AHandle npc, AHandle player) {
    LogHelpers("Task_Behavior_External: Loosening control for 3rd party script.");

    // We do NOTHING here physically. 
    // We do NOT clear tasks.
    // We do NOT force them to stand still.
    // This allows LSPDFR or another script to command the ped.
}


bool IsPedInDenialTask(AHandle ped) {
    using namespace AbstractGame;

    if (IsEntityDead(ped)) return true;
    if (IsEntityInCombat(ped)) return true;
    if (IsPedShooting(ped)) return true;
    if (IsEntityFleeing(ped)) return true;
    if (IsPedRagdoll(ped)) return true;
    if (IsPedFalling(ped)) return true;
    if (IsPedBeingArrested(ped)) return true;

    return false;
}


bool IsPedInScriptTask(AHandle ped) {
    using namespace AbstractGame;
    if (IsPedUsingScenario(ped)) return true;  
    if (IsPedUsingPhone(ped)) return true;   
    if (IsPedInCover(ped)) return true;   
    if (IsPedSitting(ped)) return true; 

    return false;
}

// EOF //