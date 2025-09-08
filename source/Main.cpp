#include "plugin.h"
#include "debugmenu_public.h"

#include "T_CB_Generic.h"
#include "CFont.h"
#include "CPlayerInfo.h"
#include "CClock.h"
#include "CWeather.h"
#include "CCheat.h"
#include "CVehicle.h"
#include "eModelHashes.h"
#include "CCamera.h"
#include "CPad.h"
#include "CPools.h"
#include "CWorld.h"
#include "CModelInfo.h"
#include "CVehicleFactoryNY.h"
#include "CPedFactoryNY.h"
#include "CStreaming.h"
#include "CFileType.h"
#include "CAutomobile.h"
#include "CHud.h"
#include "CMenuManager.h"
#include "CRadar.h"
#include "CTheScripts.h"
#include "CText.h"
#include "CControlMgr.h"
#include "CCutsceneMgr.h"
#include <map>
#include "extensions\ScriptCommands.h"
#include "CWeaponInfo.h"
#include "CViewport.h"
#include "CTxdStore.h"

#include "Timer.h"

#include "dxsdk/d3d9.h"


DebugMenuAPI gDebugMenuAPI;

bool (*DebugMenuShowing)();
void (*DebugMenuPrintString)(const char* str, float x, float y, int style);
int (*DebugMenuGetStringSize)(const char* str);

class DebugIV {
public:
    static inline bool debugCamera = false;
    static inline float fov = 45.0f;
    static inline bool godMode = false;
	static inline bool infiniteAmmo = false;
    static inline bool noReload = false;

    static inline bool pedsIgnorePlayer = false;
    static inline bool vehicleGodMode = false;
    static inline bool neverWanted = false;

    static inline int32_t controlMode = 0;
    static inline rage::Vector4 teleportPos = {};
    static inline bool playerCoords = false;
    static inline bool drawCameraOverlay = false;
    static inline bool drawCamMode = false;
    
    static inline int32_t debugMessageTimer = 0;
    static inline std::string debugMessage = {};

    static inline plugin::Timer timer = {};

    static void SetDebugMessage(std::string const& str) {
        debugMessage.assign(str);

        debugMessageTimer = timer.GetTimeInMilliseconds() + 1500;
    }

    static void DrawDebugMessage() {
        if (debugMessage.empty())
            return;

        if (debugMessageTimer < timer.GetTimeInMilliseconds()) {
            debugMessage.clear();
            return;
        }

        float x = DebugMenuGetStringSize(debugMessage.c_str()) / 2;
        x = (SCREEN_WIDTH / 2) - x;
        DebugMenuPrintString(debugMessage.c_str(), x, SCREEN_HEIGHT - 100.0f, 0);
    }

    static void CallCheat(int32_t i) {
        static uint32_t pattern = plugin::pattern::Get("B3 01 EB 02 32 DB E8 ? ? ? ? 84 C0 0F 85", 1);
        plugin::patch::SetUChar(pattern, 0);
        auto func = CCheat::m_aCheatFunctions[i];
        if (func)
            func();
        else
            CCheat::m_aCheatsActive[i] = CCheat::m_aCheatsActive[i] == 0;

        plugin::patch::SetUChar(pattern, 1);
    }

    static CPed* SpawnPed(uint32_t hash) {
        auto playa = FindPlayerPed(0);
        CStreaming::ScriptRequestModel(hash, 0);
        CStreaming::LoadAllRequestedModels(0);

        CPed* ped = nullptr;
        if (CStreaming::ScriptHasModelLoaded(hash)) {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(hash, &index);

            rage::Matrix44 mat = {};
            mat.Copy(playa->m_pMatrix);
            mat.pos += playa->GetForward() * 10.0f;

            CControlledByInfo info = { 0, 0, 0 };
            ped = CPedFactoryNY::GetInstance()->CreatePed(&info, index, &mat, 1, 1);
            ped->m_nRelationshipGroup = RELATIONSHIP_GROUP_CRIMINAL;
            CWorld::Add(ped, 0);
        }
        return ped;
    }

    static void GiveWeapon(CPed* ped, int32_t hash, int32_t ammo) {
        uint32_t index = 0;
        CModelInfo::GetModelByHash(hash, &index);

        int8_t weapType = -1;
        for (uint32_t i = 0; i < NUM_WEAPONTYPES; i++) {
            auto weap = CWeaponInfo::GetWeaponInfo(i);

            if (weap->m_nModelHash == hash) {
                weapType = i;
                break;
            }
        }

        if (weapType == -1)
            return;

        CStreaming::RequestModel(index, CFileTypeMgr::IndexOfType_WDR, 2);
        CStreaming::LoadAllRequestedModels(0);
        ped->m_WeaponData.GiveWeapon((eWeaponType)weapType, ammo, 1, 1, 1);
        CStreaming::SetIsModelDeletable(index, CFileTypeMgr::IndexOfType_WDR);
    }

    static void GiveWeapon(CPed* ped, eWeaponType weapType, int32_t ammo) {
        uint32_t index = 0;
        CModelInfo::GetModelByHash(CWeaponInfo::GetWeaponInfo(weapType)->m_nModelHash, &index);

        CStreaming::RequestModel(index, CFileTypeMgr::IndexOfType_WDR, 2);
        CStreaming::LoadAllRequestedModels(0);
        ped->m_WeaponData.GiveWeapon(weapType, ammo, 1, 1, 1);
        CStreaming::SetIsModelDeletable(index, CFileTypeMgr::IndexOfType_WDR);
    }

    static void SpawnGroupOfEnemy() {
        for (int32_t i = 0; i < 10; i++) {
            CPed* ped = SpawnPed(MODEL_HASH_M_Y_STREET_01);
            ped->m_pMatrix->pos.x += (i - 5) * 3.0f;
            ped->m_pMatrix->pos.y += (i - 5) * 3.0f;

            GiveWeapon(ped, WEAPONTYPE_AK47, 1000);

            plugin::scripting::CallCommandById<void>(plugin::Commands::BLOCK_PED_WEAPON_SWITCHING, CPools::GetPedRef(ped), false);
            plugin::scripting::CallCommandById<void>(plugin::Commands::SET_CHAR_ACCURACY, CPools::GetPedRef(ped), 100);

            plugin::scripting::CallCommandById<void>(
                plugin::Commands::TASK_COMBAT,
                CPools::GetPedRef(ped),
                CPools::GetPedRef(FindPlayerPed(0)));
        }
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
       
            veh = CVehicleFactoryNY::GetInstance()->CreateVehicle(index, RANDOM_VEHICLE, &mat, 1);
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
        if (f) {
            fclose(f);

            SetDebugMessage("Camera positions deleted");
            numSavedCameras = 0;
            currentSavedCam = 0;
        }
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

            SetDebugMessage("Camera position saved");
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
        if (currentSavedCam >= numSavedCameras)
            currentSavedCam = 0;
        LoadCam(cam);

        if (numSavedCameras == 0)
            SetDebugMessage("No camera positions found");
        else
            SetDebugMessage("Next camera position loaded");
    }

    static void PrevSavedCam(CCam* cam) {
        currentSavedCam--;
        if (currentSavedCam < 0) 
            currentSavedCam = numSavedCameras - 1;
        LoadCam(cam);

        if (numSavedCameras == 0)
            SetDebugMessage("No camera positions found");
        else
            SetDebugMessage("Previous camera position loaded");
    }

    static inline std::vector<std::string> vehicleNamesStrings = {};
    static inline std::vector<char*> vehicleNames;

    struct MissionList {
        char scriptName[32];
		char fullName[256];
        int32_t stackSize;
	};

    static inline MissionList missionListTLAD[] = {
        { "opening_credits", "Clean and Serene", 1024 },
        { "Billy2", "Angels in America", 1024 },
        { "Billy3", "It's War", 1024 },
        { "Billy4", "Action/Reaction", 1024 },
        { "Billy5", "Buyer's Market", 1024 },
        { "Billy6", "Buyer's Market", 1024 },
        { "Jim1", "Liberty City Choppers", 1024 },
        { "Jim2", "Bad Cop Drop", 1024 },
        { "Jim4", "Hit The Pipe", 1024 },
        { "Jim5", "End Of Chapter", 1024 },
        { "Jim6", "Bad Standing", 1024 },
        { "Stubbs1", "Politics", 1024 },
        { "Stubbs2", "Off Route", 1024 },
        { "Stubbs3p", "Proc Odd Jobs", 1024 },
        { "Stubbs4", "Get Lost", 1024 },
        { "E1EndCredits", "TLAD Complete", 1024 },
        { "Ashley1", "Coming Down", 1024 },
        { "Ashley2", "Roman's Holiday", 1024 },
        { "Elizabeta1", "Heavy Toll", 1024 },
        { "Elizabeta2", "Marta Full Of Grace", 1024 },
        { "Elizabeta3", "Shifting Weight", 1024 },
        { "Ray1", "Diamonds In The Rough", 1024 },
        { "Ray2", "Collector's Item", 1024 },
        { "Ray3", "Was It Worth it?", 1024 },
        { "Jim3p", "I Want One Of Those", 1024 },
    };

    static inline MissionList missionListTBOGT[] = {
        { "opening_credits", "I Luv LC", 1024 },
        { "tony2", "Chinese Takeout", 1024 },
        { "tony3", "Practice Swing", 1024 },
        { "tony4a", "Blog This!...", 1024 },
        { "tony5", "Bang Bang", 1024 },
        { "tony4b", "...Blog This!", 1024 },
        { "tony6", "Boulevard Baby", 1024 },
        { "tony7", "Frosting On The Cake", 1024 },
        { "tony8", "Not So Fast", 1024 },
        { "tony9", "Ladies' Night", 1024 },
        { "tony10", "Ladies Half Price", 1024 },
        { "tony11", "Departure Time", 1024 },
        { "E2EndCredits", "TBoGT Complete", 1024 },
        { "brother1", "Kibbutz Number One", 1024 },
        { "brother2", "This Ain't Checkers", 1024 },
        { "brother3", "No. 3", 1024 },
        { "yusuf1", "Sexy Time", 1024 },
        { "yusuf2", "High Dive", 1024 },
        { "yusuf3", "Caught With Your Pants Down", 1024 },
        { "yusuf4", "For The Man Who Has Everything", 1024 },
        { "friends1", "Corner Kids", 1024 },
        { "friends2", "Clocking Off", 1024 },
        { "mum1", "Momma's Boy", 1024 },
        { "bulgarin1", "Going Deep", 1024 },
        { "bulgarin2", "Dropping In", 1024 },
        { "bulgarin3", "In The Crosshairs", 1024 },
        { "rocco1", "Party's Over", 1024 },
    };

    static inline MissionList missionListIV[] = {
        { "initial", "The Cousins Bellic", 1024 },
        { "Roman2", "It's Your Call", 1024 },
        { "Roman3", "Three's a Crowd", 1024 },
        { "Roman4", "Bleed out", 1024 },
        { "Roman5", "Easy Fare", 1024 },
        { "Roman6", "Jamaican Heat", 1024 },
        { "Roman7", "Uncle Vlad", 1024 },
        { "Roman8p", "Taxi Mission", 1024 },
        { "Roman9", "Logging On", 1024 },
        { "Roman10p", "Steal Banshee for Brucie", 1024 },
        { "Roman11", "Roman's Sorrow", 1024 },
        { "Roman12", "Hostile Negotiation", 1024 },
        { "Michelle1", "First Date", 1024 },
        { "Vlad1", "Bull in a China Shop", 1024 },
        { "Vlad2", "Hung out to Dry", 1024 },
        { "Vlad3", "Clean Getaway", 1024 },
        { "Vlad4", "Ivan the Not So Terrible", 1024 },
        { "Jacob1", "Concrete Jungle", 1024 },
        { "Jacob2", "Shadow", 1024 },
        { "Jacob3p", "Drug delivery Mission", 1024 },
        { "Faustin1", "Crime and Punishment", 1024 },
        { "Faustin2", "Do You Have Protection?", 1024 },
        { "Faustin3", "Final Destination", 1024 },
        { "Faustin4", "No Love Lost", 1024 },
        { "Faustin5", "Rigged to Blow", 1024 },
        { "Faustin6", "The Master and the Molotov (Dimitri)", 1024 },
        { "Faustin7", "Russian Revolution (Dimitri)", 1024 },
        { "Brucie1", "Search and Delete", 1024 },
        { "Brucie2", "Easy as Can Be", 1024 },
        { "Brucie3a", "Out of the Closet", 1024 },
        { "Brucie4", "Nr. 1", 1024 },
        { "Manny1", "Escuela of the Streets", 1024 },
        { "Manny2", "Street Sweeper", 1024 },
        { "Manny3", "The Puerto Rican Connection", 1024 },
        { "Elizabeta1", "Luck of the Irish", 1024 },
        { "Elizabeta2", "Blow your Cover", 1024 },
        { "Elizabeta3", "The Snow Storm", 1024 },
        { "Elizabeta4", "Have a Heart", 1024 },
        { "Playboy2", "Deconstruction for Beginners", 1024 },
        { "Playboy3", "Photo Shoot", 1024 },
        { "Playboy4", "The Holland Play", 1024 },
        { "Dwayne1", "Ruff Rider", 1024 },
        { "Dwayne3", "Undress to Kill", 1024 },
        { "Francis1", "Call and Collect", 1024 },
        { "Francis2a", "Final Interview", 1024 },
        { "Francis3", "Holland Nights", 1024 },
        { "Francis4", "Lure", 1024 },
        { "Francis5", "Blood Brothers", 1024 },
        { "Packie1", "Harboring a Grudge", 1024 },
        { "Packie2", "Waste Not Want Knots", 1024 },
        { "Packie3", "Three Leaf Clover", 1024 },
        { "UlPaper1", "Wrong is Right", 1024 },
        { "UlPaper2", "Portrait of a Killer", 1024 },
        { "UlPaper3", "Dust off", 1024 },
        { "UlPaper4", "Paper Trial", 1024 },
        { "Ray1", "A long Way to Fall", 1024 },
        { "Ray2", "Taking in the Trash", 1024 },
        { "Ray3", "Meltdown", 1024 },
        { "Ray4", "Museum Piece", 1024 },
        { "Ray5", "No Way on the Subway", 1024 },
        { "Ray6", "Late Checkout", 1024 },
        { "Bernie1", "Hating the Haters", 1024 },
        { "Bernie2", "Union Drive", 1024 },
        { "Bernie3", "Buoys Ahoy", 1024 },
        { "Gerry1", "Action Speak Louder Than Words", 1024 },
        { "Gerry2", "I need your Clothes, Your Boots and Your Motorcycle", 1024 },
        { "Gerry3", "Unknown Mission 1", 1024 },
        { "Gerry4", "Unknown Mission 2", 1024 },
        { "Gerry5", "Unknown Mission 3", 1024 },
        { "Derrick1", "Smackdown", 1024 },
        { "Derrick2", "Babysitting", 1024 },
        { "Derrick3", "Tunnel of Death", 1024 },
        { "Phil1", "Catch the Wave", 1024 },
        { "Phil2", "Kill mission", 1024 },
        { "Phil3", "Trespass", 1024 },
        { "Phil4", "Truck Hustle", 1024 },
        { "Phil5", "To Live and Die in Alderney", 1024 },
        { "Jimmy1", "Unknown Mission 1", 1024 },
        { "Jimmy2", "Payback", 1024 },
        { "Jimmy3", "Unknown Mission 3", 1024 },
        { "Jimmy4", "Pest Control", 1024 },
        { "Gambetti1", "Entourage", 1024 },
        { "Gambetti2", "Dining Out", 1024 },
        { "Gambetti3", "Liquidize the Assests", 1024 },
        { "Finale1a", "One Last Thing", 1024 },
        { "Finale2", "A Dish served cold", 1024 },
        { "Finale3", "Wedding", 1024 },
        { "Finale5", "Out of Commission", 1024 }
    };

    static inline int32_t scriptId = 0;
    static inline std::vector<char*> scriptNames = {};
    static inline int32_t vehicleId = 0;
    static inline int32_t pedId = 0;
    static inline std::vector<char*> pedNames;
    static inline std::vector<std::string> pedNamesStrings;
    static inline std::vector<std::string> weaponNamesStrings = {};
    static inline std::vector<char*> weaponNames;
    static inline int32_t weapId = 0;
    static inline bool freezeTime = false;

    static void ChangePlayerComponent(ePedVarComp slotId, uint32_t model, uint32_t texture = 0) {
        auto playa = FindPlayerPed(0);
		int32_t ref = CPools::GetPedRef(playa);

        plugin::scripting::CallCommandById<void>(
            plugin::Commands::SET_CHAR_COMPONENT_VARIATION,
			ref, slotId, model, texture);
    }

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

        static float farDOF = 0.0f;
        DebugMenuAddFloat32("Time & Weather", "m_fFarDOF", &farDOF, []() {
            CCam* cam = TheCamera.m_pCamFollowVeh;
            if (cam) {
                if (farDOF < 0.0f)
                    farDOF = cam->m_fFarDOF;

                cam->m_fFarDOF = farDOF;
            }
        }, 0.1f, 0.0f, 100.0f);

        static float nearDOF = 0.0f;
        DebugMenuAddFloat32("Time & Weather", "m_fNearDOF", &nearDOF, []() {
            CCam* cam = TheCamera.m_pCamFollowVeh;
            if (cam) {
                if (nearDOF < 0.0f)
                    nearDOF = cam->m_fNearDOF;

                cam->m_fNearDOF = nearDOF;
            }
        }, 0.1f, 0.0f, 100.0f);

        static const char* boolstr[] = { "Off", "On" };
        auto e = DebugMenuAddInt8("Time & Weather", "Freeze Game", (int8_t*)&freezeTime, []() { CTimer::m_UserPause = false; }, 1, 0, 1, boolstr);
        DebugMenuEntrySetWrap(e, true);

        // Spawn | Ped
        e = DebugMenuAddInt32("Spawn|Ped", "Spawn Ped ID", &pedId, nullptr, 1, 0, pedNames.size() - 1, (const char**)pedNames.data());
        DebugMenuEntrySetWrap(e, true);
        DebugMenuAddCmd("Spawn|Ped", "Spawn Ped", []() {
            SpawnPed(rage::atStringHash(pedNames[pedId], 0));
        });

        DebugMenuAddCmd("Spawn|Ped", "Spawn Group of Enemies", []() {
            SpawnGroupOfEnemy();
        });

        // Spawn | Vehicle
        e = DebugMenuAddInt32("Spawn|Vehicle", "Spawn Vehicle ID", &vehicleId, nullptr, 1, 0, vehicleNames.size() - 1, (const char**)vehicleNames.data());
        DebugMenuEntrySetWrap(e, true);
        DebugMenuAddCmd("Spawn|Vehicle", "Spawn Vehicle", []() {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(rage::atStringHash(vehicleNames[vehicleId], 0), &index);
            CCheat::VehicleCheat(index);
        });
        DebugMenuAddCmd("Spawn|Vehicle", "Spawn Turismo", []() {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(rage::atStringHash("turismo", 0), &index);
            CCheat::VehicleCheat(index);
        });

        DebugMenuAddCmd("Spawn|Vehicle", "Spawn Comet", []() {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(rage::atStringHash("comet", 0), &index);
            CCheat::VehicleCheat(index);
        });

        DebugMenuAddCmd("Spawn|Vehicle", "Spawn Voodoo", []() {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(rage::atStringHash("voodoo", 0), &index);
            CCheat::VehicleCheat(index);
        });

        DebugMenuAddCmd("Spawn|Vehicle", "Spawn Rancher", []() {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(rage::atStringHash("rancher", 0), &index);
            CCheat::VehicleCheat(index);
        });

        DebugMenuAddCmd("Spawn|Vehicle", "Spawn Admiral", []() {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(rage::atStringHash("admiral", 0), &index);
            CCheat::VehicleCheat(index);
        });

        DebugMenuAddCmd("Spawn|Vehicle", "Spawn Stretch", []() {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(rage::atStringHash("stretch", 0), &index);
            CCheat::VehicleCheat(index);
        });

        DebugMenuAddCmd("Spawn|Vehicle", "Spawn SuperGT", []() {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(rage::atStringHash("supergt", 0), &index);
            CCheat::VehicleCheat(index);
        });

        DebugMenuAddCmd("Spawn|Vehicle", "Spawn Taxi", []() {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(rage::atStringHash("taxi", 0), &index);
            CCheat::VehicleCheat(index);
        });

        DebugMenuAddCmd("Spawn|Vehicle", "Spawn Police", []() {
            uint32_t index = 0;
            CModelInfo::GetModelByHash(rage::atStringHash("police", 0), &index);
            CCheat::VehicleCheat(index);
        });

        // Cheats | Player
        DebugMenuAddCmd("Cheats|Player", "Weapons 1", []() {
            CallCheat(CHEAT_WEAPONS_1);
        });

        DebugMenuAddCmd("Cheats|Player", "Weapons 2", []() {
            CallCheat(CHEAT_WEAPONS_2);
        });

        DebugMenuAddCmd("Cheats|Player", "Weapons 3 (TBOGT)", []() {
            if (CMenuManager::m_Episode  == EPISODE_TBOGT)
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
        e = DebugMenuAddInt8("Player", "Invincible", (int8_t*)&godMode, []() { FindPlayerPed(0)->m_bInvincible = false; }, 1, 0, 1, boolstr);
        DebugMenuEntrySetWrap(e, true);
        e = DebugMenuAddInt8("Player", "Infinite Ammo", (int8_t*)&infiniteAmmo, nullptr, 1, 0, 1, boolstr);
        DebugMenuEntrySetWrap(e, true);
        e = DebugMenuAddVarBool8("Player", "No reload", &noReload, nullptr);

        DebugMenuEntrySetWrap(e, true);

        e = DebugMenuAddInt8("Player", "Peds ignore Player", (int8_t*)&pedsIgnorePlayer, []() {
            FindPlayerPed(0)->m_pPlayerInfo->m_PlayerData.m_Wanted.m_EverybodyBackOff = false;
        }, 1, 0, 1, boolstr);
        DebugMenuEntrySetWrap(e, true);

        DebugMenuAddCmd("Player", "Clean clothes", []() {
            plugin::scripting::CallCommandById<void>(plugin::Commands::RESET_VISIBLE_PED_DAMAGE, CPools::GetPedRef(FindPlayerPed(0))); 
            SetDebugMessage("Clothes cleaned");
        });

        DebugMenuAddCmd("Player", "Clear Wanted Level", []() { FindPlayerPed(0)->m_pPlayerInfo->m_PlayerData.m_Wanted.m_WantedLevel = eWantedLevel::WANTED_CLEAN; SetDebugMessage("Wanted level clear"); });
        DebugMenuAddVarBool8("Player", "Never Wanted", &neverWanted, []() {
            if (neverWanted)
                SetDebugMessage("Never Wanted: On");
            else
                SetDebugMessage("Wanted level: Off");
        });

        DebugMenuAddCmd("Player", "Give Money", []() { FindPlayerPed(0)->m_pPlayerInfo->m_nMoney += 10000; });
        DebugMenuAddCmd("Player", "Remove Money", []() { FindPlayerPed(0)->m_pPlayerInfo->m_nMoney -= 10000; });

        static int32_t playerHead = 0;
		static int32_t playerUppr = 0;
		static int32_t playerLowr = 0;
		static int32_t playerAccs = 0;
        static int32_t playerHand = 0;
        static int32_t playerFeet = 0;
        static int32_t playerJacket = 0;
        static int32_t playerHair = 0;
        static int32_t playerDecl = 0;
        static int32_t playerTeeth = 0;
        static int32_t playerFace = 0;

        DebugMenuAddInt32("Player|Clothes", "Head", &playerHead, []() { ChangePlayerComponent(ePedVarComp::PV_COMP_HEAD, playerHead); }, 1, 0, 32, nullptr);
        DebugMenuAddInt32("Player|Clothes", "Upper Body", &playerUppr, []() { ChangePlayerComponent(ePedVarComp::PV_COMP_UPPR, playerUppr); }, 1, 0, 32, nullptr);
        DebugMenuAddInt32("Player|Clothes", "Lower Body", &playerLowr, []() { ChangePlayerComponent(ePedVarComp::PV_COMP_LOWR, playerLowr); }, 1, 0, 32, nullptr);
        DebugMenuAddInt32("Player|Clothes", "Accessories", &playerAccs, []() { ChangePlayerComponent(ePedVarComp::PV_COMP_ACCS, playerAccs); }, 1, 0, 32, nullptr);
        DebugMenuAddInt32("Player|Clothes", "Hand", &playerHand, []() { ChangePlayerComponent(ePedVarComp::PV_COMP_HAND, playerHand); }, 1, 0, 32, nullptr);
        DebugMenuAddInt32("Player|Clothes", "Feet", &playerFeet, []() { ChangePlayerComponent(ePedVarComp::PV_COMP_FEET, playerFeet); }, 1, 0, 32, nullptr);
        DebugMenuAddInt32("Player|Clothes", "Jacket", &playerJacket, []() { ChangePlayerComponent(ePedVarComp::PV_COMP_JBIB, playerJacket); }, 1, 0, 32, nullptr);
        DebugMenuAddInt32("Player|Clothes", "Hair", &playerHair, []() { ChangePlayerComponent(ePedVarComp::PV_COMP_HAIR, playerHair); }, 1, 0, 32, nullptr);
        DebugMenuAddInt32("Player|Clothes", "Decals", &playerDecl, []() { ChangePlayerComponent(ePedVarComp::PV_COMP_DECL, playerDecl); }, 1, 0, 32, nullptr);
        DebugMenuAddInt32("Player|Clothes", "Teeth", &playerTeeth, []() { ChangePlayerComponent(ePedVarComp::PV_COMP_TEETH, playerTeeth); }, 1, 0, 32, nullptr);
        DebugMenuAddInt32("Player|Clothes", "Face", &playerFace, []() { ChangePlayerComponent(ePedVarComp::PV_COMP_FACE, playerFace); }, 1, 0, 32, nullptr);

        // Weapon
        e = DebugMenuAddInt32("Weapon", "Get Weapon ID", &weapId, nullptr, 1, 0, weaponNames.size() - 1, (const char**)weaponNames.data());
        DebugMenuEntrySetWrap(e, true);
        DebugMenuAddCmd("Weapon", "Get Weapon", []() {
            auto playa = FindPlayerPed(0);
            if (playa)
                GiveWeapon(playa, rage::atStringHash(weaponNames[weapId], 0), 1000);
        });


        // Vehicle
        DebugMenuAddCmd("Vehicle", "Fix", []() { 
            auto vehicle = FindPlayerVehicle(0);
            if (vehicle) {
                vehicle->Fix();
                vehicle->SetHealth(1000.0f, 0);

                SetDebugMessage("Vehicle fixed");
            }
        });

        DebugMenuAddCmd("Vehicle", "Flip", []() {
            auto vehicle = FindPlayerVehicle(0);
            if (vehicle) {
                rage::Vector4 rot = vehicle->m_pMatrix->GetRotation();
                rot.y -= rage::pi();
                vehicle->m_pMatrix->SetRotate(rot);
                vehicle->Teleport(&vehicle->GetPosition(), 0, 1);

                SetDebugMessage("Vehicle flipped");
            }
        });

        DebugMenuAddVarBool8("Vehicle", "Invincible", &vehicleGodMode, []() { if (vehicleGodMode && FindPlayerVehicle(0)) FindPlayerVehicle(0)->Fix(); });

        DebugMenuAddCmd("Vehicle", "Clean vehicle", []() { if (FindPlayerVehicle(0)) { FindPlayerVehicle(0)->m_fDirtLevel = 0.0f; SetDebugMessage("Vehicle cleaned"); } });

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
        DebugMenuAddCmd("Debug", "Save Camera Position", []() { SaveCam(TheCamera.m_pCamFinal); });
        DebugMenuAddCmd("Debug", "Cycle Next", []() { NextSavedCam(TheCamera.m_pCamFinal); });
        DebugMenuAddCmd("Debug", "Cycle Prev", []() { PrevSavedCam(TheCamera.m_pCamFinal); });
        DebugMenuAddCmd("Debug", "Delete Camera Positions", DeleteSavedCams);

        DebugMenuAddVarBool8("Debug", "Disable HUD", &CHud::HideAllComponents, nullptr);
        DebugMenuAddVarBool8("Debug", "Show Player Coords", &playerCoords, nullptr);
        DebugMenuAddVarBool8("Debug", "Show Cam Mode", &drawCamMode, nullptr);

        DebugMenuAddVarBool8("Debug", "Draw Camera Overlay", &drawCameraOverlay, []() {
            if (drawCameraOverlay)
                CCutsceneMgr::LoadSprites();
            else
                CCutsceneMgr::UnloadSprites();
            });

        static int8_t episodeId = 0;
        DebugMenuAddVar("Debug", "Episode Id", &episodeId, []() {
            char buf[8];
            sprintf(buf, "%d", episodeId);
            
            gGameEpisode = episodeId;
            CMenuManager::m_Episode = episodeId;
            CMenuManager::m_EpisodeStr = buf;
            CMenuManager::m_EpisodeToStart = episodeId;
        }, 1, 0, 126, nullptr);

        // Misc
        DebugMenuAddCmd("Misc", "Teleport to Waypoint", []() {
            auto i = CRadar::GetActualBlipArrayIndex(CMenuManager::m_nWaypointIndex);
        
            if (i != 0) {
                auto trace = CRadar::ms_RadarTrace[i];
        
                if (trace) {
                    auto playa = FindPlayerPed(0);
                    rage::Vector3 blipPos = CRadar::ms_RadarTrace[i]->m_vPos;
                    rage::Vector4 pos;
                    pos.x = blipPos.x;
                    pos.y = blipPos.y;
                    pos.z = 1000.0f;
        
                    pos.z = CWorld::FindGroundZForCoord(pos.x, pos.y);
                    if (pos.z == 0.0f)
                        pos.z = playa->GetPosition().z;

                    playa->Teleport(&pos, 0, 1);

                    SetDebugMessage("Teleported to waypoint");
                }
            }
        });

        DebugMenuAddCmd("Misc", "Save game", []() {
            plugin::scripting::CallCommandById<void>(plugin::Commands::ACTIVATE_SAVE_MENU);
        });

        DebugMenuAddCmd("Misc", "Prostitute Cam", []() {
            CCamFollowVehicle::bProstituteCam ^= true;
        });

        e = DebugMenuAddInt32("Script", "Mission select", &scriptId, nullptr, 1, 0, scriptNames.size() - 1, (const char**)scriptNames.data());
        DebugMenuEntrySetWrap(e, true);

        DebugMenuAddCmd("Script", "Start mission script", []() {
            const char* name = missionListIV[scriptId].scriptName;
            int32_t stackSize = missionListIV[scriptId].stackSize;

            if (CMenuManager::m_Episode  == EPISODE_TLAD) {
                name = missionListTLAD[scriptId].scriptName;
                stackSize = missionListTLAD[scriptId].stackSize;
            }
            else if (CMenuManager::m_Episode  == EPISODE_TBOGT) {
                name = missionListTBOGT[scriptId].scriptName;
                stackSize = missionListTBOGT[scriptId].stackSize;
            }

            int32_t index = CTheScripts::GetScriptIndex(name);
            if (index > 0) {
                CStreaming::RequestScript(index, 0x14);
                CStreaming::LoadAllRequestedModels(0);
                auto playa = FindPlayerPed(0);
                if (playa && !plugin::scripting::CallCommandById<bool>(plugin::Commands::GET_MISSION_FLAG))
                    CTheScripts::StartScript(name, 0, 0, stackSize);
                CStreaming::SetIsModelDeletable(index, CFileTypeMgr::IndexOfType_SCO);
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

                SetDebugMessage("Teleported to location");
            }
        });

        static bool mobileRadio = false;
        DebugMenuAddCmd("Misc", "Mobile Radio Toggle", []() {
            mobileRadio ^= 1;
            if (mobileRadio) {
                plugin::scripting::CallCommandById<void>(plugin::Commands::ENABLE_FRONTEND_RADIO, mobileRadio);
                plugin::scripting::CallCommandById<void>(plugin::Commands::SET_MOBILE_RADIO_ENABLED_DURING_GAMEPLAY, mobileRadio);
                plugin::scripting::CallCommandById<void>(plugin::Commands::SET_MOBILE_PHONE_RADIO_STATE, mobileRadio);
                SetDebugMessage("Mobile radio: On");
            }
            else {
                plugin::scripting::CallCommandById<void>(plugin::Commands::DISABLE_FRONTEND_RADIO, mobileRadio);
                plugin::scripting::CallCommandById<void>(plugin::Commands::SET_MOBILE_RADIO_ENABLED_DURING_GAMEPLAY, mobileRadio);
                plugin::scripting::CallCommandById<void>(plugin::Commands::SET_MOBILE_PHONE_RADIO_STATE, mobileRadio);
                SetDebugMessage("Mobile radio: Off");
            }
        });

        DebugMenuAddCmd("Misc", "Retune Radio Up", []() {
            plugin::scripting::CallCommandById<void>(plugin::Commands::RETUNE_RADIO_UP);
        });

        DebugMenuAddCmd("Misc", "Retune Radio Down", []() {
            plugin::scripting::CallCommandById<void>(plugin::Commands::RETUNE_RADIO_DOWN);
        });

        DebugMenuAddCmd("Misc", "Skip Radio Forward", []() {
            plugin::scripting::CallCommandById<void>(plugin::Commands::SKIP_RADIO_FORWARD);
        });

        DebugMenuAddCmd("Misc", "Fancy Water", []() {
			static bool fancyWater = false;
			fancyWater ^= 1;
            plugin::scripting::CallCommandById<void>(plugin::Commands::ENABLE_FANCY_WATER, fancyWater);
			SetDebugMessage(fancyWater ? "Fancy water: On" : "Fancy water: Off");
        });
    }

    static void SetDebugCamAndFreezeTime(bool on) {
        LoadSavedCams();
        ToggleDebugCam();
        freezeTime = debugCamera;
        CTimer::m_UserPause = false;

        ToggleControls(debugCamera && controlMode == 0);
    }

    static void ToggleDebugCam() {
        if (plugin::scripting::CallCommandById<bool>(plugin::Commands::ARE_WIDESCREEN_BORDERS_ACTIVE))
			return;

        LoadSavedCams();
        debugCamera ^= 1;
        ToggleControls(debugCamera && controlMode == 0);

        CCam* cam = TheCamera.m_pCamGame;
        cam->m_bActive = !debugCamera;
        for (int32_t i = 0; i < eCamMode::NUM_CAM_MODES; i++) {
            auto c = cam->GetCamMode((eCamMode)i, 0);

            if (c) {
                c->m_bActive = !debugCamera;
                auto c1 = c->m_pNext;

                if (c1)
                    c1->m_bActive = !debugCamera;
            }
        }

        if (debugCamera)
            SetDebugMessage("Debug camera: On");
        else
            SetDebugMessage("Debug camera: Off");
    }

    static void ToggleControls(bool disabled) {
        if (DebugMenuShowing() && !disabled)
            return;

        auto playa = FindPlayerPed(0);
        auto cam = TheCamera.m_pCamFinal;
        if (cam) {
            if (playa) {
                playa->m_pPlayerInfo->m_bDisableControls = disabled;
                playa->m_pPlayerInfo->m_PlayerData.m_Wanted.m_EverybodyBackOff = disabled;
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
        bool forward = CControlMgr::m_keyboard.GetKeyDown(eKeyCodes::KEY_W, 2, 0);
        bool backward = CControlMgr::m_keyboard.GetKeyDown(eKeyCodes::KEY_S, 2, 0);
        bool left = CControlMgr::m_keyboard.GetKeyDown(eKeyCodes::KEY_A, 2, 0);
        bool right = CControlMgr::m_keyboard.GetKeyDown(eKeyCodes::KEY_D, 2, 0);
        bool control = CControlMgr::m_keyboard.GetKeyDown(eKeyCodes::KEY_LCONTROL, 2, 0) || CControlMgr::m_keyboard.GetKeyDown(eKeyCodes::KEY_RCONTROL, 2, 0);
        bool shift = CControlMgr::m_keyboard.GetKeyDown(eKeyCodes::KEY_LSHIFT, 2, 0) || CControlMgr::m_keyboard.GetKeyDown(eKeyCodes::KEY_RSHIFT, 2, 0);

        bool enter = CControlMgr::m_keyboard.GetKeyJustDown(eKeyCodes::KEY_RETURN, 2, 0);
        bool up = CControlMgr::m_keyboard.GetKeyDown(eKeyCodes::KEY_UP, 2, 0);
        bool down = CControlMgr::m_keyboard.GetKeyDown(eKeyCodes::KEY_DOWN, 2, 0);
        bool lmb = CPad::IsMouseButtonPressed(1);
        int32_t mouseX = 0, mouseY = 0;
        CPad::GetMouseInput(&mouseX, &mouseY);

        auto cam = TheCamera.m_pCamFinal;
        if (!cam)
            return;

        for (int32_t i = 0; i < eCamMode::NUM_CAM_MODES; i++) {
            auto c = cam->GetCamMode((eCamMode)i, 0);
        
            if (c) {
                c->m_bActive = false;
                auto c1 = c->m_pNext;
        
                if (c1)
                    c1->m_bActive = false;
            }
        }
        cam->m_bActive = true;

        rage::Matrix44 mat = cam->m_mMatrix;
        rage::Vector3 rot = cam->m_mMatrix.GetRotation();

        float dt = (CTimer::GetTimeInMilliseconds() - CTimer::GetPreviousTimeInMilliseconds()) / 1000.0f;

        if (CTimer::GetUserPause()) {
            dt = (CTimer::GetTimeInMillisecondsPauseMode() - CTimer::GetPreviousTimeInMillisecondsPauseMode()) / 1000.0f;
        }

        static float speed = 0.0f;
        static float panspeedX = 0.0f;
        static float panspeedY = 0.0f;

        float rotationSpeed = 0.0005f;

        rage::Vector3 angle = cam->m_mMatrix.GetRotation();

        float baseAccel = 30.0f;
        float maxSpeed = 50.0f;
        float friction = 150.0f;

        if (shift) {
            maxSpeed *= 4.0f;
            baseAccel *= 4.0f;
        }
        if (control) {
            maxSpeed *= 0.25f;
            baseAccel *= 0.25f;
        }

        if (controlsEnabled) {
            if (lmb) {
                angle.z -= mouseX * rotationSpeed;
                angle.x -= mouseY * rotationSpeed;
            }

            if (forward) {
                speed += baseAccel * dt;
                if (speed > maxSpeed) speed = maxSpeed;
            }
            else if (backward) {
                speed -= baseAccel * dt;
                if (speed < -maxSpeed) speed = -maxSpeed;
            }
            else {
                if (speed > 0.0f) {
                    speed -= friction * dt;
                    if (speed < 0.0f) speed = 0.0f;
                }
                else if (speed < 0.0f) {
                    speed += friction * dt;
                    if (speed > 0.0f) speed = 0.0f;
                }
            }

            if (left) {
                panspeedX -= baseAccel * dt;
                if (panspeedX < -maxSpeed) panspeedX = -maxSpeed;
            }
            else if (right) {
                panspeedX += baseAccel * dt;
                if (panspeedX > maxSpeed) panspeedX = maxSpeed;
            }
            else {
                if (panspeedX > 0.0f) {
                    panspeedX -= friction * dt;
                    if (panspeedX < 0.0f) panspeedX = 0.0f;
                }
                else if (panspeedX < 0.0f) {
                    panspeedX += friction * dt;
                    if (panspeedX > 0.0f) panspeedX = 0.0f;
                }
            }

            if (down) {
                panspeedY -= baseAccel * dt;
                if (panspeedY < -maxSpeed) panspeedY = -maxSpeed;
            }
            else if (up) {
                panspeedY += baseAccel * dt;
                if (panspeedY > maxSpeed) panspeedY = maxSpeed;
            }
            else {
                if (panspeedY > 0.0f) {
                    panspeedY -= friction * dt;
                    if (panspeedY < 0.0f) panspeedY = 0.0f;
                }
                else if (panspeedY < 0.0f) {
                    panspeedY += friction * dt;
                    if (panspeedY > 0.0f) panspeedY = 0.0f;
                }
            }
        }

        mat.SetRotate(angle);

        if (controlsEnabled) {
            mat.pos += mat.up * speed * dt;
            mat.pos += mat.right * panspeedX * dt;
            mat.pos += mat.at * panspeedY * dt;
        }

        cam->m_fFOV = fov;
        cam->m_fHintFOV = fov;
        cam->m_mMatrix = mat;
        teleportPos = mat.pos;

        auto playa = FindPlayerPed(0);
        auto vehicle = FindPlayerVehicle(0);
        if (enter) {
            if (vehicle) {
                vehicle->Teleport(&teleportPos, 0, 1);
                vehicle->SetInitialVelocity({ 0.0f, 0.0f, 0.0f });
            }
            else
                playa->Teleport(&teleportPos, 0, 1);

            SetDebugMessage("Teleported");
        }
    }

    DebugIV() {
        // Init DebugMenu
        if (DebugMenuLoad()) {
            DebugMenuShowing = (bool(*)())GetProcAddress(gDebugMenuAPI.module, "DebugMenuShowing");
            DebugMenuPrintString = (void(*)(const char*, float, float, int))GetProcAddress(gDebugMenuAPI.module, "DebugMenuPrintString");
            DebugMenuGetStringSize = (int(*)(const char*))GetProcAddress(gDebugMenuAPI.module, "DebugMenuGetStringSize");
        }

        plugin::Events::initGameEvent.before += []() {
            vehicleId = 0;
            vehicleNames = {};
            vehicleNamesStrings = {};

            scriptId = 0;
            scriptNames = {};

            pedId = 0;
            pedNames = {};
            pedNamesStrings = {};

            weapId = 0;
            weaponNames = {};
            weaponNamesStrings = {};
        };

        plugin::Events::initGameEvent.after += []() {
            for (auto& it : vehicleNamesStrings) {
                vehicleNames.push_back((char*)it.c_str());
            }

            for (auto& it : pedNamesStrings) {
                pedNames.push_back((char*)it.c_str());
            }

            for (auto& it : weaponNamesStrings) {
                weaponNames.push_back((char*)it.c_str());
            }

            if (CMenuManager::m_Episode == EPISODE_IV) {
                for (auto& it : missionListIV) {
                    scriptNames.push_back(it.fullName);
                }
            }
            else if (CMenuManager::m_Episode == EPISODE_TLAD) {
                for (auto& it : missionListTLAD) {
                    scriptNames.push_back(it.fullName);
                }
            }
            else if (CMenuManager::m_Episode == EPISODE_TBOGT) {
                for (auto& it : missionListTBOGT) {
                    scriptNames.push_back(it.fullName);
                }
            }

            AddEntries();
        };

        static uint32_t rendered = 0;
        plugin::Events::drawHudEvent += []() {
            if (rendered++ > 0) {
                rendered = 0;
                return;
            }

            if (playerCoords) {
                auto base = new T_CB_Generic_NoArgs([]() {
                    char buf[64];
                    auto playa = FindPlayerPed(0);
                    sprintf(buf, "%3f, %3f, %3f", playa->GetPosition().x, playa->GetPosition().y, playa->GetPosition().z);
                    DebugMenuPrintString(buf, 0.0f, 0.0f, 0);
                });
                base->Init();
            }

            if (drawCamMode) {
                static const char* camModes[] = {
                    "CAM_SKELETON",
                    "CAM_FOLLOW_PED" ,
                    "CAM_FOLLOW_VEHICLE" ,
                    "CAM_INTERP",
                    "CAM_SHAKE",
                    "CAM_FINAL",
                    "CAM_SCRIPT",
                    "CAM_GAME",
                    "CAM_TRANS",
                    "CAM_AIM_WEAPON",
                    "CAM_BUSTED",
                    "CAM_PHOTO",
                    "CAM_IDLE",
                    "CAM_2_PLAYER",
                    "CAM_SCRIPTED",
                    "CAM_CUTSCENE",
                    "CAM_WASTED",
                    "CAM_1ST_PERSON",
                    "CAM_2_PLAYER_VEH",
                    "CAM_AIM_WEAPON_VEH",
                    "CAM_VIEWPORTS",
                    "CAM_HISTORY",
                    "CAM_CINEMATIC",
                    "CAM_CINEMATIC_HELI_CHASE",
                    "CAM_CINEMATIC_CAM_MAN",
                    "CAM_SPLINE",
                    "CAM_CINEMATOGRAPHY",
                    "CAM_FPS_WEAPON",
                    "CAM_FIRE_TRUCK",
                    "CAM_RADAR",
                    "CAM_WEAPON_AIMING",
                    "CAM_ANIMATED",
                    "CAM_INTERMEZZO",
                    "CAM_VIEW_SEQ",
                    "CAM_VIEWFIND",
                    "CAM_PLAYER_SETTINGS",
                    "CAM_CINEMATIC_VEH_OFFSET",
                    "CAM_REPLAY,",
                    "CAM_FREE",
                    "CAM_DEBUG",
                    "CAM_MARKET",
                    "CAM_SECTOR",
                };
                auto base = new T_CB_Generic_NoArgs([]() {
                    char buf[64];
                    auto cam = TheCamera.m_pCamGame->m_pCurrentCamera;
                    sprintf(buf, "%s", camModes[cam->GetCurrentCamMode()]);
                    DebugMenuPrintString(buf, 0.0f, playerCoords ? 20.0f : 0.0f, 0);
                });
                base->Init();
            }

            if (drawCameraOverlay) {
                auto base = new T_CB_Generic_NoArgs([]() {
                    plugin::CallDyn(plugin::pattern::Get("83 EC 30 FF 35"));
                });
                base->Init();
            }

            {
                auto base = new T_CB_Generic_NoArgs([]() {
                    DrawDebugMessage();
                });
                base->Init();
            }
        };

        plugin::Events::gameProcessEvent += []() {
            if (CMenuManager::m_MenuActive)
                return;

            if (DebugMenuShowing()) {
                plugin::scripting::CallCommandById<void>(plugin::Commands::HIDE_HELP_TEXT_THIS_FRAME);
            }

            if ((CControlMgr::m_keyboard.GetKeyDown(eKeyCodes::KEY_LCONTROL, 2, 0) ||
                 CControlMgr::m_keyboard.GetKeyDown(eKeyCodes::KEY_RCONTROL, 2, 0)) &&
                CControlMgr::m_keyboard.GetKeyJustDown(eKeyCodes::KEY_B, 2, 0))
                ToggleDebugCam();

            if ((CControlMgr::m_keyboard.GetKeyDown(eKeyCodes::KEY_LCONTROL, 2, 0) ||
                 CControlMgr::m_keyboard.GetKeyDown(eKeyCodes::KEY_RCONTROL, 2, 0)) &&
                CControlMgr::m_keyboard.GetKeyJustDown(eKeyCodes::KEY_V, 2, 0)) {
                SetDebugCamAndFreezeTime(!freezeTime);
                SetDebugMessage("Freeze time + Debug camera : " + std::string(freezeTime ? "On" : "Off"));
            }

            if (controlMode == 0 && debugCamera) {
                if (CControlMgr::m_keyboard.GetKeyJustDown(eKeyCodes::KEY_Z, 2, 0))
                    SaveCam(TheCamera.m_pCamFinal);
                if (CControlMgr::m_keyboard.GetKeyJustDown(eKeyCodes::KEY_X, 2, 0))
                    DeleteSavedCams();
                if (CControlMgr::m_keyboard.GetKeyJustDown(eKeyCodes::KEY_Q, 2, 0))
                    PrevSavedCam(TheCamera.m_pCamFinal);
                if (CControlMgr::m_keyboard.GetKeyJustDown(eKeyCodes::KEY_E, 2, 0))
                    NextSavedCam(TheCamera.m_pCamFinal);
            }

            auto playa = FindPlayerPed(0);

            if (godMode)
                playa->m_bInvincible = true;

            if (vehicleGodMode) {
                auto vehicle = FindPlayerVehicle(0);
                if (vehicle) {
                    vehicle->m_bInvincible = true;
                    vehicle->m_bCanBeVisiblyDamaged = false;
                    vehicle->SetHealth(1000.0f, 0);
                }
            }

            if (infiniteAmmo) {
                auto ammoData = playa->m_WeaponData.GetAmmoDataExtraCheck();
                if (ammoData) {
                    ammoData->m_nAmmoTotal = 9999;
                }
            }

            if (noReload) {
                auto ammoData = playa->m_WeaponData.GetAmmoDataExtraCheck();
                if (ammoData && ammoData->m_nAmmoInClip <= 1) {
                    ammoData->m_nAmmoInClip = CWeaponInfo::GetWeaponInfo(ammoData->m_nType)->m_nClipSize;
                    ammoData->m_nAmmoTotal -= ammoData->m_nAmmoInClip;

                    if (ammoData->m_nAmmoTotal < 0)
                        ammoData->m_nAmmoTotal = 0;
                }
            }

            if (neverWanted) {
                FindPlayerPed(0)->m_pPlayerInfo->m_PlayerData.m_Wanted.m_WantedLevel = eWantedLevel::WANTED_CLEAN;
                FindPlayerPed(0)->m_pPlayerInfo->m_PlayerData.m_Wanted.SetMaximumWantedLevel(eWantedLevel::WANTED_CLEAN);
            }

            if (pedsIgnorePlayer) {
                playa->m_pPlayerInfo->m_PlayerData.m_Wanted.m_EverybodyBackOff = true;
            }

            if (freezeTime) {
                CTimer::m_UserPause = true;
            }

            ProcessDebugCam();
        };

        plugin::CdeclEvent <plugin::AddressList<0x9D9EC1, plugin::H_CALL>, plugin::PRIORITY_AFTER, plugin::ArgPickN<char*, 0>, CBaseModelInfo* (char*)> onItemDefCars({ "E8 ? ? ? ? 8B F0 83 C4 04 8D 44 24 78" });
        onItemDefCars += [](char* modelName) {
            vehicleNamesStrings.push_back(modelName);
        };

        plugin::CdeclEvent <plugin::AddressList<0x9D8447, plugin::H_CALL>, plugin::PRIORITY_AFTER, plugin::ArgPickN<char*, 0>, CBaseModelInfo* (char*)> onItemDefPeds({ "E8 ? ? ? ? 83 C4 4C 8B F0" });
        onItemDefPeds += [](char* modelName) {
            pedNamesStrings.push_back(modelName);
        };

        plugin::CdeclEvent <plugin::AddressList<0x9DA0CC, plugin::H_CALL>, plugin::PRIORITY_AFTER, plugin::ArgPickN<char*, 0>, CBaseModelInfo* (char*)> onItemDefWeap({ "E8 ? ? ? ? 8B F0 83 C4 20 8D 44 24 24" });
        onItemDefWeap += [](char* modelName) {
            weaponNamesStrings.push_back(modelName);
        };
    }
} debugIV;
