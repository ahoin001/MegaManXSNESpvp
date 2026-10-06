#pragma once
#include "mmx_weapon_combat.h"
#include "mmx_weapons.h"
#include <stdbool.h>
#include <stdint.h>

/* Arena rules sit beside co-op. MmxCoopState stays at its save size.
 * This blob is save-chunk version 18 and is safe to extend with new fields
 * at the end. character, loadout and palette are stored now so a later
 * roster, three-weapon loadout, or palette swap does not need a new mode. */

enum {
  MMX_PVP_FIGHT = 0,
  MMX_PVP_ROUND_END = 1,
  MMX_PVP_MATCH_END = 2,
  MMX_PVP_LOADOUT_SLOTS = 3,
  MMX_PVP_LOADOUT_ALL = 0xff,
  MMX_PVP_COOLDOWN_SLOTS = 48,
  MMX_PVP_ROUND_HOLD = 120, /* about two seconds */
  MMX_PVP_HURT_FRAMES = 20,
  MMX_PVP_SHOT_DAMAGE = 1,
  MMX_PVP_SPECIAL_DAMAGE = 3,
  MMX_PVP_BODY_DAMAGE = 2,
  MMX_PVP_SEAT_ALIVE = 1
};

typedef struct MmxPvpBox {
  int16_t left, right, top, bottom;
  int16_t camera_x, camera_y;
  int16_t spawn_x[2], spawn_y[2];
} MmxPvpBox;

typedef struct MmxPvpState {
  uint8_t enabled;
  uint8_t phase;
  uint8_t best_of;
  uint8_t wins[2];
  uint16_t phase_timer;
  uint8_t baseline_hp;
  uint8_t arena_ready;
  uint8_t stage;
  MmxPvpBox arena;
  uint16_t cooldown[2][MMX_PVP_COOLDOWN_SLOTS];
  uint8_t character[2];
  uint8_t loadout[2][MMX_PVP_LOADOUT_SLOTS];
  uint8_t palette[2];
  uint8_t hurt[2];
  uint8_t native_hit[2][8];
  uint8_t imported_hit[2][8];
} MmxPvpState;

typedef struct MmxPvpHitShot {
  int x, y;
  uint8_t active, special, hit;
} MmxPvpHitShot;

typedef struct MmxPvpHitActor {
  int x, y;
  uint8_t hp, hurt;
  MmxPvpHitShot shots[8];
} MmxPvpHitActor;

typedef struct MmxPvpLiveSeat {
  uint8_t *body;
  uint8_t *shots;
  MmxWeaponCombatState *combat;
  MmxWeaponsState *weapons;
  uint8_t status;
} MmxPvpLiveSeat;

void MmxPvpEnable(void);
void MmxPvpDisable(void);
bool MmxPvpEnabled(void);
/* A versus room's agreed kits. Enable copies them once, then clears the arm. */
void MmxPvpArmLaunch(const uint8_t character[2], const uint8_t loadout[2][3]);
void MmxPvpClearArm(void);
bool MmxPvpLaunchArmed(void);
/* 1 when the three slots are real weapons, so shoulders stay inside that kit. */
bool MmxPvpRestrictsLoadout(void);
/* direction < 0 walks backward. The buster is always in the ring. */
bool MmxPvpCycleWeapon(unsigned seat, unsigned page, unsigned weapon, int direction,
                       unsigned *out_page, unsigned *out_weapon);
/* Title skip for an armed versus boot. gameplay stops the script. */
int MmxPvpBootWantsStart(unsigned frame, int gameplay);
#define MMX_PVP_PICK_PREFIX "[[mmxpvp]] "
int MmxPvpParsePick(const char *text, int *w0, int *w1, int *w2);
int MmxPvpPickReady(int w0, int w1, int w2);
void MmxPvpReset(void);
MmxPvpState MmxPvpGetState(void);
bool MmxPvpValidState(const MmxPvpState *state);
void MmxPvpSetState(const MmxPvpState *state);
/* Keep a match that was already enabled when an older save has no PvP blob. */
void MmxPvpApplyLoaded(const MmxPvpState *loaded);

void MmxPvpInit(MmxPvpState *state);
void MmxPvpSetActiveSeat(unsigned seat);
unsigned MmxPvpActiveSeat(void);
bool MmxPvpInputLocked(void);
bool MmxPvpBlocksCampaignDeath(void);
bool MmxPvpArenaReady(void);
bool MmxPvpFollowTarget(int *x, int *y);
void MmxPvpClampBody(int *x, int *y);

/* weapon_id 0 is the buster. Other ids are (page << 4) | weapon. */
bool MmxPvpLoadoutAllows(const uint8_t loadout[MMX_PVP_LOADOUT_SLOTS], unsigned weapon_id);
/* Starts a cooldown when the special is ready. Does not change *energy. */
bool MmxPvpTryFire(MmxPvpState *state, unsigned seat, unsigned weapon_id, unsigned *energy);
void MmxPvpTickCooldowns(MmxPvpState *state);
/* Live spend: loadout gate only. Cooldowns are applied when the shot is seen. */
bool MmxPvpCommitSpend(unsigned seat, unsigned weapon_id);

bool MmxPvpRectsOverlap(int x, int y, int w, int h, int x2, int y2, int w2, int h2);
/* -1 none, 0 or 1 the seat that reached 0 HP, 2 both. */
int MmxPvpExchange(MmxPvpHitActor actors[2]);
bool MmxPvpRegisterKo(MmxPvpState *state, unsigned victim);
bool MmxPvpRegisterDraw(MmxPvpState *state);
/* 1 = round timer finished and both seats should respawn. */
int MmxPvpTickClock(MmxPvpState *state);
void MmxPvpApplySpawns(const MmxPvpState *state, int *x0, int *y0, int *x1, int *y1,
                       uint8_t *hp0, uint8_t *hp1);
void MmxPvpBuildArena(int x0, int y0, int x1, int y1, int min_x, int max_x, int min_y, int max_y,
                      MmxPvpBox *box);
void MmxPvpClampPoint(const MmxPvpBox *box, int *x, int *y);

void MmxPvpSimulate(MmxPvpLiveSeat seats[2], int min_x, int max_x, int min_y, int max_y,
                    uint8_t stage_id, uint8_t max_hp);
