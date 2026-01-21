
#include "main.h"

// for version Venators Enhanced Converstaions 0.9.0 or higher

struct NpcBrain {
	float happiness; // current happiness in the moment altogether
	int conversation_task_type = g_current_task_type; // the current task type he is in in conversation, importet from main.cpp
	float fear;  // current fear altogether
	// should we do this as an array so its doable for different other NPCs and then like personas?
	// so its like: npc: santa clause. fear: helper 1: 0, helper 2: 0, his magical rudolf: 0.1 -> rudolf can be aggressive
	// the higher the mood list factor , for each npc entity the current one got one
	std::string mood; // a word to describe the mood
	float energy; // how much energy he got for task to start
	// one person is one entity here
	EntityRegistry EnttiyIdToBrain; // the unique Entity ID and the conversation data for past conversations
	bool IsInConversation; // true or false
	std::string npc_current_task_state; //  what is currently doing
	std::string archetype; // some additioan info about archetype
	NpcPersona PersonaToBrain; // all the ini settings for the person for the LLM and TTS to know, like gender, traits, affiliation, gangs, ... . this is what the npc and also mainly the LLM INFERENCE GENERATOR works with when playing the npc 
	bool issaved; // when on true, the npc will be remembered and saved to a data file
	std::map<PersistID, float> relationships; // map for relationship to relationship, plus config reader data for that one
	std::vector<std::string> desirelist; // what he got in list, like "want to eat icrecream", ...
	
	// for conversation
	bool allowconversation;
	uint32_t MaxConvoManagerNodes = 1; // max 1 conversation at the time
		// following is only for later UE5 or similar port:
	std::string ragdollstate;
	bool hasRRdesire;
	bool hasRRdrug;
};

struct EntityManager { // always for each npc , must be mapped to array list later into same ram places, for mass enttiy
	uint16_t UniqueID;  // mandatory, hash or int
	NpcBrain Brain = nullptr; // only activate when required
	uint8_t state; // what submanager he is in. combat, talking, idle, scripted, blocked, scenario, custom, ...
	uint8_t bodystate; // the state his body is in. 
	uint8_t ragdollstate; // what he is doing. e.g. when he is falling, the manager knows that for the duration of hte falling, the AI manager does not need to do FOV or EQS checks -> less CPU weight
	Vec3 pos; // when req
	Vec3 rotvec; // when req
	float rotw; // when req
	bool AllowAIHandle; // AI Handle == CPP code that runs for each npc. if false only high level script will take, will T pose else
}

struct StateID {
	IDLE = 1;
	SCENARIO = 2;
	COMBAT = 3;
	SCRIPTED = 4;
	BLOCKED = 5;
	DEAD = 6;
}

// following are only active for each npc when required.
struct CombatManager {
	NpcCombatInfo = ConfigReader::NpcReader.CombatData;
	bool AllowCombat;
	float updatepriority;
	bool AutoAssignCombatAIHandle;
	uint8_t CombatState;
}

struct ScriptManager {
	bool AllowScript; // t / f
	bool IsInScript; // t / f
	uint8_t ReCheckTimerS; // recheck if script still ative timer
	bool DisableRR; // disables RR for time of script
}

struct ScenarioManager {
	bool AllowScenario;
	bool AutoAssignScenarioAIHandle;
	uint16_t ScenarioState;
}

struct BlockedManager {
	bool AllowBlocking; // true or false. if false, then this AI can not be blocked for the main handle
}

struct TransitionManager {
	bool BlockTransition;
}

struct SpawnManager {
	uint8_t updateintervalMS = 32;
}

struct DespawnManager {
	uint8_t updateintervalMS = 32;
}

struct SystemManager {
	uint8_t updateintervalMS = 16
	uint32_t maxentityperloop = 0x55FFFF
	uint8_T ThrowErrorRetries = 2;   uint8_t MaxBrainsAtSameTimeActive = 12; uint16_t MaxManagersAtSameTimeActive = 0xFFFFFF;
}

struct UpdateManager {
	uint16_t MaxGetWeightQueue value=0x0FFF;
	uint8_t MaxSameTimeInWeihtGetter=64;
	uint8_t PreferMaxSameTimeInWeightGetter = 12;
	uint8_t BaseUpdateRate = 32;
	uint8_t MaxUpdateRate = 8;
	uint8_t CombatUpdateMinBufferPerEntity = 100;
	uint8_t MainLoopMinBufferPerEntity = 32;
}






// EOF. das ist das system wie ich die AI haben wollte. 
