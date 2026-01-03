#pragma once
#include "AbstractTypes.h"

// Declarations for the NPC logic in ModHelpers.cpp
void StartNpcConversationTasks(AHandle npc, AHandle player, int current_task_type);
void UpdateNpcConversationTasks(AHandle npc, AHandle player);
//void UpdateNpcMemoryJanitor(bool force_clear_all);

bool IsPedInDenialTask(AHandle ped);

bool IsPedInScriptTask(AHandle ped);

enum class NpcBehaviour : int {
	DEFAULT = 0,
	MAINTAIN = 1,
	OVERTAKE = 2
};

// EOF //