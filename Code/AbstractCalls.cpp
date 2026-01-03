// AbstractCalls.cpp

#include "AbstractCalls.h"
#include "main.h" // <--- THE NEW INTERFACE
#include <cmath> 

// === KONFIGURATION ===



#define TARGET_GAME_GTAV

#ifdef TARGET_GAME_GTAV 
    // ScriptHookV Header / Natives
    // We assume your project includes nativesheader.h or similar via AbstractCalls.h or global settings
    // NO WINDOWS.H HERE!
    namespace HUD = UI;
#endif
namespace AbstractGame {

    // =============================================================
    // INTERNE HILFSFUNKTIONEN (Nur für diese Datei sichtbar)
    // =============================================================

#ifdef TARGET_GAME_GTAV
    inline int ToGtaID(AHandle h) {
        return static_cast<int>(h);
    }

    inline AVec3 ToAVec(Vector3 v) {
        return { v.x, v.y, v.z };
    }
#endif

    // =============================================================
    // 1. SYSTEM, ZEIT & INPUT
    // =============================================================

    AbstractTypes::TimeMillis GetTimeMs() {
        // [FIX] Abstraction: Using AOS
        return (AbstractTypes::TimeMillis)AOS::GetTimeMs();
    }

    void SystemWait(int ms) {
#ifdef TARGET_GAME_GTAV
        scriptWait(ms);
        //SYSTEM::WAIT(ms);
#else
        // Fallback for non-GTA platforms
        // AOS::SleepMs(ms); 
#endif
    }

    bool IsKeyPressed(int keyID) {
        // [FIX] Abstraction: Using AOS
        return AOS::IsKeyPressed(keyID);
    }

    bool IsGamePausedOrLoading() {
#ifdef TARGET_GAME_GTAV
        return GAMEPLAY::GET_MISSION_FLAG() || HUD::IS_PAUSE_MENU_ACTIVE();
#else
        return false;
#endif
    }

    // =============================================================
    // 2. ENTITY CORE
    // =============================================================

    AHandle GetPlayerHandle() {
#ifdef TARGET_GAME_GTAV
        return (AHandle)PLAYER::PLAYER_PED_ID();
#else
        return AbstractTypes::INVALID_HANDLE;
#endif
    }

    bool IsEntityValid(AHandle entity) {
        if (entity == AbstractTypes::INVALID_HANDLE) return false;
#ifdef TARGET_GAME_GTAV
        return ENTITY::DOES_ENTITY_EXIST(ToGtaID(entity));
#else
        return false;
#endif
    }

    bool IsEntityDead(AHandle entity) {
#ifdef TARGET_GAME_GTAV
        int id = ToGtaID(entity);
        if (ENTITY::DOES_ENTITY_EXIST(id)) {
            return PED::IS_PED_DEAD_OR_DYING(id, true);
        }
#endif
        return true;
    }

    // =============================================================
    // 3. MATHEMATIK & POSITION
    // =============================================================

    AVec3 GetEntityPosition(AHandle entity) {
#ifdef TARGET_GAME_GTAV
        int id = ToGtaID(entity);
        if (ENTITY::DOES_ENTITY_EXIST(id)) {
            Vector3 v = ENTITY::GET_ENTITY_COORDS(id, true);
            return ToAVec(v);
        }
#endif
        return { 0.0f, 0.0f, 0.0f };
    }

    float GetDistance(AVec3 a, AVec3 b) {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        float dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    float GetDistanceBetweenEntities(AHandle e1, AHandle e2) {
#ifdef TARGET_GAME_GTAV
        int idA = ToGtaID(e1);
        int idB = ToGtaID(e2);
        if (ENTITY::DOES_ENTITY_EXIST(idA) && ENTITY::DOES_ENTITY_EXIST(idB)) {
            Vector3 A = ENTITY::GET_ENTITY_COORDS(idA, true);
            Vector3 B = ENTITY::GET_ENTITY_COORDS(idB, true);
            return GAMEPLAY::GET_DISTANCE_BETWEEN_COORDS(A.x, A.y, A.z, B.x, B.y, B.z, true);
        }
#endif
        return 99999.9f;
    }

    // =============================================================
    // 4. LOGIK CHECKS
    // =============================================================

    bool IsEntityInCombat(AHandle entity) {
#ifdef TARGET_GAME_GTAV
        int id = ToGtaID(entity);
        if (ENTITY::DOES_ENTITY_EXIST(id)) return PED::IS_PED_IN_COMBAT(id, 0);
#endif
        return false;
    }

    bool IsEntitySwimming(AHandle entity) {
#ifdef TARGET_GAME_GTAV
        int id = ToGtaID(entity);
        if (ENTITY::DOES_ENTITY_EXIST(id)) return PED::IS_PED_SWIMMING(id);
#endif
        return false;
    }

    bool IsEntityJumping(AHandle entity) {
#ifdef TARGET_GAME_GTAV
        return PED::IS_PED_JUMPING(ToGtaID(entity));
#endif
        return false;
    }

    bool IsEntityFleeing(AHandle entity) {
#ifdef TARGET_GAME_GTAV
        return PED::IS_PED_FLEEING(ToGtaID(entity));
#endif
        return false;
    }

    bool IsEntityInVehicle(AHandle entity) {
#ifdef TARGET_GAME_GTAV
        return PED::IS_PED_IN_ANY_VEHICLE(ToGtaID(entity), false);
#endif
        return false;
    }

    AHandle GetVehicleOfEntity(AHandle entity) {
#ifdef TARGET_GAME_GTAV
        int id = ToGtaID(entity);
        if (PED::IS_PED_IN_ANY_VEHICLE(id, false)) {
            return (AHandle)PED::GET_VEHICLE_PED_IS_IN(id, false);
        }
#endif
        return AbstractTypes::INVALID_HANDLE;
    }

    bool IsEntityDriver(AHandle vehicle, AHandle entity) {
#ifdef TARGET_GAME_GTAV
        int vehID = ToGtaID(vehicle);
        int pedID = ToGtaID(entity);
        if (ENTITY::DOES_ENTITY_EXIST(vehID) && ENTITY::DOES_ENTITY_EXIST(pedID)) {
            return (VEHICLE::GET_PED_IN_VEHICLE_SEAT(vehID, -1) == pedID);
        }
#endif
        return false;
    }

    // =============================================================
    // 5. AI & TASKS
    // =============================================================

    AHandle GetClosestPed(AVec3 center, float radius, AHandle ignoreEntity) {
#ifdef TARGET_GAME_GTAV
        Ped outPed = 0;
        int ignoreID = ToGtaID(ignoreEntity);
        // Note: Passed true/true instead of BOOL for strict C++ correctness
        int found = PED::GET_CLOSEST_PED(center.x, center.y, center.z, radius, true, true, &outPed, true, true, 0);
        if (found && ENTITY::DOES_ENTITY_EXIST(outPed) && outPed != ignoreID) {
            return (AHandle)outPed;
        }
#endif
        return AbstractTypes::INVALID_HANDLE;
    }

    void ClearTasks(AHandle entity) {
#ifdef TARGET_GAME_GTAV
        int id = ToGtaID(entity);
        if (ENTITY::DOES_ENTITY_EXIST(id)) AI::CLEAR_PED_TASKS_IMMEDIATELY(id);
#endif
    }

    void TaskLookAtEntity(AHandle entity, AHandle target, int durationMs) {
#ifdef TARGET_GAME_GTAV
        int e = ToGtaID(entity);
        int t = ToGtaID(target);
        if (ENTITY::DOES_ENTITY_EXIST(e) && ENTITY::DOES_ENTITY_EXIST(t)) {
            AI::TASK_TURN_PED_TO_FACE_ENTITY(e, t, durationMs);
        }
#endif
    }

    void TaskStandStill(AHandle entity, int durationMs) {
#ifdef TARGET_GAME_GTAV
        int id = ToGtaID(entity);
        if (ENTITY::DOES_ENTITY_EXIST(id)) {
            AI::TASK_STAND_STILL(id, durationMs);
        }
#endif
    }

    void SetPedHeadTracking(AHandle entity, bool enabled) {
#ifdef TARGET_GAME_GTAV
        int id = ToGtaID(entity);
        if (ENTITY::DOES_ENTITY_EXIST(id)) {
            PED::SET_PED_CONFIG_FLAG(id, 281, enabled);
        }
#endif
    }

    // =============================================================
    // 6. UI & RENDERING
    // =============================================================

    void ShowSubtitle(const std::string& text, int durationMs) {
#ifdef TARGET_GAME_GTAV
        UI::_SET_TEXT_ENTRY((char*)"STRING");
        UI::_ADD_TEXT_COMPONENT_STRING((char*)text.c_str());
        UI::_DRAW_SUBTITLE_TIMED(durationMs, true);
#endif
    }

    void DrawText2D(const std::string& text, float x, float y, float scale, int r, int g, int b, int a) {
#ifdef TARGET_GAME_GTAV
        UI::SET_TEXT_FONT(0); // Standard GTA Font
        UI::SET_TEXT_SCALE(0.0f, scale);
        UI::SET_TEXT_COLOUR(r, g, b, a);
        UI::SET_TEXT_CENTRE(true);

        // Schatten und Outline für bessere Lesbarkeit vor hellem Hintergrund
        UI::SET_TEXT_DROPSHADOW(2, 0, 0, 0, 255);
        UI::SET_TEXT_EDGE(1, 0, 0, 0, 255);
        UI::SET_TEXT_OUTLINE();

        UI::_SET_TEXT_ENTRY((char*)"STRING");
        UI::_ADD_TEXT_COMPONENT_STRING((char*)text.c_str());

        // Zeichne den Text
        UI::_DRAW_TEXT(x, y);
#endif
    }

    // =============================================================
    // 7. KEYBOARD INPUT (Ingame OS)
    // =============================================================

    void OpenKeyboard(const std::string& title, const std::string& defaultText, int maxChars) {
#ifdef TARGET_GAME_GTAV
        GAMEPLAY::DISPLAY_ONSCREEN_KEYBOARD(1, (char*)title.c_str(), (char*)"", (char*)defaultText.c_str(), (char*)"", (char*)"", (char*)"", maxChars);
#endif
    }

    int UpdateKeyboardStatus() {
#ifdef TARGET_GAME_GTAV
        return GAMEPLAY::UPDATE_ONSCREEN_KEYBOARD();
#endif
        return 2;
    }

    std::string GetKeyboardResult() {
#ifdef TARGET_GAME_GTAV
        return std::string(GAMEPLAY::GET_ONSCREEN_KEYBOARD_RESULT());
#endif
        return "";
    }


    AbstractTypes::ModelID GetEntityModel(AHandle entity) {
#ifdef TARGET_GAME_GTAV
        int id = ToGtaID(entity);
        if (ENTITY::DOES_ENTITY_EXIST(id)) return ENTITY::GET_ENTITY_MODEL(id);
#endif
        return 0;
    }

    bool IsPedHuman(AHandle entity) {
#ifdef TARGET_GAME_GTAV
        return PED::IS_PED_HUMAN(ToGtaID(entity));
#endif
        return false;
    }

    bool IsPedMale(AHandle entity) {
#ifdef TARGET_GAME_GTAV
        return PED::IS_PED_MALE(ToGtaID(entity));
#endif
        return true;
    }


    uint32_t GetCurrentWeatherType() {
#ifdef TARGET_GAME_GTAV
        return GAMEPLAY::_GET_CURRENT_WEATHER_TYPE();
#endif
        return 0;
    }

    void GetGameTime(int& hour, int& minute) {
#ifdef TARGET_GAME_GTAV
        hour = TIME::GET_CLOCK_HOURS();
        minute = TIME::GET_CLOCK_MINUTES();
#endif
    }

    std::string GetZoneName(AVec3 pos) {
#ifdef TARGET_GAME_GTAV
        char* name = ZONE::GET_NAME_OF_ZONE(pos.x, pos.y, pos.z);
        return std::string(name ? name : "UNKNOWN");
#endif
        return "UNKNOWN";
    }

    void SetPedConfigFlag(AHandle entity, int flagId, bool value)
    {
#ifdef TARGET_GAME_GTAV
        // [FIX] Corrected to use parameters instead of hardcoded 281
        PED::SET_PED_CONFIG_FLAG(ToGtaID(entity), flagId, value);
#endif
    }

    bool IsEntityLivingEntity(AHandle tempTarget) {
#ifdef TARGET_GAME_GTAV
        return ENTITY::IS_ENTITY_A_PED(ToGtaID(tempTarget));
#endif
        return false;
    }

    bool IsPedUsingScenario(AHandle entity) {
#ifdef TARGET_GAME_GTAV
        int id = ToGtaID(entity);
        if (ENTITY::DOES_ENTITY_EXIST(id)) {
            return PED::IS_PED_USING_ANY_SCENARIO(id);
        }
#endif
        return false;
    }

    bool IsPedUsingPhone(AHandle entity) {
#ifdef TARGET_GAME_GTAV
        int id = ToGtaID(entity);
        if (ENTITY::DOES_ENTITY_EXIST(id)) {
            return PED::IS_PED_RUNNING_MOBILE_PHONE_TASK(id);
        }
#endif
        return false;
    }

    bool IsPedInCover(AHandle entity) {
#ifdef TARGET_GAME_GTAV
        int id = ToGtaID(entity);
        if (ENTITY::DOES_ENTITY_EXIST(id)) {
            return PED::IS_PED_IN_COVER(id, false);
        }
#endif
        return false;
    }

    bool IsPedRagdoll(AHandle entity) {
#ifdef TARGET_GAME_GTAV
        int id = ToGtaID(entity);
        if (ENTITY::DOES_ENTITY_EXIST(id)) {
            return PED::IS_PED_RAGDOLL(id);
        }
#endif
        return false;
    }

    bool IsPedFalling(AHandle entity) {
#ifdef TARGET_GAME_GTAV
        int id = ToGtaID(entity);
        if (ENTITY::DOES_ENTITY_EXIST(id)) {
            return PED::IS_PED_FALLING(id);
        }
#endif
        return false;
    }

    bool IsPedShooting(AHandle entity) {
#ifdef TARGET_GAME_GTAV
        int id = ToGtaID(entity);
        if (ENTITY::DOES_ENTITY_EXIST(id)) {
            return PED::IS_PED_SHOOTING(id);
        }
#endif
        return false;
    }

    bool IsPedBeingArrested(AHandle entity) {
#ifdef TARGET_GAME_GTAV
        int id = ToGtaID(entity);
        if (ENTITY::DOES_ENTITY_EXIST(id)) {
            return AI::IS_PED_BEING_ARRESTED(id);
        }
#endif
        return false;
    }

    bool IsPedSitting(AHandle entity) {
#ifdef TARGET_GAME_GTAV
        int id = ToGtaID(entity);
        if (ENTITY::DOES_ENTITY_EXIST(id)) {
            // Check vehicle sitting
            if (PED::IS_PED_SITTING_IN_ANY_VEHICLE(id)) return true;
            // Additional checks for bench sitting scenarios are covered by IsPedUsingScenario
        }
#endif
        return false;
    }

    // =============================================================
    // NEW ABSTRACTED TASKS
    // =============================================================

    void TaskTurnToFaceEntity(AHandle entity, AHandle target, int durationMs) {
#ifdef TARGET_GAME_GTAV
        int e = ToGtaID(entity);
        int t = ToGtaID(target);
        if (ENTITY::DOES_ENTITY_EXIST(e) && ENTITY::DOES_ENTITY_EXIST(t)) {
            AI::TASK_TURN_PED_TO_FACE_ENTITY(e, t, durationMs);
        }
#endif
    }

    void TaskClearLookAt(AHandle entity) {
#ifdef TARGET_GAME_GTAV
        int id = ToGtaID(entity);
        if (ENTITY::DOES_ENTITY_EXIST(id)) {
            AI::TASK_CLEAR_LOOK_AT(id);
        }
#endif
    }

    void TaskWander(AHandle entity, float radius, int flag) {
#ifdef TARGET_GAME_GTAV
        int id = ToGtaID(entity);
        if (ENTITY::DOES_ENTITY_EXIST(id)) {
            AI::TASK_WANDER_STANDARD(id, radius, flag);
        }
#endif
    }

    void TaskLookAtEntityAdvanced(AHandle entity, AHandle target, int durationMs, int flags, int priority) {
#ifdef TARGET_GAME_GTAV
        int e = ToGtaID(entity);
        int t = ToGtaID(target);
        if (ENTITY::DOES_ENTITY_EXIST(e) && ENTITY::DOES_ENTITY_EXIST(t)) {
            AI::TASK_LOOK_AT_ENTITY(e, t, durationMs, flags, priority);
        }
#endif
    }

}

//EOF