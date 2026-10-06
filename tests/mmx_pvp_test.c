#include "mmx_pvp.h"
#include "mmx_arena.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

uint8_t g_ram[0x20000];

static void test_shot_hit_and_miss(void) {
  MmxPvpHitActor actors[2];
  memset(actors, 0, sizeof(actors));
  actors[0].x = 100; actors[0].y = 100; actors[0].hp = 10;
  actors[1].x = 400; actors[1].y = 100; actors[1].hp = 10;
  actors[0].shots[0].active = 1;
  actors[0].shots[0].x = 400;
  actors[0].shots[0].y = 90;
  assert(MmxPvpExchange(actors) == -1);
  assert(actors[1].hp == 9);
  assert(actors[0].hp == 10);
  assert(actors[0].shots[0].hit == 1);
  /* The same shot cannot hit again until it leaves. */
  assert(MmxPvpExchange(actors) == -1);
  assert(actors[1].hp == 9);
  actors[0].shots[0].x = 800;
  actors[0].shots[0].hit = 0;
  actors[1].hurt = 0;
  assert(MmxPvpExchange(actors) == -1);
  assert(actors[1].hp == 9);
}

static void test_ko_reset_and_match(void) {
  MmxPvpState match;
  MmxPvpHitActor actors[2];
  int x0, y0, x1, y1, i;
  uint8_t hp0, hp1;
  MmxPvpInit(&match);
  match.baseline_hp = 16;
  MmxPvpBuildArena(1000, 500, 1040, 500, 0, 4000, 0, 4000, &match.arena);
  memset(actors, 0, sizeof(actors));
  actors[0].x = 100; actors[0].y = 100; actors[0].hp = 10;
  actors[1].x = 400; actors[1].y = 100; actors[1].hp = 1;
  actors[0].shots[0].active = 1;
  actors[0].shots[0].special = 1;
  actors[0].shots[0].x = 400;
  actors[0].shots[0].y = 90;
  assert(MmxPvpExchange(actors) == 1);
  assert(actors[1].hp == 0);
  assert(MmxPvpRegisterKo(&match, 1));
  assert(match.wins[0] == 1);
  assert(match.phase == MMX_PVP_ROUND_END);
  for (i = 0; i < MMX_PVP_ROUND_HOLD - 1; ++i) assert(MmxPvpTickClock(&match) == 0);
  assert(MmxPvpTickClock(&match) == 1);
  assert(match.phase == MMX_PVP_READY);
  for (i = 0; i < MMX_PVP_READY_FRAMES; ++i) assert(MmxPvpTickClock(&match) == 0);
  assert(match.phase == MMX_PVP_FIGHT && match.go_timer == MMX_PVP_GO_FRAMES);
  MmxPvpApplySpawns(&match, &x0, &y0, &x1, &y1, &hp0, &hp1);
  assert(hp0 == 16 && hp1 == 16);
  assert(x0 == match.arena.spawn_x[0] && x1 == match.arena.spawn_x[1]);
  assert(x0 != x1);
  assert(MmxPvpRegisterKo(&match, 1));
  assert(match.wins[0] == 2);
  for (i = 0; i < MMX_PVP_ROUND_HOLD; ++i) assert(MmxPvpTickClock(&match) == 0);
  assert(match.phase == MMX_PVP_MATCH_END);
  assert(match.phase != MMX_PVP_FIGHT);
}

static void test_cooldown_keeps_energy(void) {
  MmxPvpState match;
  unsigned energy = 28 * 256;
  unsigned i;
  MmxPvpInit(&match);
  match.loadout[0][0] = 1;
  match.loadout[0][1] = 2;
  match.loadout[0][2] = 3;
  assert(!MmxPvpTryFire(&match, 0, 4, &energy));
  assert(energy == 28 * 256);
  assert(MmxPvpTryFire(&match, 0, 1, &energy));
  assert(energy == 28 * 256);
  assert(match.cooldown[0][1] > 0);
  assert(!MmxPvpTryFire(&match, 0, 1, &energy));
  assert(energy == 28 * 256);
  for (i = 0; match.cooldown[0][1]; ++i) {
    assert(i < 300);
    MmxPvpTickCooldowns(&match);
  }
  assert(MmxPvpTryFire(&match, 0, 1, &energy));
  assert(energy == 28 * 256);
  assert(MmxPvpTryFire(&match, 0, 0, &energy));
}

static void test_open_loadout_and_clamp(void) {
  MmxPvpState match;
  MmxPvpBox box;
  int x, y, inside_x, inside_y;
  unsigned energy = 10;
  MmxPvpInit(&match);
  assert(MmxPvpTryFire(&match, 0, 4, &energy));
  assert(energy == 10);
  MmxPvpBuildArena(1000, 500, 1040, 500, 0, 4000, 0, 4000, &box);
  assert(box.spawn_x[0] < box.spawn_x[1]);
  assert(box.spawn_x[0] >= box.left && box.spawn_x[1] <= box.right);
  assert(box.right - box.left == 224);
  x = box.left - 20;
  y = box.bottom + 30;
  MmxPvpClampPoint(&box, &x, &y);
  assert(x == box.left && y == box.bottom);
  inside_x = box.left + 10;
  inside_y = box.top + 10;
  MmxPvpClampPoint(&box, &inside_x, &inside_y);
  assert(inside_x == box.left + 10 && inside_y == box.top + 10);
}

static void test_retire_stage_slots(void) {
  uint8_t ram[0x1700];
  memset(ram, 0, sizeof(ram));
  ram[0xe68] = 1;
  ram[0xe68 + 10] = 0x19;
  ram[0x1228] = 7;
  ram[0x1228 + 5] = 40;
  ram[0x1428] = 3;
  ram[0x1428 + 8] = 12;
  assert(MmxPvpRetireStageSlot(ram, 0xe68));
  assert(MmxPvpRetireStageSlot(ram, 0x1428));
  assert(!MmxPvpRetireStageSlot(ram, 0x1228));
  assert(!MmxPvpRetireStageSlot(ram, 0xe69));
  assert(ram[0xe68] == 0 && ram[0xe68 + 10] == 0);
  assert(ram[0x1428] == 0 && ram[0x1428 + 8] == 0);
  assert(ram[0x1228] == 7 && ram[0x1228 + 5] == 40);
}

static void test_versus_kit_and_boot(void) {
  MmxPvpState match;
  uint8_t character[2] = {0, 1};
  uint8_t kit[2][3] = {{1, 2, 3}, {0x11, 0x12, 0x13}};
  unsigned page = 9, weapon = 9;
  int w0 = 0, w1 = 0, w2 = 0;
  MmxPvpArmLaunch(character, kit);
  assert(MmxPvpLaunchArmed());
  MmxPvpEnable();
  assert(!MmxPvpLaunchArmed());
  match = MmxPvpGetState();
  assert(match.phase == MMX_PVP_SETUP);
  assert(match.character[0] == 0 && match.character[1] == 1);
  assert(match.loadout[0][0] == 0);
  assert(!MmxPvpBootWantsStart(120, 0));
  match.loadout[0][0] = 1;
  match.loadout[0][1] = 2;
  match.loadout[0][2] = 3;
  match.loadout[1][0] = 0x11;
  match.loadout[1][1] = 0x12;
  match.loadout[1][2] = 0x13;
  match.phase = MMX_PVP_FIGHT;
  MmxPvpSetState(&match);
  assert(MmxPvpRestrictsLoadout());
  assert(MmxPvpCycleWeapon(0, 0, 0, 1, &page, &weapon));
  assert(page == 0 && weapon == 1);
  assert(MmxPvpCycleWeapon(0, 0, 3, 1, &page, &weapon));
  assert(page == 0 && weapon == 0);
  assert(MmxPvpCycleWeapon(0, 0, 4, 1, &page, &weapon));
  assert(page == 0 && weapon == 1);
  assert(MmxPvpCycleWeapon(1, 1, 1, -1, &page, &weapon));
  assert(page == 0 && weapon == 0);
  match = MmxPvpGetState();
  match.phase = MMX_PVP_SETUP;
  match.entering = 1;
  MmxPvpSetState(&match);
  assert(MmxPvpBootWantsStart(120, 0));
  assert(MmxPvpBootWantsStart(240, 0));
  assert(MmxPvpBootWantsStart(400, 0));
  assert(MmxPvpBootWantsStart(560, 0));
  assert(MmxPvpBootWantsStart(720, 0));
  assert(!MmxPvpBootWantsStart(121, 0));
  assert(!MmxPvpBootWantsStart(120, 1));
  assert(MmxPvpParsePick("[[mmxpvp]] 1 2 3", &w0, &w1, &w2));
  assert(w0 == 1 && w1 == 2 && w2 == 3);
  assert(MmxPvpPickReady(1, 2, 3));
  assert(MmxPvpPickReady(0x11, 0x12, 0x21));
  assert(!MmxPvpPickReady(1, 1, 2));
  assert(!MmxPvpPickReady(0, 1, 2));
  assert(!MmxPvpParsePick("hello", &w0, &w1, &w2));
  MmxPvpClearArm();
  MmxPvpEnable();
  assert(MmxPvpGetState().phase == MMX_PVP_SETUP);
  assert(!MmxPvpBootWantsStart(120, 0));
}

enum { PAD_B = 1u << 0, PAD_UP = 1u << 4, PAD_DOWN = 1u << 5, PAD_RIGHT = 1u << 7, PAD_A = 1u << 8 };

static void press(unsigned pad, uint16_t button, int in_stage) {
  MmxPvpSetupPoll(pad ? 0 : button, pad ? button : 0, in_stage);
  MmxPvpSetupPoll(0, 0, in_stage);
}

static int layout_has(const char *text) {
  MmxPvpSetupItem items[96];
  int cover = 0, n = MmxPvpSetupLayout(items, 96, &cover), i;
  for (i = 0; i < n; ++i)
    if (!strcmp(items[i].text, text)) return 1;
  return 0;
}

static void ready_both(int in_stage) {
  press(0, PAD_A, in_stage);
  press(1, PAD_A, in_stage);
}

static void test_setup_to_arena(void) {
  static uint8_t rom[0x40000];
  uint8_t body0[64], body1[64];
  MmxPvpLiveSeat seats[2];
  MmxPvpState match;
  char text[64];
  unsigned i;
  /* Thirteen stage lists of three checkpoints, as in the retail table. */
  memset(rom, 0, sizeof rom);
  for (i = 0; i < 13; ++i) {
    unsigned off = 0x1a + i * 6;
    rom[0x32780 + i * 2] = (uint8_t)off;
    rom[0x32780 + i * 2 + 1] = (uint8_t)(off >> 8);
  }
  MmxArenaSetRom(rom, sizeof rom);
  assert(MmxArenaLastCheckpoint(0) == 2);
  assert(MmxArenaRoomAvailable(7));

  MmxPvpClearArm();
  MmxPvpEnable();
  assert(MmxPvpGetState().phase == MMX_PVP_SETUP);
  assert(layout_has("VERSUS"));

  press(0, PAD_A, 0);
  assert(MmxPvpGetState().owner[0] == 0);
  press(1, PAD_A, 0); /* X is taken. */
  assert(MmxPvpGetState().owner[0] == 0 && MmxPvpGetState().owner[1] == 0xff);
  press(1, PAD_RIGHT, 0);
  press(1, PAD_A, 0);
  assert(MmxPvpGetState().owner[1] == 1);
  for (i = 0; i < 3; ++i) {
    press(0, PAD_A, 0);
    press(1, PAD_A, 0);
  }
  match = MmxPvpGetState();
  assert(MmxPvpPickReady(match.loadout[0][0], match.loadout[0][1], match.loadout[0][2]));
  assert(MmxPvpPickReady(match.loadout[1][0], match.loadout[1][1], match.loadout[1][2]));
  assert(match.row[0] == 4 && match.row[1] == 4);

  /* A rule change clears both ready marks. */
  press(0, PAD_A, 0);
  assert(MmxPvpGetState().ready[0] == 1);
  press(0, PAD_DOWN, 0);
  press(0, PAD_RIGHT, 0);
  assert(MmxPvpGetState().armor == 1 && MmxPvpGetState().ready[0] == 0);
  press(0, PAD_UP, 0);
  /* P2 never reaches the rules. */
  press(1, PAD_DOWN, 0);
  assert(MmxPvpGetState().row[1] == 0);
  press(1, PAD_UP, 0);
  assert(MmxPvpGetState().entering == 0);
  ready_both(0);
  assert(MmxPvpGetState().entering == 1);
  assert(MmxPvpBootWantsStart(120, 0));

  g_ram[0x1f7a] = 0;
  MmxPvpPatchNewGameStage();
  assert(g_ram[0x1f7a] == 1);
  g_ram[0x1f81] = 0;
  MmxPvpPatchCheckpoint();
  assert(g_ram[0x1f81] == 2);
  MmxPvpSetupPoll(0, 0, 1);
  assert(MmxPvpGetState().phase == MMX_PVP_APPROACH);
  assert(MmxPvpCallout(text, sizeof text) && !strcmp(text, "TO THE BOSS DOOR"));

  memset(body0, 0, sizeof body0);
  memset(body1, 0, sizeof body1);
  body0[5] = 100; body0[8] = 150; body0[0x27] = 3;
  body1[5] = 140; body1[8] = 150; body1[0x27] = 16;
  memset(seats, 0, sizeof seats);
  seats[0].body = body0;
  seats[1].body = body1;
  seats[0].status = seats[1].status = MMX_PVP_SEAT_ALIVE;
  MmxPvpSimulate(seats, 0, 1000, 0, 1000, 1, 16);
  assert(MmxPvpGetState().phase == MMX_PVP_APPROACH);
  assert(body0[0x27] == 16);

  MmxPvpGateOpened(0x1c00, 0x200);
  assert(MmxPvpGetState().phase == MMX_PVP_APPROACH);
  MmxPvpGateOpened(0x1d00, 0x200);
  match = MmxPvpGetState();
  assert(match.phase == MMX_PVP_READY && match.gate_ok && match.gate_x == 0x1d00);
  assert(!MmxPvpArenaReady());
  for (i = 0; i < MMX_PVP_GATE_FRAMES - MMX_PVP_READY_FRAMES + 1; ++i)
    MmxPvpSimulate(seats, 0, 1000, 0, 1000, 1, 16);
  assert(MmxPvpArenaReady());
  assert(MmxPvpCallout(text, sizeof text) && !strcmp(text, "ROUND 1"));
  assert(MmxPvpInputLocked());
  for (i = 0; i < MMX_PVP_READY_FRAMES && MmxPvpGetState().phase == MMX_PVP_READY; ++i)
    MmxPvpSimulate(seats, 0, 1000, 0, 1000, 1, 16);
  assert(MmxPvpGetState().phase == MMX_PVP_FIGHT);
  assert(!MmxPvpInputLocked());
  assert(MmxPvpCallout(text, sizeof text) && !strcmp(text, "GO"));
  assert(MmxPvpScoreLine(text, sizeof text) && !strcmp(text, "X 0 - 0 ZERO"));

  /* Two knockouts end the set and bring back the same picks. */
  match = MmxPvpGetState();
  assert(MmxPvpRegisterKo(&match, 1));
  for (i = 0; i < MMX_PVP_ROUND_HOLD; ++i) MmxPvpTickClock(&match);
  for (i = 0; i < MMX_PVP_READY_FRAMES; ++i) MmxPvpTickClock(&match);
  assert(match.phase == MMX_PVP_FIGHT && match.round_no == 2);
  assert(MmxPvpRegisterKo(&match, 1));
  for (i = 0; i < MMX_PVP_ROUND_HOLD; ++i) MmxPvpTickClock(&match);
  assert(match.phase == MMX_PVP_MATCH_END);
  MmxPvpSetState(&match);
  assert(MmxPvpCallout(text, sizeof text) && !strcmp(text, "X WINS THE SET"));
  for (i = 0; i < MMX_PVP_ROUND_HOLD; ++i) MmxPvpTickClock(&match);
  assert(match.phase == MMX_PVP_SETUP && !match.ready[0] && !match.ready[1]);
  assert(match.owner[0] == 0 && match.owner[1] == 1 && match.gate_ok);
  MmxPvpSetState(&match);
  assert(layout_has("LAST SET X 2 - 0 ZERO"));

  /* The same stage: no reload, no walk. */
  ready_both(1);
  match = MmxPvpGetState();
  assert(match.phase == MMX_PVP_READY && match.entering == 0 && match.wins[0] == 0);

  MmxPvpClearArm();
  MmxPvpEnable();
}

int main(void) {
  test_shot_hit_and_miss();
  test_ko_reset_and_match();
  test_cooldown_keeps_energy();
  test_open_loadout_and_clamp();
  test_retire_stage_slots();
  test_versus_kit_and_boot();
  test_setup_to_arena();
  return 0;
}
