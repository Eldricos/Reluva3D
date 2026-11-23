# Star Trek: Armada: the missions' API

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

`Armada.exe` exports 439 functions and variables by name. The missions'
DLLs (`missions/*.dsl`, `*.drl`) and the computer player's (`AI/*.dsl`,
`*.drl`), 59 in all, import 213 of them: the API a native Armada has to
offer them, whichever way they're run ([PLAN.md](../PLAN.md)).

Made with `reluva-tool storm3d exe-survey <game folder>` (the decorated
names, and how many DLLs import each), the names undecorated with LLVM's
`llvm-undname`. The count is of DLLs, out of 59.

| DLLs | Name |
|---:|---|
| 52 | `class character WORLD` |
| 39 | `public: int __thiscall hot_int::operator int(void)` |
| 31 | `public: void __thiscall hot_int::operator=(int)` |
| 30 | `bool __cdecl in(struct _iobuf *, class hot_int *)` |
| 30 | `bool __cdecl out(struct _iobuf *, class hot_int *, char *)` |
| 30 | `class linked_list load_scripts` |
| 30 | `class linked_list save_scripts` |
| 30 | `public: __thiscall hot_int::hot_int(char *)` |
| 30 | `public: class list_cell * __thiscall linked_list::Add_Data(void *)` |
| 30 | `public: class script * __thiscall character::new_script(char *)` |
| 30 | `public: int __thiscall linked_list::Remove_Data(void *)` |
| 30 | `public: virtual __thiscall hot_int::~hot_int(void)` |
| 30 | `public: void __thiscall script::add_step(int (__cdecl *)(class script *))` |
| 29 | `public: virtual void __thiscall hot_type::add_dependent_rule(class rule *)` |
| 29 | `public: virtual void __thiscall hot_type::remove_dependent_rule(class rule *)` |
| 28 | `class dirty_int ALWAYS_EVALUATE` |
| 26 | `bool __cdecl in(struct _iobuf *, class timer_int *)` |
| 26 | `bool __cdecl in(struct _iobuf *, int *, unsigned int)` |
| 26 | `bool __cdecl out(struct _iobuf *, class timer_int *, char *)` |
| 26 | `bool __cdecl out(struct _iobuf *, int *, unsigned int, char *)` |
| 26 | `class hot_int isCineractiveCancel` |
| 26 | `int __cdecl AudioMessage(char const *)` |
| 26 | `int __cdecl GetHandle(char *)` |
| 26 | `public: __thiscall timer_int::timer_int(char *)` |
| 26 | `public: class rule * __thiscall IIIE::create_rule(int (__cdecl *)(void), void (__cdecl *)(void), char *)` |
| 26 | `public: virtual __thiscall timer_int::~timer_int(void)` |
| 26 | `public: virtual void __thiscall timer_int::start(float)` |
| 26 | `public: void __thiscall IIIE::trash_rule(class rule *)` |
| 26 | `void __cdecl CineractiveEnd(void)` |
| 26 | `void __cdecl CineractiveStart(void)` |
| 26 | `void __cdecl Goto(int, char *, int)` |
| 26 | `void __cdecl GridVisible(bool)` |
| 25 | `int __cdecl BuildObject(char *, int, char *, int)` |
| 25 | `void __cdecl CineractiveShot(int, int)` |
| 25 | `void __cdecl StopAudioMessage(int)` |
| 24 | `float __cdecl GetTime(void)` |
| 24 | `public: virtual void __thiscall timer_int::stop(void)` |
| 24 | `void __cdecl FailMission(float, char *)` |
| 24 | `void __cdecl SetPosition(int, char *)` |
| 24 | `void __cdecl SucceedMission(float, char *)` |
| 23 | `void __cdecl ObjectivesDisplay_Set_Visibility(bool)` |
| 23 | `void __cdecl SetSpecialForcesFlag(int, bool)` |
| 22 | `int __cdecl ShowTitle(int, int, int, int, char const *, bool)` |
| 22 | `void __cdecl RemoveTitle(int, bool)` |
| 21 | `void __cdecl CineractiveShot(char *, float, int)` |
| 21 | `void __cdecl CineractiveShot(char *, int, int)` |
| 21 | `void __cdecl SetWarpSpeed(int, float)` |
| 21 | `void __cdecl ShowBridgeOnScreen(bool)` |
| 21 | `void __cdecl ShowBridgeScene(bool)` |
| 20 | `bool __cdecl IsCompromised(int, int)` |
| 20 | `int __cdecl SetDilithium(int, int)` |
| 20 | `int __cdecl ShowSubtitle(int, int, int, int, char const *, bool)` |
| 20 | `void __cdecl FadeIn(float, float, float)` |
| 20 | `void __cdecl FadeOut(float, float, float)` |
| 20 | `void __cdecl ObjectivesDisplay_Set_Text_From_File(char *)` |
| 20 | `void __cdecl RemoveObject(int)` |
| 20 | `void __cdecl SetBridgeSceneRace(enum BridgeDisplay::BridgeType)` |
| 20 | `void __cdecl Stop(int, int)` |
| 19 | `void __cdecl PlayBridgeMovie(char *, int)` |
| 19 | `void __cdecl PreLoadBridgeScene(enum BridgeDisplay::BridgeType)` |
| 19 | `void __cdecl StopMovie(void)` |
| 18 | `bool __cdecl in(struct _iobuf *, class Vector3 *, unsigned int)` |
| 18 | `bool __cdecl out(struct _iobuf *, class Vector3 *, unsigned int, char *)` |
| 18 | `void __cdecl AI_Team_Load_AIP(int, char *)` |
| 18 | `void __cdecl CineractiveCameraOffset(char *, float, bool)` |
| 18 | `void __cdecl Goto(int, int, int)` |
| 18 | `void __cdecl RemoveSubtitle(int, bool)` |
| 17 | `int __cdecl CountUnits(int, char *)` |
| 16 | `bool __cdecl IsAudioMessageDone(int)` |
| 16 | `class Vector3 const & __cdecl GetLocation(int)` |
| 16 | `void __cdecl CineractiveShot(char *, float, char *, int)` |
| 16 | `void __cdecl SetRelation(int, int, enum Team::TeamRelation)` |
| 13 | `float __cdecl GetDistance(int &, char *, int)` |
| 13 | `void __cdecl Attack(int, int, int)` |
| 13 | `void __cdecl CineractiveCameraOffset(float, float, float, bool)` |
| 13 | `void __cdecl CineractiveShot(char *, int, char *, int)` |
| 13 | `void __cdecl CraftCannotDie(int, bool)` |
| 13 | `void __cdecl ObstacleAvoidence(bool)` |
| 12 | `void __cdecl AI_Attack(int, int)` |
| 12 | `void __cdecl DisableAllAITeams(void)` |
| 12 | `void __cdecl EnableAllAITeams(void)` |
| 11 | `class hot_int isCineractiveDone` |
| 11 | `void __cdecl MinimapMessage(int, int, float)` |
| 11 | `void __cdecl SetShieldPercent(int, float)` |
| 10 | `bool __cdecl IsMoviePlaying(void)` |
| 10 | `float __cdecl GetDistance(int &, int &)` |
| 10 | `float __cdecl GetShieldPercent(int)` |
| 10 | `int __cdecl CountUnitsNearPoint(char *, float, int, char *, int)` |
| 10 | `void __cdecl DestroyCritical(int)` |
| 10 | `void __cdecl PointAt(int, int)` |
| 10 | `void __cdecl ResetWarpSpeed(int)` |
| 9 | `bool __cdecl IsAlive(int)` |
| 9 | `bool __cdecl IsValid(int)` |
| 9 | `float __cdecl SetOfficers(int, float)` |
| 9 | `float __cdecl SetTeamCrew(int, float)` |
| 9 | `int __cdecl CountUnitsNearObject(int, float, int, char *)` |
| 9 | `int __cdecl GetNearestStarbase(int, int)` |
| 9 | `void __cdecl AI_Attack(int, class Vector3)` |
| 9 | `void __cdecl UnpauseSimulation(void)` |
| 8 | `void __cdecl Attack(int, class Vector3)` |
| 8 | `void __cdecl PauseSimulation(void)` |
| 8 | `void __cdecl PointAt(int, char *)` |
| 7 | `bool __cdecl InQueue(int)` |
| 7 | `int __cdecl GetNearestEnemy(int, float)` |
| 7 | `int __cdecl GetRealTeam(int)` |
| 7 | `void __cdecl Decloak(int)` |
| 7 | `void __cdecl LinkWormHoles(int, int)` |
| 7 | `void __cdecl SetRealTeam(int, int)` |
| 7 | `void __cdecl SetRedAlert(int)` |
| 6 | `class Vector3 const & __cdecl GetLocation(char *, int)` |
| 6 | `void __cdecl Cloak(int)` |
| 6 | `void __cdecl DisableWeapons(int, bool)` |
| 6 | `void __cdecl EnableLight(int, bool)` |
| 6 | `void __cdecl ImmediateCloak(int)` |
| 6 | `void __cdecl SetGreenAlert(int)` |
| 5 | `bool __cdecl IsExploding(int)` |
| 5 | `bool __cdecl RecruitSpecialForces(char *, int, int &)` |
| 5 | `void __cdecl CenterCamera(char *, int)` |
| 5 | `void __cdecl CineractiveShot(char *, float, char *, float)` |
| 5 | `void __cdecl FadeRemove(void)` |
| 5 | `void __cdecl MinimapMessage(char *, int, float, int)` |
| 5 | `void __cdecl RemoveAllCraft(bool, int)` |
| 5 | `void __cdecl SetImpulseSpeed(int, float)` |
| 5 | `void __cdecl SetPerceivedTeam(int, int)` |
| 4 | `int __cdecl Load_Rules(char *)` |
| 4 | `public: __thiscall group::group(char *)` |
| 4 | `public: __thiscall group::~group(void)` |
| 4 | `public: void * __thiscall script::nth_param_ptr(int)` |
| 4 | `public: void __thiscall script::add_character_param(class character *)` |
| 4 | `void __cdecl AI_Team_Load_AIP(class character &, char *)` |
| 4 | `void __cdecl DestroyShieldGenerator(int)` |
| 4 | `void __cdecl SetSpecialEnergy(int, float)` |
| 4 | `void __cdecl SetSpecialWeaponAttackInterval(int, float)` |
| 4 | `void __cdecl Starfield_Load_Background_Geometry(char *)` |
| 3 | `bool __cdecl in(struct _iobuf *, float *, unsigned int)` |
| 3 | `bool __cdecl out(struct _iobuf *, float *, unsigned int, char *)` |
| 3 | `float __cdecl GetRealShieldPercent(int)` |
| 3 | `int __cdecl CreateSpecialFlag(int, char *)` |
| 3 | `int __cdecl GetLocationOfFlag(int)` |
| 3 | `public: class rule * __thiscall IIIE::create_rule(int (__cdecl *)(class character &), void (__cdecl *)(class character &), char *)` |
| 3 | `public: void __thiscall IIIE::trash_rule(char *)` |
| 3 | `public: void __thiscall group::add_group_entry_function(void (__cdecl *)(class character &), void (__cdecl *)(class character &))` |
| 3 | `public: void __thiscall group::remove_group_entry_function(void (__cdecl *)(class character &), void (__cdecl *)(class character &))` |
| 3 | `void __cdecl Build(int, char *)` |
| 3 | `void __cdecl CineractiveShot(char *, int, char *, float)` |
| 3 | `void __cdecl DestroyLifeSupport(int)` |
| 3 | `void __cdecl DestroyWeapons(int)` |
| 3 | `void __cdecl DisableEngines(int, bool)` |
| 3 | `void __cdecl Patrol(int, char *)` |
| 3 | `void __cdecl SetMaxShields(int, float)` |
| 3 | `void __cdecl SetRace(int, char const *)` |
| 3 | `void __cdecl Special_Attack(int, class Vector3, char const *)` |
| 3 | `void __cdecl Special_Attack(int, int, char const *)` |
| 3 | `void __cdecl StopAllShips(int, char const *, bool, bool)` |
| 3 | `void __cdecl TransferFlag(int, int)` |
| 2 | `bool __cdecl DoesNotExist(int)` |
| 2 | `bool __cdecl Exists(int)` |
| 2 | `bool __cdecl InStopMode(int)` |
| 2 | `bool __cdecl OutOfSpecialAttackMode(int)` |
| 2 | `class hot_float strength_team_1` |
| 2 | `class hot_float strength_team_2` |
| 2 | `class hot_int moons_team_1` |
| 2 | `class hot_int moons_team_2` |
| 2 | `float __cdecl AddCrew(int, float)` |
| 2 | `float __cdecl AddOfficers(int, float)` |
| 2 | `float __cdecl GetCrew(int)` |
| 2 | `int __cdecl CountVisibleUnits(int, int, bool, char *)` |
| 2 | `int __cdecl CountVisibleUnitsNearObject(int, float, int, bool, char *)` |
| 2 | `int __cdecl GetNearestObjectNearMe(int, float, int, char *, int, bool)` |
| 2 | `int __cdecl ReplaceObject(int, char *)` |
| 2 | `public: float __thiscall hot_float::operator float(void)` |
| 2 | `void __cdecl DestroyEngines(int)` |
| 2 | `void __cdecl DisableShieldGenerator(int, bool)` |
| 2 | `void __cdecl DisableTeam(int)` |
| 2 | `void __cdecl EnableTeam(int)` |
| 2 | `void __cdecl Mine(int, int)` |
| 2 | `void __cdecl RemoveAllObjects(void)` |
| 2 | `void __cdecl RestrictSpecialFlag(int, enum SpecialFlag::Restriction)` |
| 2 | `void __cdecl SearchAndDestroy(int)` |
| 2 | `void __cdecl SetCrew(int, float)` |
| 2 | `void __cdecl SetIsAITeam(int const &, bool const &)` |
| 2 | `void __cdecl SetYellowAlert(int)` |
| 2 | `void __cdecl Special_Attack(int, char const *)` |
| 1 | `bool __cdecl InAttackMode(int)` |
| 1 | `bool __cdecl IsDerelict(int)` |
| 1 | `bool __cdecl IsThereARomulanSpyOnShip(int)` |
| 1 | `bool __cdecl IsVisibleToTeam(int, int)` |
| 1 | `bool __cdecl UnderAttack(int, float)` |
| 1 | `class Vector3 __cdecl GetAttackLocation(int, int)` |
| 1 | `float __cdecl GetDilithiumOnFreighter(int)` |
| 1 | `int __cdecl AddDilithium(int, int)` |
| 1 | `int __cdecl BuildObject(char *, int, int, float, float, float)` |
| 1 | `int __cdecl GetAllyShipAttacks(int, int)` |
| 1 | `int __cdecl GetAllyStationAttacks(int, int)` |
| 1 | `int __cdecl GetNearestObjectNearPosition(class Vector3 const &, float, int, char *, int, bool, int)` |
| 1 | `int __cdecl GetNearestTranswarpGate(int)` |
| 1 | `int __cdecl GetPercievedTeamOfFlag(int)` |
| 1 | `int __cdecl GetRealTeamOfFlag(int)` |
| 1 | `int __cdecl GetTranswarpExitWormhole(int)` |
| 1 | `int __cdecl Load_Action_Scripts(char *)` |
| 1 | `void __cdecl AI_Attack(int)` |
| 1 | `void __cdecl AddTransporterEffect(int)` |
| 1 | `void __cdecl Add_Team_To_Group(int, class group &)` |
| 1 | `void __cdecl AllowMultipleSpecialWeaponFire(int, bool)` |
| 1 | `void __cdecl AttackMainEnemyBase(int, bool)` |
| 1 | `void __cdecl ClearRestrictions(int)` |
| 1 | `void __cdecl DestroySensors(int)` |
| 1 | `void __cdecl EnableWormholeTravel(int, bool)` |
| 1 | `void __cdecl EndCredits(void)` |
| 1 | `void __cdecl RemoveBillboardEffect(int)` |
| 1 | `void __cdecl RestrictSpecialFlag(int, enum SpecialFlag::Restriction, int)` |
| 1 | `void __cdecl SetWormholeTeam(int, int)` |
| 1 | `void __cdecl StartCredits(void)` |
