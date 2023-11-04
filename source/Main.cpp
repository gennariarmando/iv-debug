#include "plugin.h"
#include "debugmenu_public.h"

#include "CPlayerInfo.h"
#include "CClock.h"
#include "CWeather.h"
#include "CCheat.h"
#include "CVehicle.h"
#include "eModelHashes.h"
#include "CCamera.h"
#include "CPad.h"
#include "CPools.h"
#include "CPad.h"
#include "CWorld.h"
#include "CModelInfo.h"
#include "CVehicleFactoryNY.h"
#include "CStreaming.h"
#include "CFileType.h"
#include "CAutomobile.h"
#include "CHud.h"
#include "CMenuManager.h"

using namespace plugin;

DebugMenuAPI gDebugMenuAPI;

void (*DebugMenuInit)();
void (*DebugMenuProcess)();
void (*DebugMenuRender)();
void (*DebugMenuShutdown)();
bool (*DebugMenuShowing)();
void (*DebugMenuClear)();
void (*DebugMenuPrintString)(const char* str, float x, float y, int style);
int (*DebugMenuGetStringSize)(const char* str);

class DebugIV {
public:
    static inline bool debugCamera = false;
    static inline float fov = 45.0f;
    static inline bool godMode = false;
    static inline int32_t controlMode = 0;
    static inline rage::Vector4 teleportPos = {};
    static inline bool playerCoords = false;

    static void CallCheat(int32_t i) {
        static int32_t pattern = plugin::GetPattern("B3 01 EB 02 32 DB E8 ? ? ? ? 84 C0 0F 85", 1);
        plugin::patch::SetUChar(pattern, 0);
        auto func = CCheat::m_aCheatFunctions[i];
        if (func)
            func();
        else
            CCheat::m_aCheatsActive[i] = CCheat::m_aCheatsActive[i] == 0;


        plugin::patch::SetUChar(pattern, 1);
    }

    static CVehicle* SpawnVehicle(uint32_t hash) {
        auto playa = FindPlayerPed(0);
        CStreaming::ScriptRequestModel(hash, 0);
        CStreaming::LoadAllRequestedModels(0);

        CVehicle* veh = nullptr;
        if (CStreaming::ScriptHasModelLoaded(hash)) {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(hash, &index);

            rage::Matrix44 mat = {};
            mat.Copy(playa->m_pMatrix);
            mat.pos += playa->GetForward() * 10.0f;

            veh = GetVehicleFactory()->CreateVehicle(index, RANDOM_VEHICLE, &mat, 1);
            CWorld::Add(veh, 0);
        }
        return veh;
    }

    struct CameraSetting {
        rage::Vector3 pos;
        rage::Vector3 rot;
        int hour, minute;
        int oldweather, newweather;
    };

    static inline CameraSetting savedCameras[100];
    static inline int numSavedCameras;
    static inline int currentSavedCam;

    static void LoadSavedCams() {
        char line[256];
        CameraSetting* cs;
        FILE* f = fopen("campositions.txt", "r");
        if (f) {
            cs = savedCameras;
            numSavedCameras = 0;
            while (fgets(line, 256, f)) {
                sscanf(line, "%f %f %f  %f %f %f  %d %d  %d %d",
                    &cs->pos.x, &cs->pos.y, &cs->pos.z,
                    &cs->rot.x, &cs->rot.y, &cs->rot.z,
                    &cs->hour, &cs->minute,
                    &cs->oldweather, &cs->newweather);
                cs++;
                numSavedCameras++;
            }
            if (currentSavedCam >= numSavedCameras)
                currentSavedCam = 0;
            fclose(f);
        }
    }

    static void DeleteSavedCams() {
        FILE* f = fopen("campositions.txt", "w");
        if (f)
            fclose(f);
    }

    static void SaveCam(CCam* cam) {
        if (!cam)
            return;

        FILE* f = fopen("campositions.txt", "a");
        if (f) {
            fprintf(f, "%f %f %f  %f %f %f  %d %d  %d %d\n",
                cam->m_mMatrix.pos.x, cam->m_mMatrix.pos.y, cam->m_mMatrix.pos.z,
                cam->m_mMatrix.GetRotation().x, cam->m_mMatrix.GetRotation().y, cam->m_mMatrix.GetRotation().z,
                CClock::ms_nGameClockHours, CClock::ms_nGameClockMinutes,
                CWeather::OldWeatherType, CWeather::NewWeatherType);
            fclose(f);
            LoadSavedCams();
            currentSavedCam = numSavedCameras - 1;
        }
    }

    static void LoadCam(CCam* cam) {
        if (!cam)
            return;

        if (currentSavedCam < 0 || currentSavedCam >= numSavedCameras)
            return;

        cam->m_mMatrix.pos = savedCameras[currentSavedCam].pos;
        cam->m_mMatrix.SetRotate(savedCameras[currentSavedCam].rot);

        CClock::ms_nGameClockHours = savedCameras[currentSavedCam].hour;
        CClock::ms_nGameClockMinutes = savedCameras[currentSavedCam].minute;
        CWeather::OldWeatherType = savedCameras[currentSavedCam].oldweather;
        CWeather::NewWeatherType = savedCameras[currentSavedCam].newweather;
        CWeather::InterpolationValue = CClock::ms_nGameClockMinutes / 60.0f;
    }

    static void NextSavedCam(CCam* cam) {
        currentSavedCam++;
        if (currentSavedCam >= numSavedCameras) currentSavedCam = 0;
        LoadCam(cam);
    }

    static void PrevSavedCam(CCam* cam) {
        currentSavedCam--;
        if (currentSavedCam < 0) currentSavedCam = numSavedCameras - 1;
        LoadCam(cam);
    }

    static inline std::vector<std::string> vehicleNamesStrings = {};
    static inline std::vector<char*> vehicleNames;
    static void AddEntries() {
        // Time & Weather
        DebugMenuAddInt32("Time & Weather", "Current Hour", &CClock::ms_nGameClockHours, nullptr, 1, 0, 23, nullptr);
        DebugMenuAddInt32("Time & Weather", "Current Minute", &CClock::ms_nGameClockMinutes, []() {
            CWeather::InterpolationValue = CClock::ms_nGameClockMinutes / 60.0f + CClock::ms_nGameClockSeconds / 3600.0f;
        }, 1, 0, 59, nullptr);

        static const char* weathers[] = {
                "Extra Sunny",
                "Sunny",
                "Sunny Windy",
                "Cloudy",
                "Raining",
                "Drizzle",
                "Foggy",
                "Lightning",
                "Extra Sunny 2",
                "Sunny Windy 2",
        };
        DebugMenuAddInt32("Time & Weather", "Old Weather", &CWeather::OldWeatherType, nullptr, 1, 0, 9, weathers);
        DebugMenuAddInt32("Time & Weather", "New Weather", &CWeather::NewWeatherType, nullptr, 1, 0, 9, weathers);
        DebugMenuAddFloat32("Time & Weather", "Time scale", &CTimer::ms_fTimeScale, nullptr, 0.1f, 0.0f, 10.0f);

        // Spawn
        static int32_t vehId = 0;
        auto e = DebugMenuAddInt32("Spawn", "Spawn Vehicle ID", &vehId, nullptr, 1, 0, vehicleNames.size() - 1, (const char**)vehicleNames.data());
        DebugMenuEntrySetWrap(e, true);
        DebugMenuAddCmd("Spawn", "Spawn Vehicle", []() {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(rage::GetHashKey(vehicleNames[vehId], 0), &index);
            CCheat::VehicleCheat(index);
        });
        DebugMenuAddCmd("Spawn", "Spawn Turismo", []() {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(rage::GetHashKey("turismo", 0), &index);
            CCheat::VehicleCheat(index);
        });

        DebugMenuAddCmd("Spawn", "Spawn Comet", []() {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(rage::GetHashKey("comet", 0), &index);
            CCheat::VehicleCheat(index);
        });

        DebugMenuAddCmd("Spawn", "Spawn Voodoo", []() {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(rage::GetHashKey("voodoo", 0), &index);
            CCheat::VehicleCheat(index);
        });

        DebugMenuAddCmd("Spawn", "Spawn Rancher", []() {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(rage::GetHashKey("rancher", 0), &index);
            CCheat::VehicleCheat(index);
        });

        DebugMenuAddCmd("Spawn", "Spawn Admiral", []() {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(rage::GetHashKey("admiral", 0), &index);
            CCheat::VehicleCheat(index);
        });

        DebugMenuAddCmd("Spawn", "Spawn Stretch", []() {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(rage::GetHashKey("stretch", 0), &index);
            CCheat::VehicleCheat(index);
        });

        DebugMenuAddCmd("Spawn", "Spawn SuperGT", []() {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(rage::GetHashKey("supergt", 0), &index);
            CCheat::VehicleCheat(index);
        });

        DebugMenuAddCmd("Spawn", "Spawn Taxi", []() {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(rage::GetHashKey("taxi", 0), &index);
            CCheat::VehicleCheat(index);
        });

        DebugMenuAddCmd("Spawn", "Spawn Police", []() {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(rage::GetHashKey("police", 0), &index);
            CCheat::VehicleCheat(index);
        });

        // Cheats | Player
        DebugMenuAddCmd("Cheats|Player", "Weapons 1", []() {
            CallCheat(CHEAT_WEAPONS_1);
        });

        DebugMenuAddCmd("Cheats|Player", "Weapons 2", []() {
            CallCheat(CHEAT_WEAPONS_2);
        });

        if (gGameEpisode == EPISODE_TBOGT)
            DebugMenuAddCmd("Cheats|Player", "Weapons 3 (TBOGT)", []() {
                CallCheat(CHEAT_WEAPONS_3);
            });

        DebugMenuAddCmd("Cheats|Player", "Health Armour Ammo", []() {
            CallCheat(CHEAT_HEALTH_ARMOUR_AMMO);
        });

        DebugMenuAddCmd("Cheats|Player", "Increase Wanted Level", []() {
            CallCheat(CHEAT_INCREASE_WANTED_LEVEL);
        });

        DebugMenuAddCmd("Cheats|Player", "Clear Wanted Level", []() {
            CallCheat(CHEAT_CLEAR_WANTED_LEVEL);
        });

        DebugMenuAddCmd("Cheats|Player", "Parachute (TBOGT)", []() {
            CallCheat(CHEAT_PARACHUTE);
        });

        DebugMenuAddCmd("Cheats|Player", "Super Punch (TBOGT)", []() {
            CallCheat(CHEAT_SUPER_PUNCH);
        });

        DebugMenuAddCmd("Cheats|Player", "Explosive Sniper (TBOGT)", []() {
            CallCheat(CHEAT_EXPLOSIVE_SNIPER);
        });

        // Cheats | Spawn
        DebugMenuAddCmd("Cheats|Spawn", "Spawn Annihilator", []() {
            CallCheat(CHEAT_SPAWN_ANNIHILATOR);
        });

        DebugMenuAddCmd("Cheats|Spawn", "Spawn FCR900", []() {
            CallCheat(CHEAT_SPAWN_FCR900);
        });

        DebugMenuAddCmd("Cheats|Spawn", "Spawn FBIBuffalo", []() {
            CallCheat(CHEAT_SPAWN_FBIBUFFALO);
        });

        DebugMenuAddCmd("Cheats|Spawn", "Spawn JetMax", []() {
            CallCheat(CHEAT_SPAWN_JETMAX);
        });

        DebugMenuAddCmd("Cheats|Spawn", "Spawn Comet", []() {
            CallCheat(CHEAT_SPAWN_COMET);
        });

        DebugMenuAddCmd("Cheats|Spawn", "Spawn Turismo", []() {
            CallCheat(CHEAT_SPAWN_TURISMO);
        });

        DebugMenuAddCmd("Cheats|Spawn", "Spawn Cognoscenti", []() {
            CallCheat(CHEAT_SPAWN_COGNOSCENTI);
        });

        DebugMenuAddCmd("Cheats|Spawn", "Spawn Super GT", []() {
            CallCheat(CHEAT_SPAWN_SUPERGT);
        });

        DebugMenuAddCmd("Cheats|Spawn", "Spawn Sanchez", []() {
            CallCheat(CHEAT_SPAWN_SANCHEZ);
        });

        DebugMenuAddCmd("Cheats|Spawn", "Spawn Innovation (TLAD)", []() {
            CallCheat(CHEAT_SPAWN_INNOVATION);
        });

        DebugMenuAddCmd("Cheats|Spawn", "Spawn Hexer (TLAD)", []() {
            CallCheat(CHEAT_SPAWN_HEXER);
        });

        DebugMenuAddCmd("Cheats|Spawn", "Spawn Hakuchou (TLAD)", []() {
            CallCheat(CHEAT_SPAWN_HAKUCHOU_CUSTOM);
        });

        DebugMenuAddCmd("Cheats|Spawn", "Spawn Double T (TLAD)", []() {
            CallCheat(CHEAT_SPAWN_DOUBLE_T_CUSTOM);
        });

        DebugMenuAddCmd("Cheats|Spawn", "Spawn Gang Burrito (TLAD)", []() {
            CallCheat(CHEAT_SPAWN_GANG_BURRITO);
        });

        DebugMenuAddCmd("Cheats|Spawn", "Spawn Slamvan (TLAD)", []() {
            CallCheat(CHEAT_SPAWN_SLAMVAN);
        });

        DebugMenuAddCmd("Cheats|Spawn", "Spawn Floater (TBOGT)", []() {
            CallCheat(CHEAT_SPAWN_FLOATER);
        });

        DebugMenuAddCmd("Cheats|Spawn", "Spawn Akuma (TBOGT)", []() {
            CallCheat(CHEAT_SPAWN_AKUMA);
        });

        DebugMenuAddCmd("Cheats|Spawn", "Spawn Vader (TBOGT)", []() {
            CallCheat(CHEAT_SPAWN_VADER);
        });

        DebugMenuAddCmd("Cheats|Spawn", "Spawn APC (TBOGT)", []() {
            CallCheat(CHEAT_SPAWN_APC);
        });

        DebugMenuAddCmd("Cheats|Spawn", "Spawn Buzzard (TBOGT)", []() {
            CallCheat(CHEAT_SPAWN_BUZZARD);
        });

        DebugMenuAddCmd("Cheats|Spawn", "Spawn Bullet GT (TBOGT)", []() {
            CallCheat(CHEAT_SPAWN_BULLETGT);
        });

        // Cheats | Time & Weather
        DebugMenuAddCmd("Cheats|Time & Weather", "Change Weather", []() {
            CCheat::ChangeWeatherCheat();
        });

        // Player
        static const char* boolstr[] = { "Off", "On" };
        e = DebugMenuAddInt8("Player", "Invincible", (int8_t*)&godMode, nullptr, 1, 0, 1, boolstr);
        DebugMenuEntrySetWrap(e, true);
        DebugMenuAddCmd("Player", "Clear Wanted Level", []() { FindPlayerPed(0)->m_pPlayerInfo->m_PlayerData.m_Wanted.m_nWantedLevel = 0; });
        DebugMenuAddCmd("Player", "Give Money", []() { FindPlayerPed(0)->m_pPlayerInfo->m_nMoney += 10000; });
        DebugMenuAddCmd("Player", "Remove Money", []() { FindPlayerPed(0)->m_pPlayerInfo->m_nMoney -= 10000; });

        // Debug
        e = DebugMenuAddCmd("Debug|Camera", "Toggle Debug Camera", []() {
            ToggleDebugCam();
        });
        DebugMenuEntrySetWrap(e, true);

        static const char* controlStr[2] = { "Camera", "Player" };
        e = DebugMenuAddVar("Debug|Camera", "Debug Camera Control", &controlMode, []() {
            ToggleControls(controlMode == 0);
        }, 1, 0, 1, controlStr);
        DebugMenuEntrySetWrap(e, true);
        DebugMenuAddVar("Debug", "FOV", &fov, nullptr, 1.0f, 5.0f, 180.0f);
        DebugMenuAddCmd("Debug", "Save Camera Position", []() { SaveCam(TheCamera.m_pCamGame); });
        DebugMenuAddCmd("Debug", "Cycle Next", []() { NextSavedCam(TheCamera.m_pCamGame); });
        DebugMenuAddCmd("Debug", "Cycle Prev", []() { PrevSavedCam(TheCamera.m_pCamGame); });
        DebugMenuAddCmd("Debug", "Delete Camera Positions", DeleteSavedCams);

        DebugMenuAddVarBool8("Debug", "Disable HUD", &CHud::HideAllComponents, nullptr);
        DebugMenuAddVarBool8("Debug", "Show Player Coords", &playerCoords, nullptr);

        // Misc
        DebugMenuAddCmd("Misc", "Teleport to Waypoint", []() {
            auto i = CRadar::GetActualBlipArrayIndex(CMenuManager::m_nWaypointIndex);
        
            if (i != 0) {
                auto trace = CRadar::ms_RadarTrace[i];
        
                if (trace) {
                    rage::Vector3 blipPos = CRadar::ms_RadarTrace[i]->m_vPos;
                    rage::Vector4 pos;
                    pos.x = blipPos.x;
                    pos.y = blipPos.y;
                    pos.z = blipPos.z;
        
                    pos.z = CWorld::FindGroundZForCoord(pos.x, pos.y);
                    auto playa = FindPlayerPed(0);
                    playa->Teleport(&pos, 0, 1);
                }
            }
        });

        static const char* interiorNameList[] = {
            "Broker Safehouse",
            "Comrades Bar",
            "Brucie's Garage",
            "South Bohan Safehouse",
            "Playboy X's Penthouse",
            "Luis Lopez Safehouse"
        };

        static rage::Vector3 interiorPosList[] = {
            { 892.0f, -496.0f, 19.0f },
            { 930.0f, -492.0f, 16.0f },
            { 868.0f, -122.0f, 6.0f },
            { 602.0f, 1410.0f, 17.5f },
            { -426.0f, 1460.0f, 39.0f },
            { -434.0f, 1394.0f, 16.5f },
        };

        static int32_t interiorId = 0;
        DebugMenuAddInt32("Misc", "Locations", &interiorId, nullptr, 1, 0, plugin::array_size(interiorNameList) - 1, interiorNameList);
        DebugMenuAddCmd("Misc", "Teleport to Location", []() {
            auto playa = FindPlayerPed(0);
            if (playa) {
                rage::Vector4 pos = interiorPosList[interiorId];
                playa->Teleport(&pos, 0, 1);
            }
        });
    }

    static void ToggleDebugCam() {
        CCam* cam = TheCamera.m_pCamFollowVeh;

        if (!cam)
            cam = TheCamera.m_pCamFollowPed;

        if (!cam)
            return;

        LoadSavedCams();
        debugCamera ^= 1;
        cam->m_bActive = !debugCamera;
        ToggleControls(debugCamera && controlMode == 0);
    }

    static void ToggleControls(bool disabled) {
        auto playa = FindPlayerPed(0);
        auto cam = TheCamera.m_pCamGame;
        if (cam) {
            if (playa) {
                playa->m_pPlayerInfo->m_bDisableControls = disabled;
                playa->m_pPlayerInfo->m_PlayerData.m_Wanted.m_bIgnoredByEveryone = disabled;
            }
        }
    }

    static void ProcessDebugCam() {
        if (!debugCamera)
            return;

        if (controlMode == 1)
            return;

        ToggleControls(controlMode == 0);

        bool controlsEnabled = !DebugMenuShowing();
        bool forward = CPad::KeyboardMgr.IsKeyPressed(eKeyCodes::KEY_W, 2, 0);
        bool backward = CPad::KeyboardMgr.IsKeyPressed(eKeyCodes::KEY_S, 2, 0);
        bool left = CPad::KeyboardMgr.IsKeyPressed(eKeyCodes::KEY_A, 2, 0);
        bool right = CPad::KeyboardMgr.IsKeyPressed(eKeyCodes::KEY_D, 2, 0);
        bool control = CPad::KeyboardMgr.IsKeyPressed(eKeyCodes::KEY_LCONTROL, 2, 0) || CPad::KeyboardMgr.IsKeyPressed(eKeyCodes::KEY_RCONTROL, 2, 0);
        bool enter = CPad::KeyboardMgr.IsKeyJustPressed(eKeyCodes::KEY_RETURN, 2, 0);
        bool up = CPad::KeyboardMgr.IsKeyPressed(eKeyCodes::KEY_UP, 2, 0);
        bool down = CPad::KeyboardMgr.IsKeyPressed(eKeyCodes::KEY_DOWN, 2, 0);
        bool lmb = CPad::IsMouseButtonPressed(1);
        int32_t mouseX = 0, mouseY = 0;
        CPad::GetMouseInput(&mouseX, &mouseY);

        auto cam = TheCamera.m_pCamGame;
        if (!cam)
            return;

        rage::Matrix44 mat = cam->m_mMatrix;
        rage::Vector3 rot = cam->m_mMatrix.GetRotation();

        static float speed = 0.0f;
        static float panspeedX = 0.0f;
        static float panspeedY = 0.0f;

        float rotationSpeed = 0.0005f;

        rage::Vector3 angle = cam->m_mMatrix.GetRotation();

        if (controlsEnabled) {
            if (lmb) {
                angle.z -= mouseX * rotationSpeed;
                angle.x -= mouseY * rotationSpeed;
            }

            if (forward)
                speed += 0.01f;
            else if (backward)
                speed -= 0.01f;
            else
                speed = 0.0f;

            if (left)
                panspeedX -= 0.01f;
            else if (right)
                panspeedX += 0.01f;
            else
                panspeedX = 0.0f;

            if (down)
                panspeedY -= 0.01f;
            else if (up)
                panspeedY += 0.01f;
            else
                panspeedY = 0.0f;

            if (control) {
                if (speed > 0.25f) speed = 0.25f;
                if (speed < -0.25f) speed = -0.25f;
            
                if (panspeedX > 0.25f) panspeedX = 0.25f;
                if (panspeedX < -0.25f) panspeedX = -0.25f;
            
                if (panspeedY > 0.25f) panspeedY = 0.25f;
                if (panspeedY < -0.25f) panspeedY = -0.25f;
            }
            else {
                if (speed > 50.0f) speed = 50.0f;
                if (speed < -50.0f) speed = -50.0f;
            
                if (panspeedX > 50.0f) panspeedX = 50.0f;
                if (panspeedX < -50.0f) panspeedX = -50.0f;
            
                if (panspeedY > 50.0f) panspeedY = 50.0f;
                if (panspeedY < -50.0f) panspeedY = -50.0f;
            }
        }

        mat.SetRotate(angle);

        if (controlsEnabled) {
            mat.pos += mat.up * speed;
            mat.pos += mat.right * panspeedX;
            mat.pos += mat.at * panspeedY;
        }

        cam->m_fFOV = fov;
        cam->m_fHintFOV = fov;
        cam->m_mMatrix = mat;
        teleportPos = mat.pos;

        auto playa = FindPlayerPed(0);
        auto vehicle = FindPlayerVehicle(0);
        if (enter) {
            if (vehicle)
                vehicle->Teleport(&teleportPos, 0, 1);
            else
                playa->Teleport(&teleportPos, 0, 1);
        }
    }
    DebugIV() {
        // Init DebugMenu
        if (DebugMenuLoad()) {
            DebugMenuInit = (void(*)())GetProcAddress(gDebugMenuAPI.module, "DebugMenuInit");
            DebugMenuProcess = (void(*)())GetProcAddress(gDebugMenuAPI.module, "DebugMenuProcess");
            DebugMenuRender = (void(*)())GetProcAddress(gDebugMenuAPI.module, "DebugMenuRender");
            DebugMenuShutdown = (void(*)())GetProcAddress(gDebugMenuAPI.module, "DebugMenuShutdown");
            DebugMenuShowing = (bool(*)())GetProcAddress(gDebugMenuAPI.module, "DebugMenuShowing");
            DebugMenuClear = (void(*)())GetProcAddress(gDebugMenuAPI.module, "DebugMenuClear");
            DebugMenuPrintString = (void(*)(const char*, float, float, int))GetProcAddress(gDebugMenuAPI.module, "DebugMenuPrintString");
            DebugMenuGetStringSize = (int(*)(const char*))GetProcAddress(gDebugMenuAPI.module, "DebugMenuGetStringSize");
        }

        plugin::Events::initEngineEvent += []() {

        };

        plugin::Events::initGameEvent.before += []() {
            vehicleNamesStrings = {};
            vehicleNames = {};
        };

        plugin::Events::initGameEvent.after += []() {
            for (auto& it : vehicleNamesStrings) {
                vehicleNames.push_back((char*)it.c_str());
            }

            DebugMenuClear();
            AddEntries();
        };

        plugin::Events::drawHudEvent += []() {
            if (playerCoords) {
                auto base = new T_CB_Generic([]() {
                    char buf[64];
                    auto playa = FindPlayerPed(0);
                    sprintf(buf, "%3f, %3f, %3f", playa->GetPosition().x, playa->GetPosition().y, playa->GetPosition().z);
                    DebugMenuPrintString(buf, 0.0f, 0.0f, 0);
                    CFont::DrawFonts();
                });
                base->Append();
            }
        };

        plugin::Events::gameProcessEvent += []() {
            if (CMenuManager::m_MenuActive)
                return;

            if ((CPad::KeyboardMgr.IsKeyPressed(eKeyCodes::KEY_LCONTROL, 2, 0) ||
                CPad::KeyboardMgr.IsKeyPressed(eKeyCodes::KEY_RCONTROL, 2, 0)) &&
                CPad::KeyboardMgr.IsKeyJustPressed(eKeyCodes::KEY_B, 2, 0))
                ToggleDebugCam();

            auto playa = FindPlayerPed(0);
            playa->m_bInvincible = godMode;

            ProcessDebugCam();
        };

        CdeclEvent <AddressList<0x9D9EC1, H_CALL>, PRIORITY_AFTER, ArgPickN<char*, 0>, CBaseModelInfo*(char*)> onItemDefCars;
        onItemDefCars.SetRefAddr(plugin::GetPattern("E8 ? ? ? ? 8B F0 83 C4 04 8D 44 24 78", 0));
        onItemDefCars += [](char* modelName) {
            vehicleNamesStrings.push_back(modelName);
        };

    }
} debugIV;
