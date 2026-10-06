#pragma once
#include "mmx_weapon_combat.h"
#include "mmx_weapons.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Arena rules sit beside co-op. MmxCoopState stays at its save size.
 * This blob is save-chunk version 18 and is safe to extend with new fields
 * at the end. character, loadout and palette are stored now so a later
 * roster, three-weapon loadout, or palette swap does not need a new mode. */

enum {
  MMX_PVP_FIGHT = 0,
  MMX_PVP_ROUND_END = 1,
  MMX_PVP_MATCH_END = 2,
  MMX_PVP_SETUP = 3,
  /* Walking from the last checkpoint to the boss room. No damage. */
  MMX_PVP_APPROACH = 4,
  /* The round is about to start. Input is locked. */
  MMX_PVP_READY = 5,
  MMX_PVP_READY_FRAMES = 60,
  /* The first wait after a door lets the boss room finish scrolling in. */
  MMX_PVP_GATE_FRAMES = 90,
  MMX_PVP_GO_FRAMES = 40,
  MMX_PVP_LOADOUT_SLOTS = 3,
  MMX_PVP_LOADOUT_ALL = 0xff,
  MMX_PVP_COOLDOWN_SLOTS = 48,
  MMX_PVP_ROUND_HOLD = 120, /* about two seconds */
  MMX_PVP_HURT_FRAMES = 20,
  MMX_PVP_SHOT_DAMAGE = 1,
  MMX_PVP_SPECIAL_DAMAGE = 3,
  MMX_PVP_BODY_DAMAGE = 2,
  MMX_PVP_SEAT_ALIVE = 1,
  MMX_PVP_LAYOUT_FLAT = 0,
  MMX_PVP_LAYOUT_PLATFORM = 1
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
  /* Appended. room is a catalog index, layout is flat or one platform,
   * last_ko is 0, 1, 2 (draw), or 0xff while the round is still live. */
  uint8_t room;
  uint8_t layout;
  uint8_t last_ko;
  uint8_t actors_retired;
  int16_t last_y[2];
  /* Shared setup. owner is 0, 1, or 0xff when the seat is open.
   * Both machines render this struct, so both see the same seats. */
  uint8_t owner[2];
  uint8_t ready[2];
  uint8_t armor;
  uint8_t zero_modern;
  uint8_t row[2];
  uint8_t seat_pick[2];
  /* 1 while the stage loader is coming up, 2 while a different stage is
   * being left for a reload. */
  uint8_t entering;
  uint16_t held[2];
  /* The boss room the doors opened onto. doors counts doors since the
   * stage started; the arena locks on the room's last one. */
  uint8_t gate_ok;
  uint8_t doors;
  int16_t gate_x, gate_y;
  uint8_t round_no;
  uint8_t go_timer;
} MmxPvpState;

/* One piece of the setup screen, in a 256 x 224 field. icon is a weapon id
 * drawn as its 16 x 16 icon, otherwise text is drawn at scale. */
enum {
  MMX_PVP_TONE_TEXT = 0,
  MMX_PVP_TONE_DIM = 1,
  MMX_PVP_TONE_ACCENT = 2,
  MMX_PVP_TONE_READY = 3,
  MMX_PVP_TONE_CURSOR = 4,
  MMX_PVP_TONE_RULE = 5
};
typedef struct MmxPvpSetupItem {
  int16_t x, y;
  uint8_t icon;
  uint8_t scale;
  uint8_t tone;
  int16_t width; /* a rule line's length, when tone is RULE */
  char text[48];
} MmxPvpSetupItem;

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
void MmxPvpArmArena(unsigned room, unsigned layout);
void MmxPvpClearArm(void);
bool MmxPvpLaunchArmed(void);
/* 1 when the three slots are real weapons, so shoulders stay inside that kit. */
bool MmxPvpRestrictsLoadout(void);
/* direction < 0 walks backward. The buster is always in the ring. */
bool MmxPvpCycleWeapon(unsigned seat, unsigned page, unsigned weapon, int direction,
                       unsigned *out_page, unsigned *out_weapon);
/* Title skip only after the setup screen has committed a stage. */
int MmxPvpBootWantsStart(unsigned frame, int gameplay);
/* The new-game initializer writes Highway, then clears armor. These run
 * inside that initializer so the selected stage and armor replace those bytes
 * before the stage loader continues. Story Play leaves them alone. */
void MmxPvpPatchNewGameStage(void);
void MmxPvpPatchNewGameArmor(void);
/* $00:991B clears the checkpoint. A versus entry starts at the last one. */
void MmxPvpPatchCheckpoint(void);
/* Both pads, every frame. in_stage is the gameplay mode the boot already uses. */
void MmxPvpSetupPoll(uint16_t pad0, uint16_t pad1, int in_stage);
/* Item count, 0 when the setup screen is not up. *cover is 1 when nothing
 * of the game should show behind it, 0 when it sits over the arena. */
int MmxPvpSetupLayout(MmxPvpSetupItem *items, int cap, int *cover);
/* A door finished with the camera on the next room. */
void MmxPvpGateOpened(int camera_x, int camera_y);
/* Whether an X2 (1) or X3 (2) weapon page can be offered. Unset offers all. */
void MmxPvpSetPageCheck(int (*page_ok)(unsigned page));
/* "X 1 - 0 ZERO" while a set is being played. */
int MmxPvpScoreLine(char *out, size_t cap);
#define MMX_PVP_PICK_PREFIX "[[mmxpvp]] "
#define MMX_PVP_MAP_PREFIX "[[mmxmap]] "
int MmxPvpParsePick(const char *text, int *w0, int *w1, int *w2);
int MmxPvpParseMap(const char *text, int *room, int *layout);
/* 1 and a line for the round or the match. 0 while the fight is live. */
int MmxPvpCallout(char *out, size_t cap);
/* Clear the stage's own actors once, so the room stays a duel. */
void MmxPvpRetireArenaActors(uint8_t *ram);
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
/* 1 when addr is one enemy ($E68) or enemy-shot ($1428) slot, which is then cleared.
 * Player shots at $1228 are left alone. */
int MmxPvpRetireStageSlot(uint8_t *ram, unsigned addr);
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
