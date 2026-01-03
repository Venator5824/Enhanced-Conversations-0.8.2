
#include "main.h"

// for version Venators Enhanced Converstaions 0.9.0 or higher

struct NpcBrain {
	float happiness; // current happiness in the moment altogether
	int conversation_task_type = g_current_task_type; // the current task type he is in in conversation, importet from main.cpp
	float fear;  // current fear altogether
	// should we do this as an array so its doable for different other NPCs and then like personas?
	// so its like: npc: santa clause. fear: helper 1: 0, helper 2: 0, his magical rudolf: 0.1 -> rudolf can be aggressive
	// the higher the mood list factor , for each npc entity the current one got one
	std::string mood; // a word to describe the mood
	float energy; // how much energy he got for task to start
	// one person is one entity here
	EntityRegistry EnttiyIdToBrain; // the unique Entity ID and the conversation data for past conversations
	bool IsInConversation; // true or false
	std::string npc_current_task_state; //  what is currently doing
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
	uint32_t ragdolltaskid;
	bool hasRRdesire;
	bool hasRRdrug;
};

// EOF