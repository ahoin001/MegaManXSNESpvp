#include "mmx_pvp.h"
#include "mmx_arena.h"
#include "mmx_versus_catalog.h"
#include <stdio.h>
#include <string.h>

extern uint8_t g_ram[0x20000];

static MmxPvpState g_pvp;
static MmxPvpState g_pending;
static unsigned g_seat;
static uint8_t g_pending_valid;
static uint8_t g_arm_valid;
static uint8_t g_arm_arena;
static uint8_t g_arm_character[2];
static uint8_t g_arm_loadout[2][MMX_PVP_LOADOUT_SLOTS];
static uint8_t g_arm_room;
static uint8_t g_arm_layout;

static int sat16(int v) {
  if (v > 32767) return 32767;
  if (v < -32768) return -32768;
  return v;
}

static unsigned cooldown_index(unsigned weapon_id) {
  unsigned page = weapon_id >> 4, weapon = weapon_id & 15;
  if (page > 2) return MMX_PVP_COOLDOWN_SLOTS;
  return page * 16 + weapon;
}

static uint16_t cooldown_frames(unsigned weapon_id) {
  unsigned weapon = weapon_id & 15;
  if (weapon <= 2) return 90;
  if (weapon <= 5) return 150;
  return 240;
}

static unsigned wins_needed(const MmxPvpState *s) {
  return (unsigned)(s->best_of / 2 + 1);
}

static unsigned read_hp(const uint8_t *body) { return body ? body[0x27] & 127 : 0; }

static void write_hp(uint8_t *body, unsigned hp) {
  if (!body) return;
  if (hp > 127) hp = 127;
  /* Bit 7 is the story "not in play" flag. A versus body stays visible. */
  body[0x27] = (uint8_t)hp;
}

static int read_coord(const uint8_t *body, unsigned offset) {
  return (int16_t)(body[offset] | (body[offset + 1] << 8));
}

static void write_coord(uint8_t *body, unsigned offset, int v) {
  unsigned u = (unsigned)sat16(v);
  body[offset] = (uint8_t)u;
  body[offset + 1] = (uint8_t)(u >> 8);
}

void MmxPvpInit(MmxPvpState *s) {
  unsigned seat, slot;
  if (!s) return;
  memset(s, 0, sizeof(*s));
  s->enabled = 1;
  s->best_of = 3;
  s->phase = MMX_PVP_FIGHT;
  s->last_ko = 0xff;
  s->owner[0] = s->owner[1] = 0xff;
  for (seat = 0; seat < 2; ++seat)
    for (slot = 0; slot < MMX_PVP_LOADOUT_SLOTS; ++slot)
      s->loadout[seat][slot] = MMX_PVP_LOADOUT_ALL;
}

void MmxPvpArmLaunch(const uint8_t character[2], const uint8_t loadout[2][MMX_PVP_LOADOUT_SLOTS]) {
  if (!character || !loadout) return;
  memcpy(g_arm_character, character, sizeof(g_arm_character));
  memcpy(g_arm_loadout, loadout, sizeof(g_arm_loadout));
  g_arm_valid = 1;
}
void MmxPvpArmArena(unsigned room, unsigned layout) {
  if (room >= (unsigned)MmxArenaRoomCount()) room = 0;
  g_arm_room = (uint8_t)room;
  g_arm_layout = layout ? MMX_PVP_LAYOUT_PLATFORM : MMX_PVP_LAYOUT_FLAT;
  g_arm_arena = 1;
}
void MmxPvpClearArm(void) { g_arm_valid = 0; g_arm_arena = 0; }
bool MmxPvpLaunchArmed(void) { return g_arm_valid != 0; }

void MmxPvpEnable(void) {
  if (g_arm_valid) {
    unsigned seat, slot;
    MmxPvpInit(&g_pvp);
    memcpy(g_pvp.character, g_arm_character, sizeof(g_pvp.character));
    for (seat = 0; seat < 2; ++seat)
      for (slot = 0; slot < MMX_PVP_LOADOUT_SLOTS; ++slot)
        g_pvp.loadout[seat][slot] = g_arm_loadout[seat][slot];
    g_arm_valid = 0;
    g_pvp.phase = MMX_PVP_SETUP;
    g_pvp.owner[0] = g_pvp.owner[1] = 0xff;
    memset(g_pvp.loadout, 0, sizeof(g_pvp.loadout));
    memset(g_pvp.ready, 0, sizeof(g_pvp.ready));
  } else if (g_pending_valid) {
    g_pvp = g_pending;
    g_pvp.enabled = 1;
    g_pending_valid = 0;
  } else {
    MmxPvpInit(&g_pvp);
    g_pvp.phase = MMX_PVP_SETUP;
    memset(g_pvp.loadout, 0, sizeof(g_pvp.loadout));
  }
  if (g_arm_arena) {
    g_pvp.room = g_arm_room;
    g_pvp.layout = g_arm_layout;
    g_arm_arena = 0;
  }
  g_seat = 0;
}
void MmxPvpDisable(void) {
  memset(&g_pvp, 0, sizeof(g_pvp));
  g_pending_valid = 0;
  g_seat = 0;
}
void MmxPvpReset(void) {
  if (!g_pvp.enabled) { MmxPvpDisable(); return; }
  MmxPvpInit(&g_pvp);
}
bool MmxPvpEnabled(void) { return g_pvp.enabled != 0; }
MmxPvpState MmxPvpGetState(void) { return g_pvp; }

bool MmxPvpValidState(const MmxPvpState *s) {
  unsigned seat, slot, i;
  if (!s || s->enabled > 1 || s->phase > MMX_PVP_READY || s->best_of > 9) return false;
  if (s->enabled && s->best_of < 1) return false;
  if (s->phase_timer > MMX_PVP_ROUND_HOLD || s->baseline_hp > 127 || s->arena_ready > 1 || s->stage > 15)
    return false;
  if (s->room >= (unsigned)MmxArenaRoomCount() || s->layout > MMX_PVP_LAYOUT_PLATFORM ||
      s->actors_retired > 1 || (s->last_ko > 2 && s->last_ko != 0xff))
    return false;
  if (s->armor > 1 || s->zero_modern > 1 || s->entering > 2) return false;
  if (s->gate_ok > 1 || s->go_timer > MMX_PVP_GO_FRAMES || s->round_no > 30) return false;
  for (seat = 0; seat < 2; ++seat) {
    if (s->owner[seat] > 1 && s->owner[seat] != 0xff) return false;
    if (s->ready[seat] > 1 || s->row[seat] > 8 || s->seat_pick[seat] > 1) return false;
  }
  for (seat = 0; seat < 2; ++seat) {
    if (s->wins[seat] > 9 || s->character[seat] > 1 || s->palette[seat] > 15 || s->hurt[seat] > MMX_PVP_HURT_FRAMES)
      return false;
    for (i = 0; i < MMX_PVP_COOLDOWN_SLOTS; ++i)
      if (s->cooldown[seat][i] > 240) return false;
    for (slot = 0; slot < 8; ++slot)
      if (s->native_hit[seat][slot] > 2 || s->imported_hit[seat][slot] > 2) return false;
  }
  return true;
}

void MmxPvpSetState(const MmxPvpState *s) {
  if (s && MmxPvpValidState(s)) g_pvp = *s;
}

void MmxPvpApplyLoaded(const MmxPvpState *loaded) {
  if (!loaded || !loaded->enabled || !MmxPvpValidState(loaded)) {
    g_pending_valid = 0;
    return;
  }
  if (!g_pvp.enabled) {
    g_pending = *loaded;
    g_pending_valid = 1;
    return;
  }
  g_pvp = *loaded;
  g_pvp.enabled = 1;
  g_pending_valid = 0;
}

void MmxPvpSetActiveSeat(unsigned seat) { if (seat < 2) g_seat = seat; }
unsigned MmxPvpActiveSeat(void) { return g_seat; }
bool MmxPvpInputLocked(void) {
  return g_pvp.enabled && g_pvp.phase != MMX_PVP_FIGHT && g_pvp.phase != MMX_PVP_APPROACH;
}
bool MmxPvpBlocksCampaignDeath(void) { return MmxPvpEnabled(); }
bool MmxPvpArenaReady(void) { return g_pvp.enabled && g_pvp.arena_ready; }

int MmxPvpRetireStageSlot(uint8_t *ram, unsigned addr) {
  unsigned base;
  if (!ram) return 0;
  addr &= 0xffffu;
  if (addr >= 0xe68u && addr < 0x1228u && (addr - 0xe68u) % 64u == 0) base = addr;
  else if (addr >= 0x1428u && addr < 0x1628u && (addr - 0x1428u) % 64u == 0) base = addr;
  else return 0;
  memset(ram + base, 0, 64);
  return 1;
}

bool MmxPvpFollowTarget(int *x, int *y) {
  if (!MmxPvpArenaReady() || !x || !y) return false;
  *x = g_pvp.arena.camera_x + 128;
  *y = g_pvp.arena.camera_y + 160;
  return true;
}

void MmxPvpClampBody(int *x, int *y) {
  if (!MmxPvpArenaReady()) return;
  MmxPvpClampPoint(&g_pvp.arena, x, y);
}

bool MmxPvpLoadoutAllows(const uint8_t loadout[MMX_PVP_LOADOUT_SLOTS], unsigned weapon_id) {
  unsigned i;
  if (weapon_id == 0) return true;
  if (!loadout) return false;
  for (i = 0; i < MMX_PVP_LOADOUT_SLOTS; ++i) {
    if (loadout[i] == MMX_PVP_LOADOUT_ALL) return true;
    if (loadout[i] == (uint8_t)weapon_id) return true;
  }
  return false;
}

static int weapon_id_ok(int id) {
  int page, weapon;
  if (id <= 0 || id > 0x28) return 0;
  page = id >> 4;
  weapon = id & 15;
  return page <= 2 && weapon >= 1 && weapon <= 8;
}

bool MmxPvpRestrictsLoadout(void) {
  unsigned seat, slot;
  if (!g_pvp.enabled) return false;
  for (seat = 0; seat < 2; ++seat)
    for (slot = 0; slot < MMX_PVP_LOADOUT_SLOTS; ++slot)
      if (g_pvp.loadout[seat][slot] == MMX_PVP_LOADOUT_ALL) return false;
  return true;
}

bool MmxPvpCycleWeapon(unsigned seat, unsigned page, unsigned weapon, int direction,
                       unsigned *out_page, unsigned *out_weapon) {
  unsigned ring[1 + MMX_PVP_LOADOUT_SLOTS];
  unsigned n = 0, i, cur, id;
  if (seat > 1 || !out_page || !out_weapon || !MmxPvpRestrictsLoadout()) return false;
  ring[n++] = 0;
  for (i = 0; i < MMX_PVP_LOADOUT_SLOTS; ++i) {
    unsigned w = g_pvp.loadout[seat][i], j, seen = 0;
    if (!w || w == MMX_PVP_LOADOUT_ALL) continue;
    for (j = 0; j < n; ++j) if (ring[j] == w) seen = 1;
    if (!seen) ring[n++] = w;
  }
  id = page ? ((page << 4) | (weapon & 15)) : (weapon & 15);
  cur = 0;
  for (i = 0; i < n; ++i) if (ring[i] == id) cur = i;
  if (!n) return false;
  cur = direction < 0 ? (cur + n - 1) % n : (cur + 1) % n;
  id = ring[cur];
  *out_page = id >> 4;
  *out_weapon = id & 15;
  return true;
}

int MmxPvpBootWantsStart(unsigned frame, int gameplay) {
  if (!g_pvp.enabled || !g_pvp.entering || gameplay) return 0;
  return frame == 120 || frame == 240 || frame == 400 || frame == 560 || frame == 720;
}

void MmxPvpPatchNewGameStage(void) {
  MmxArenaRoom room;
  if (!g_pvp.enabled || !g_pvp.entering) return;
  if (!MmxArenaRoomAvailable(g_pvp.room) || !MmxArenaRoomGet(g_pvp.room, &room)) return;
  g_ram[0x1f7a] = room.stage_id;
}

void MmxPvpPatchCheckpoint(void) {
  MmxArenaRoom room;
  int last;
  if (!g_pvp.enabled || !g_pvp.entering) return;
  if (!MmxArenaRoomGet(g_pvp.room, &room) || g_ram[0x1f7a] != room.stage_id) return;
  last = MmxArenaLastCheckpoint(g_pvp.room);
  if (last >= 0) g_ram[0x1f81] = (uint8_t)last;
}

void MmxPvpPatchNewGameArmor(void) {
  if (!g_pvp.enabled || !g_pvp.entering || !g_pvp.armor) return;
  /* Helmet, arms, body, and boots. The initializer just cleared these. */
  g_ram[0x1f99] = (uint8_t)(g_ram[0x1f99] | 0x0f);
}

void MmxPvpGateOpened(int camera_x, int camera_y) {
  MmxArenaRoom room;
  if (!g_pvp.enabled || g_pvp.phase != MMX_PVP_APPROACH) return;
  if (g_pvp.doors < 255) g_pvp.doors++;
  if (!MmxArenaRoomGet(g_pvp.room, &room) || g_pvp.doors < room.gate_doors) return;
  g_pvp.gate_ok = 1;
  g_pvp.gate_x = (int16_t)camera_x;
  g_pvp.gate_y = (int16_t)camera_y;
  g_pvp.phase = MMX_PVP_READY;
  g_pvp.phase_timer = MMX_PVP_GATE_FRAMES;
  g_pvp.arena_ready = 0;
  g_pvp.actors_retired = 0;
}

static int parse_id(const char **text, int *out) {
  int value = 0;
  const char *p;
  if (!text || !*text || !out) return 0;
  p = *text;
  if (*p == ' ') ++p;
  if (*p < '0' || *p > '9') return 0;
  while (*p >= '0' && *p <= '9') {
    value = value * 10 + (*p - '0');
    if (value > 255) return 0;
    ++p;
  }
  *text = p;
  *out = value;
  return 1;
}

int MmxPvpParsePick(const char *text, int *w0, int *w1, int *w2) {
  int a, b, c;
  size_t prefix;
  if (!text || !w0 || !w1 || !w2) return 0;
  prefix = strlen(MMX_PVP_PICK_PREFIX);
  if (strncmp(text, MMX_PVP_PICK_PREFIX, prefix) != 0) return 0;
  text += prefix;
  if (!parse_id(&text, &a) || !parse_id(&text, &b) || !parse_id(&text, &c)) return 0;
  *w0 = a;
  *w1 = b;
  *w2 = c;
  return 1;
}

int MmxPvpPickReady(int w0, int w1, int w2) {
  if (!weapon_id_ok(w0) || !weapon_id_ok(w1) || !weapon_id_ok(w2)) return 0;
  return w0 != w1 && w0 != w2 && w1 != w2;
}

static void equip_first(MmxPvpLiveSeat *seat, unsigned n) {
  unsigned id, page = 0, weapon = 0;
  if (!seat || n > 1 || !MmxPvpRestrictsLoadout()) return;
  id = g_pvp.loadout[n][0];
  if (id && id != MMX_PVP_LOADOUT_ALL) {
    page = id >> 4;
    weapon = id & 15;
  }
  if (seat->weapons) {
    seat->weapons->page = (uint8_t)page;
    seat->weapons->weapon = (uint8_t)weapon;
    seat->weapons->menu_page = (uint8_t)page;
    seat->weapons->charge = 0;
    seat->weapons->cooldown = 0;
  }
  if (seat->body) seat->body[0x33] = page ? 0 : (uint8_t)weapon;
}

bool MmxPvpTryFire(MmxPvpState *s, unsigned seat, unsigned weapon_id, unsigned *energy) {
  unsigned idx;
  (void)energy;
  if (!s || seat > 1) return false;
  if (weapon_id == 0) return true;
  if (!MmxPvpLoadoutAllows(s->loadout[seat], weapon_id)) return false;
  idx = cooldown_index(weapon_id);
  if (idx >= MMX_PVP_COOLDOWN_SLOTS) return false;
  if (s->cooldown[seat][idx]) return false;
  s->cooldown[seat][idx] = cooldown_frames(weapon_id);
  return true;
}

void MmxPvpTickCooldowns(MmxPvpState *s) {
  unsigned seat, i;
  if (!s) return;
  for (seat = 0; seat < 2; ++seat)
    for (i = 0; i < MMX_PVP_COOLDOWN_SLOTS; ++i)
      if (s->cooldown[seat][i]) s->cooldown[seat][i]--;
}

bool MmxPvpCommitSpend(unsigned seat, unsigned weapon_id) {
  if (!g_pvp.enabled || seat > 1) return true;
  return MmxPvpLoadoutAllows(g_pvp.loadout[seat], weapon_id);
}

bool MmxPvpRectsOverlap(int x, int y, int w, int h, int x2, int y2, int w2, int h2) {
  if (w <= 0 || h <= 0 || w2 <= 0 || h2 <= 0) return false;
  return x < x2 + w2 && x + w > x2 && y < y2 + h2 && y + h > y2;
}

static void apply_damage(MmxPvpHitActor *actor, unsigned damage) {
  if (!actor || !actor->hp) return;
  actor->hp = actor->hp > damage ? (uint8_t)(actor->hp - damage) : 0;
}

static bool body_shot(int bx, int by, int sx, int sy) {
  return MmxPvpRectsOverlap(bx - 12, by - 32, 24, 32, sx - 4, sy - 4, 8, 8);
}

static void decay_hurt(MmxPvpHitActor actors[2]) {
  unsigned i;
  for (i = 0; i < 2; ++i)
    if (actors[i].hurt) actors[i].hurt--;
}

static void apply_shots(MmxPvpHitActor actors[2]) {
  unsigned attacker, slot;
  for (attacker = 0; attacker < 2; ++attacker) {
    unsigned victim = attacker ^ 1;
    for (slot = 0; slot < 8; ++slot) {
      MmxPvpHitShot *shot = &actors[attacker].shots[slot];
      if (!shot->active) { shot->hit = 0; continue; }
      if (shot->hit || actors[victim].hurt) continue;
      if (!body_shot(actors[victim].x, actors[victim].y, shot->x, shot->y)) continue;
      apply_damage(&actors[victim], shot->special ? MMX_PVP_SPECIAL_DAMAGE : MMX_PVP_SHOT_DAMAGE);
      shot->hit = 1;
      actors[victim].hurt = MMX_PVP_HURT_FRAMES;
    }
  }
}

static void apply_bodies(MmxPvpHitActor actors[2]) {
  if (actors[0].hurt || actors[1].hurt) return;
  if (!MmxPvpRectsOverlap(actors[0].x - 12, actors[0].y - 32, 24, 32,
                          actors[1].x - 12, actors[1].y - 32, 24, 32)) return;
  apply_damage(&actors[0], MMX_PVP_BODY_DAMAGE);
  apply_damage(&actors[1], MMX_PVP_BODY_DAMAGE);
  actors[0].hurt = actors[1].hurt = MMX_PVP_HURT_FRAMES;
}

static int ko_seat(const MmxPvpHitActor actors[2]) {
  if (!actors[0].hp && !actors[1].hp) return 2;
  if (!actors[0].hp) return 0;
  if (!actors[1].hp) return 1;
  return -1;
}

int MmxPvpExchange(MmxPvpHitActor actors[2]) {
  if (!actors) return -1;
  decay_hurt(actors);
  apply_shots(actors);
  apply_bodies(actors);
  return ko_seat(actors);
}

bool MmxPvpRegisterKo(MmxPvpState *s, unsigned victim) {
  unsigned winner;
  if (!s || !s->enabled || victim > 1 || s->phase != MMX_PVP_FIGHT) return false;
  winner = victim ^ 1;
  if (s->wins[winner] < 9) s->wins[winner]++;
  s->last_ko = (uint8_t)victim;
  s->phase = MMX_PVP_ROUND_END;
  s->phase_timer = MMX_PVP_ROUND_HOLD;
  return true;
}

bool MmxPvpRegisterDraw(MmxPvpState *s) {
  if (!s || !s->enabled || s->phase != MMX_PVP_FIGHT) return false;
  s->last_ko = 2;
  s->phase = MMX_PVP_ROUND_END;
  s->phase_timer = MMX_PVP_ROUND_HOLD;
  return true;
}

int MmxPvpTickClock(MmxPvpState *s) {
  if (!s || !s->enabled) return 0;
  if (s->phase == MMX_PVP_FIGHT) {
    MmxPvpTickCooldowns(s);
    if (s->go_timer) s->go_timer--;
    return 0;
  }
  if (s->phase == MMX_PVP_APPROACH) {
    if (s->phase_timer) s->phase_timer--;
    return 0;
  }
  if (s->phase == MMX_PVP_READY) {
    if (s->phase_timer) s->phase_timer--;
    if (s->phase_timer) return 0;
    s->phase = MMX_PVP_FIGHT;
    s->go_timer = MMX_PVP_GO_FRAMES;
    return 0;
  }
  if (s->phase == MMX_PVP_MATCH_END) {
    if (s->phase_timer) s->phase_timer--;
    if (s->phase_timer) return 0;
    /* The room stays loaded. The next set can start in it. */
    s->phase = MMX_PVP_SETUP;
    s->ready[0] = s->ready[1] = 0;
    s->entering = 0;
    s->arena_ready = 0;
    return 0;
  }
  if (s->phase != MMX_PVP_ROUND_END) return 0;
  if (s->phase_timer) s->phase_timer--;
  if (s->phase_timer) return 0;
  if (s->wins[0] >= wins_needed(s) || s->wins[1] >= wins_needed(s)) {
    s->phase = MMX_PVP_MATCH_END;
    s->phase_timer = MMX_PVP_ROUND_HOLD;
    return 0;
  }
  s->phase = MMX_PVP_READY;
  s->phase_timer = MMX_PVP_READY_FRAMES;
  if (s->round_no < 30) s->round_no++;
  memset(s->cooldown, 0, sizeof(s->cooldown));
  memset(s->hurt, 0, sizeof(s->hurt));
  memset(s->native_hit, 0, sizeof(s->native_hit));
  memset(s->imported_hit, 0, sizeof(s->imported_hit));
  return 1;
}

void MmxPvpApplySpawns(const MmxPvpState *s, int *x0, int *y0, int *x1, int *y1,
                       uint8_t *hp0, uint8_t *hp1) {
  if (!s) return;
  if (x0) *x0 = s->arena.spawn_x[0];
  if (y0) *y0 = s->arena.spawn_y[0];
  if (x1) *x1 = s->arena.spawn_x[1];
  if (y1) *y1 = s->arena.spawn_y[1];
  if (hp0) *hp0 = s->baseline_hp ? s->baseline_hp : 1;
  if (hp1) *hp1 = s->baseline_hp ? s->baseline_hp : 1;
}

void MmxPvpBuildArena(int x0, int y0, int x1, int y1, int min_x, int max_x, int min_y, int max_y,
                      MmxPvpBox *box) {
  int mid_x, mid_y, cam_x, cam_y, feet;
  if (!box) return;
  mid_x = x0 + (x1 - x0) / 2;
  mid_y = y0 + (y1 - y0) / 2;
  cam_x = mid_x - 128;
  cam_y = mid_y - 160;
  if (max_x >= min_x) {
    if (cam_x < min_x) cam_x = min_x;
    if (cam_x > max_x) cam_x = max_x;
  }
  if (max_y >= min_y) {
    if (cam_y < min_y) cam_y = min_y;
    if (cam_y > max_y) cam_y = max_y;
  }
  memset(box, 0, sizeof(*box));
  box->camera_x = (int16_t)sat16(cam_x);
  box->camera_y = (int16_t)sat16(cam_y);
  box->left = (int16_t)sat16(cam_x + 16);
  box->right = (int16_t)sat16(cam_x + 240);
  box->top = (int16_t)sat16(cam_y + 48);
  box->bottom = (int16_t)sat16(cam_y + 200);
  feet = mid_y;
  if (feet < box->top) feet = box->top;
  if (feet > box->bottom) feet = box->bottom;
  box->spawn_x[0] = (int16_t)sat16(cam_x + 72);
  box->spawn_x[1] = (int16_t)sat16(cam_x + 184);
  box->spawn_y[0] = box->spawn_y[1] = (int16_t)sat16(feet);
}

void MmxPvpClampPoint(const MmxPvpBox *box, int *x, int *y) {
  if (!box || !x || !y) return;
  if (*x < box->left) *x = box->left;
  if (*x > box->right) *x = box->right;
  if (*y < box->top) *y = box->top;
  if (*y > box->bottom) *y = box->bottom;
}

/* A new special may start a cooldown. Further shots in the same observe pass
 * (a spread) are part of that shot and do not pay the timer again. */
static bool observe_special(MmxPvpState *s, unsigned seat, unsigned weapon_id, int *opened_idx) {
  unsigned idx, energy = 0;
  if (weapon_id == 0) return true;
  if (!MmxPvpLoadoutAllows(s->loadout[seat], weapon_id)) return false;
  idx = cooldown_index(weapon_id);
  if (idx >= MMX_PVP_COOLDOWN_SLOTS) return false;
  if (!s->cooldown[seat][idx]) {
    if (!MmxPvpTryFire(s, seat, weapon_id, &energy)) return false;
    if (opened_idx) *opened_idx = (int)idx;
    return true;
  }
  /* Same-frame spread: only siblings of the weapon that just started. */
  if (opened_idx && *opened_idx == (int)idx) return true;
  return false;
}

static void gate_shots(MmxPvpState *s, MmxPvpLiveSeat *seat, unsigned n) {
  unsigned weapon, id, i;
  int opened = -1;
  if (!seat || !seat->body) return;
  weapon = seat->body[0x33];
  id = weapon ? (((seat->weapons ? seat->weapons->page : 0) << 4) | (weapon & 15)) : 0;
  if (seat->shots) {
    for (i = 0; i < 8; ++i) {
      uint8_t *slot = seat->shots + i * 64;
      if (!slot[0]) { s->native_hit[n][i] = 0; continue; }
      if (s->native_hit[n][i]) continue;
      if (!observe_special(s, n, id, &opened)) { memset(slot, 0, 64); continue; }
      s->native_hit[n][i] = 1;
    }
  }
  opened = -1;
  if (!seat->combat) return;
  for (i = 0; i < 8; ++i) {
    MmxWeaponShot *shot = &seat->combat->shots[i];
    unsigned shot_id;
    if (!shot->active) { s->imported_hit[n][i] = 0; continue; }
    shot_id = ((unsigned)shot->page << 4) | (shot->weapon & 15);
    if (!shot_id) shot_id = 1;
    if (s->imported_hit[n][i]) continue;
    if (!observe_special(s, n, shot_id, &opened)) { shot->active = 0; continue; }
    s->imported_hit[n][i] = 1;
  }
}

static void fill_shots(MmxPvpHitActor *actor, const MmxPvpLiveSeat *seat, const uint8_t hit[8], int imported) {
  unsigned i;
  memset(actor->shots, 0, sizeof(actor->shots));
  if (!imported && seat->shots) {
    unsigned special = seat->body[0x33] != 0;
    for (i = 0; i < 8; ++i) {
      const uint8_t *slot = seat->shots + i * 64;
      if (!slot[0]) continue;
      actor->shots[i].active = 1;
      actor->shots[i].special = (uint8_t)special;
      actor->shots[i].hit = hit[i] == 2;
      actor->shots[i].x = (int16_t)(slot[5] | (slot[6] << 8));
      actor->shots[i].y = (int16_t)(slot[8] | (slot[9] << 8));
    }
  } else if (imported && seat->combat) {
    for (i = 0; i < 8; ++i) {
      const MmxWeaponShot *shot = &seat->combat->shots[i];
      if (!shot->active) continue;
      actor->shots[i].active = 1;
      actor->shots[i].special = 1;
      actor->shots[i].hit = hit[i] == 2;
      actor->shots[i].x = (int)(shot->x >> 8);
      actor->shots[i].y = (int)(shot->y >> 8);
    }
  }
}

static void store_hits(uint8_t hit[8], const MmxPvpHitActor *actor) {
  unsigned i;
  for (i = 0; i < 8; ++i) {
    if (!actor->shots[i].active) hit[i] = 0;
    else if (actor->shots[i].hit) hit[i] = 2;
    else if (!hit[i]) hit[i] = 1;
  }
}

static void place_seat(MmxPvpLiveSeat *seat, int x, int y, uint8_t hp) {
  unsigned i;
  if (!seat || !seat->body) return;
  write_coord(seat->body, 5, x);
  write_coord(seat->body, 8, y);
  write_hp(seat->body, hp);
  seat->body[2] = seat->body[3] = seat->body[4] = 0;
  seat->body[0x1a] = seat->body[0x1b] = 0;
  if (seat->shots) memset(seat->shots, 0, 0x200);
  if (seat->combat)
    for (i = 0; i < 8; ++i) seat->combat->shots[i].active = 0;
}

static void clamp_seat(const MmxPvpBox *box, MmxPvpLiveSeat *seat) {
  int x, y;
  if (!seat || !seat->body) return;
  x = read_coord(seat->body, 5);
  y = read_coord(seat->body, 8);
  MmxPvpClampPoint(box, &x, &y);
  write_coord(seat->body, 5, x);
  write_coord(seat->body, 8, y);
}

/* One shared ledge, the same fraction of every room. Catch a body that
 * crosses it on the way down and leave an upward jump alone. */
static void land_platform(MmxPvpLiveSeat *seat, int16_t *prev_y) {
  int x, y, prev, mid, half, plat;
  if (!seat || !seat->body || !prev_y || g_pvp.layout != MMX_PVP_LAYOUT_PLATFORM) return;
  if (g_pvp.phase != MMX_PVP_FIGHT || !g_pvp.arena_ready) return;
  x = read_coord(seat->body, 5);
  y = read_coord(seat->body, 8);
  prev = *prev_y;
  mid = (g_pvp.arena.left + g_pvp.arena.right) / 2;
  half = (g_pvp.arena.right - g_pvp.arena.left) / 6;
  if (half < 16) half = 16;
  plat = g_pvp.arena.bottom - 52;
  if (x >= mid - half && x <= mid + half && prev <= plat && y >= plat && y < plat + 18) {
    write_coord(seat->body, 8, plat);
    seat->body[0x1a] = seat->body[0x1b] = 0;
    y = plat;
  }
  *prev_y = (int16_t)y;
}

/* A fighter who drops out of the lowest camera on the way in is put back
 * beside the other one, before the stage's pit death can take a life. */
static void rescue_fall(MmxPvpLiveSeat seats[2], int max_y) {
  unsigned i;
  for (i = 0; i < 2; ++i) {
    MmxPvpLiveSeat *me = &seats[i], *other = &seats[i ^ 1];
    int oy;
    if (!me->body || !other->body || me->status != MMX_PVP_SEAT_ALIVE) continue;
    if (read_coord(me->body, 8) <= max_y + 232) continue;
    oy = read_coord(other->body, 8);
    if (oy > max_y + 232) continue;
    place_seat(me, read_coord(other->body, 5), oy - 8, g_pvp.baseline_hp ? g_pvp.baseline_hp : 16);
  }
}

static void publish_hp(MmxPvpLiveSeat *seat, unsigned hp) {
  if (!seat || !seat->body) return;
  if (!hp) hp = 1; /* Leave the native death script alone; the round owns the KO. */
  write_hp(seat->body, hp);
}

void MmxPvpSimulate(MmxPvpLiveSeat seats[2], int min_x, int max_x, int min_y, int max_y,
                    uint8_t stage_id, uint8_t max_hp) {
  unsigned i;
  int ko = -1;
  MmxPvpHitActor actors[2];
  if (!g_pvp.enabled || !seats) return;
  if (g_pvp.phase == MMX_PVP_SETUP) return;
  if (g_pvp.phase == MMX_PVP_MATCH_END) {
    MmxPvpTickClock(&g_pvp);
    return;
  }
  if (stage_id != g_pvp.stage) {
    g_pvp.stage = stage_id;
    g_pvp.arena_ready = 0;
    g_pvp.actors_retired = 0;
    g_pvp.doors = 0;
    g_pvp.gate_ok = 0;
    if (g_pvp.phase != MMX_PVP_APPROACH) {
      g_pvp.phase = MMX_PVP_APPROACH;
      g_pvp.phase_timer = MMX_PVP_ROUND_HOLD;
    }
  }
  if (!g_pvp.baseline_hp) g_pvp.baseline_hp = max_hp ? max_hp : 16;
  for (i = 0; i < 2; ++i) {
    if (seats[i].weapons) {
      memset(seats[i].weapons->energy, 28, sizeof(seats[i].weapons->energy));
      memset(seats[i].weapons->fraction, 0, sizeof(seats[i].weapons->fraction));
      seats[i].weapons->initialized = 1;
    }
    if (g_pvp.baseline_hp && seats[i].body && read_hp(seats[i].body) > g_pvp.baseline_hp)
      write_hp(seats[i].body, g_pvp.baseline_hp);
  }
  if (g_pvp.phase == MMX_PVP_APPROACH || (g_pvp.phase == MMX_PVP_READY && !g_pvp.arena_ready)) {
    /* Enemies on the way in cannot wear a fighter down before the duel. */
    for (i = 0; i < 2; ++i)
      if (seats[i].body && read_hp(seats[i].body) < g_pvp.baseline_hp)
        write_hp(seats[i].body, g_pvp.baseline_hp);
    if (g_pvp.phase == MMX_PVP_APPROACH) rescue_fall(seats, max_y);
  }
  if (g_pvp.phase == MMX_PVP_APPROACH) {
    MmxPvpTickClock(&g_pvp);
    return;
  }
  if (seats[0].status == MMX_PVP_SEAT_ALIVE && seats[1].status == MMX_PVP_SEAT_ALIVE &&
      seats[0].body && seats[1].body && !g_pvp.arena_ready && g_pvp.phase == MMX_PVP_READY &&
      g_pvp.phase_timer <= MMX_PVP_READY_FRAMES) {
    /* The doors have stopped scrolling. Both bodies are in the boss room,
     * and the camera bounds are that room's. */
    MmxPvpBuildArena(read_coord(seats[0].body, 5), read_coord(seats[0].body, 8),
                     read_coord(seats[1].body, 5), read_coord(seats[1].body, 8),
                     min_x, max_x, min_y, max_y, &g_pvp.arena);
    g_pvp.arena_ready = 1;
    place_seat(&seats[0], g_pvp.arena.spawn_x[0], g_pvp.arena.spawn_y[0], g_pvp.baseline_hp);
    place_seat(&seats[1], g_pvp.arena.spawn_x[1], g_pvp.arena.spawn_y[1], g_pvp.baseline_hp);
    g_pvp.last_y[0] = g_pvp.arena.spawn_y[0];
    g_pvp.last_y[1] = g_pvp.arena.spawn_y[1];
    equip_first(&seats[0], 0);
    equip_first(&seats[1], 1);
  }
  if (g_pvp.phase == MMX_PVP_FIGHT && g_pvp.arena_ready &&
      seats[0].status == MMX_PVP_SEAT_ALIVE && seats[1].status == MMX_PVP_SEAT_ALIVE &&
      seats[0].body && seats[1].body) {
    for (i = 0; i < 2; ++i) gate_shots(&g_pvp, &seats[i], i);
    for (i = 0; i < 2; ++i) {
      actors[i].x = read_coord(seats[i].body, 5);
      actors[i].y = read_coord(seats[i].body, 8);
      actors[i].hp = (uint8_t)read_hp(seats[i].body);
      actors[i].hurt = g_pvp.hurt[i];
      fill_shots(&actors[i], &seats[i], g_pvp.native_hit[i], 0);
    }
    decay_hurt(actors);
    apply_shots(actors);
    apply_bodies(actors);
    ko = ko_seat(actors);
    for (i = 0; i < 2; ++i) {
      store_hits(g_pvp.native_hit[i], &actors[i]);
      g_pvp.hurt[i] = actors[i].hurt;
    }
    for (i = 0; i < 2; ++i) fill_shots(&actors[i], &seats[i], g_pvp.imported_hit[i], 1);
    apply_shots(actors);
    if (ko < 0) ko = ko_seat(actors);
    else if (ko_seat(actors) >= 0 && ko_seat(actors) != ko) ko = 2;
    for (i = 0; i < 2; ++i) {
      store_hits(g_pvp.imported_hit[i], &actors[i]);
      g_pvp.hurt[i] = actors[i].hurt;
      publish_hp(&seats[i], actors[i].hp);
    }
    if (ko == 2) MmxPvpRegisterDraw(&g_pvp);
    else if (ko == 0 || ko == 1) MmxPvpRegisterKo(&g_pvp, (unsigned)ko);
  }
  if (MmxPvpTickClock(&g_pvp) == 1) {
    int x0, y0, x1, y1;
    uint8_t hp0, hp1;
    MmxPvpApplySpawns(&g_pvp, &x0, &y0, &x1, &y1, &hp0, &hp1);
    place_seat(&seats[0], x0, y0, hp0);
    place_seat(&seats[1], x1, y1, hp1);
  }
  if (g_pvp.arena_ready) {
    clamp_seat(&g_pvp.arena, &seats[0]);
    clamp_seat(&g_pvp.arena, &seats[1]);
    land_platform(&seats[0], &g_pvp.last_y[0]);
    land_platform(&seats[1], &g_pvp.last_y[1]);
  }
}

void MmxPvpRetireArenaActors(uint8_t *ram) {
  unsigned addr;
  if (!ram || !g_pvp.enabled || !g_pvp.gate_ok) return;
  if (g_pvp.phase == MMX_PVP_APPROACH) return;
  /* The boss arrives a while after the door. Its slots are cleared every
   * frame the room is an arena; enemy shots only once. */
  for (addr = 0xe68; addr < 0x1228; addr += 64)
    if (ram[addr]) MmxPvpRetireStageSlot(ram, addr);
  if (g_pvp.actors_retired) return;
  for (addr = 0x1428; addr < 0x1628; addr += 64)
    if (ram[addr]) MmxPvpRetireStageSlot(ram, addr);
  g_pvp.actors_retired = 1;
}

int MmxPvpParseMap(const char *text, int *room, int *layout) {
  int a, b;
  size_t prefix;
  if (!text || !room || !layout) return 0;
  prefix = strlen(MMX_PVP_MAP_PREFIX);
  if (strncmp(text, MMX_PVP_MAP_PREFIX, prefix) != 0) return 0;
  text += prefix;
  if (!parse_id(&text, &a) || !parse_id(&text, &b)) return 0;
  if (a < 0 || a >= MmxArenaRoomCount() || (b != 0 && b != 1)) return 0;
  *room = a;
  *layout = b;
  return 1;
}

static const char *seat_name(unsigned seat) { return seat ? "ZERO" : "X"; }

int MmxPvpCallout(char *out, size_t cap) {
  if (!out || !cap || !g_pvp.enabled) return 0;
  out[0] = 0;
  switch (g_pvp.phase) {
  case MMX_PVP_APPROACH:
    if (!g_pvp.phase_timer) return 0;
    snprintf(out, cap, "TO THE BOSS DOOR");
    return 1;
  case MMX_PVP_READY:
    if (!g_pvp.arena_ready) return 0;
    if (g_pvp.phase_timer > MMX_PVP_READY_FRAMES / 2)
      snprintf(out, cap, "ROUND %u", g_pvp.round_no ? g_pvp.round_no : 1u);
    else
      snprintf(out, cap, "READY");
    return 1;
  case MMX_PVP_FIGHT:
    if (!g_pvp.go_timer) return 0;
    snprintf(out, cap, "GO");
    return 1;
  case MMX_PVP_ROUND_END:
    if (g_pvp.last_ko == 0xff) return 0;
    if (g_pvp.last_ko == 2) snprintf(out, cap, "DRAW");
    else snprintf(out, cap, "%s WINS THE ROUND", seat_name(g_pvp.last_ko ^ 1u));
    return 1;
  case MMX_PVP_MATCH_END:
    if (g_pvp.wins[0] == g_pvp.wins[1])
      snprintf(out, cap, "DRAW  %u-%u", g_pvp.wins[0], g_pvp.wins[1]);
    else
      snprintf(out, cap, "%s WINS THE SET", seat_name(g_pvp.wins[1] > g_pvp.wins[0]));
    return 1;
  default:
    return 0;
  }
}

int MmxPvpScoreLine(char *out, size_t cap) {
  if (!out || !cap || !g_pvp.enabled || !g_pvp.gate_ok) return 0;
  if (g_pvp.phase != MMX_PVP_READY && g_pvp.phase != MMX_PVP_FIGHT &&
      g_pvp.phase != MMX_PVP_ROUND_END && g_pvp.phase != MMX_PVP_MATCH_END)
    return 0;
  snprintf(out, cap, "X %u - %u ZERO", g_pvp.wins[0], g_pvp.wins[1]);
  return 1;
}

/* Rows in screen order. Only pad 0 reaches the shared rules. */
enum {
  ROW_SEAT = 0,
  ROW_W0 = 1,
  ROW_W1 = 2,
  ROW_W2 = 3,
  ROW_READY = 4,
  ROW_ARMOR = 5,
  ROW_ZERO = 6,
  ROW_STAGE = 7,
  ROW_LAYOUT = 8
};

static unsigned last_row(unsigned pad) { return pad == 0 ? ROW_LAYOUT : ROW_READY; }

static int seat_of(unsigned pad) {
  if (g_pvp.owner[0] == pad) return 0;
  if (g_pvp.owner[1] == pad) return 1;
  return -1;
}

static int kit_ready(unsigned seat) {
  return MmxPvpPickReady(g_pvp.loadout[seat][0], g_pvp.loadout[seat][1], g_pvp.loadout[seat][2]);
}

static int (*g_page_ok)(unsigned page);

void MmxPvpSetPageCheck(int (*page_ok)(unsigned page)) { g_page_ok = page_ok; }

static int weapon_offered(const MmxVersusCatalogRow *row) {
  if (!row) return 0;
  if (!row->page) return 1;
  return g_page_ok ? g_page_ok(row->page) : 1;
}

static int step_room(int room, int dir) {
  int n = MmxArenaRoomCount(), step, next;
  if (n <= 0) return 0;
  if (room < 0 || room >= n) room = 0;
  next = room;
  for (step = 0; step < n; ++step) {
    next = (next + (dir < 0 ? n - 1 : 1)) % n;
    if (MmxArenaRoomAvailable(next)) return next;
  }
  return room;
}

static void cycle_weapon(unsigned seat, unsigned slot, int dir) {
  int n = MmxVersusCatalogCount(), idx = -1, i, step;
  if (n <= 0 || seat > 1 || slot > 2) return;
  for (i = 0; i < n; ++i)
    if (MmxVersusCatalogAt(i)->id == g_pvp.loadout[seat][slot]) idx = i;
  if (idx < 0) idx = dir < 0 ? 0 : n - 1;
  for (step = 0; step < n; ++step) {
    const MmxVersusCatalogRow *row;
    int k, clash = 0;
    idx = (idx + (dir < 0 ? n - 1 : 1)) % n;
    row = MmxVersusCatalogAt(idx);
    if (!weapon_offered(row)) continue;
    for (k = 0; k < 3; ++k)
      if (k != (int)slot && g_pvp.loadout[seat][k] == row->id) clash = 1;
    if (!clash) {
      g_pvp.loadout[seat][slot] = row->id;
      g_pvp.ready[seat] = 0;
      return;
    }
  }
}

static void claim_seat(unsigned pad) {
  unsigned want = g_pvp.seat_pick[pad] & 1, other;
  if (g_pvp.owner[want] != 0xff && g_pvp.owner[want] != pad) return;
  for (other = 0; other < 2; ++other)
    if (g_pvp.owner[other] == pad) g_pvp.owner[other] = 0xff;
  g_pvp.owner[want] = (uint8_t)pad;
  g_pvp.ready[want] = 0;
}

static void release_seat(unsigned pad) {
  int seat = seat_of(pad);
  if (seat < 0) return;
  g_pvp.owner[seat] = 0xff;
  g_pvp.ready[seat] = 0;
}

static void start_approach(void) {
  g_pvp.phase = MMX_PVP_APPROACH;
  g_pvp.phase_timer = MMX_PVP_ROUND_HOLD;
  g_pvp.entering = 0;
  g_pvp.arena_ready = 0;
  g_pvp.actors_retired = 0;
  g_pvp.gate_ok = 0;
  g_pvp.doors = 0;
}

static void begin_match(int in_stage) {
  MmxArenaRoom room;
  if (g_pvp.owner[0] == 0xff || g_pvp.owner[1] == 0xff) return;
  if (g_pvp.owner[0] == g_pvp.owner[1]) return;
  if (!g_pvp.ready[0] || !g_pvp.ready[1]) return;
  if (!kit_ready(0) || !kit_ready(1)) return;
  if (!MmxArenaRoomAvailable(g_pvp.room) || !MmxArenaRoomGet(g_pvp.room, &room)) return;
  g_pvp.wins[0] = g_pvp.wins[1] = 0;
  g_pvp.last_ko = 0xff;
  g_pvp.round_no = 1;
  g_pvp.go_timer = 0;
  g_pvp.arena_ready = 0;
  g_pvp.actors_retired = 0;
  memset(g_pvp.cooldown, 0, sizeof(g_pvp.cooldown));
  memset(g_pvp.hurt, 0, sizeof(g_pvp.hurt));
  memset(g_pvp.native_hit, 0, sizeof(g_pvp.native_hit));
  memset(g_pvp.imported_hit, 0, sizeof(g_pvp.imported_hit));
  if (in_stage && g_pvp.stage == room.stage_id) {
    /* Same room: the next set starts where the last one ended. */
    g_pvp.entering = 0;
    if (g_pvp.gate_ok) {
      g_pvp.phase = MMX_PVP_READY;
      g_pvp.phase_timer = MMX_PVP_READY_FRAMES;
    } else {
      start_approach();
    }
    return;
  }
  g_pvp.gate_ok = 0;
  g_pvp.doors = 0;
  if (in_stage) {
    /* A different stage: the same mode bytes $00:951C writes for a new game. */
    g_ram[0x1f7a] = room.stage_id;
    g_ram[0xd1] = 2;
    g_ram[0xd2] = 0;
    g_ram[0xd3] = 0;
    g_pvp.entering = 2;
  } else {
    g_pvp.entering = 1;
  }
}

static void edit_row(unsigned pad, int dir) {
  int seat = seat_of(pad);
  unsigned row = g_pvp.row[pad];
  if (row == ROW_SEAT) {
    g_pvp.seat_pick[pad] ^= 1;
    return;
  }
  if (row >= ROW_W0 && row <= ROW_W2) {
    if (seat >= 0) cycle_weapon((unsigned)seat, row - ROW_W0, dir);
    return;
  }
  if (pad != 0 || row == ROW_READY) return;
  g_pvp.ready[0] = g_pvp.ready[1] = 0;
  if (row == ROW_ARMOR) g_pvp.armor ^= 1;
  else if (row == ROW_ZERO) g_pvp.zero_modern ^= 1;
  else if (row == ROW_STAGE) g_pvp.room = (uint8_t)step_room(g_pvp.room, dir);
  else if (row == ROW_LAYOUT)
    g_pvp.layout = g_pvp.layout == MMX_PVP_LAYOUT_PLATFORM ? MMX_PVP_LAYOUT_FLAT : MMX_PVP_LAYOUT_PLATFORM;
}

static void confirm_row(unsigned pad) {
  unsigned row = g_pvp.row[pad];
  int seat = seat_of(pad);
  if (row == ROW_SEAT) {
    claim_seat(pad);
    if (seat_of(pad) >= 0) g_pvp.row[pad] = ROW_W0;
    return;
  }
  if (row >= ROW_W0 && row <= ROW_W2 && seat >= 0) {
    if (!g_pvp.loadout[seat][row - ROW_W0]) cycle_weapon((unsigned)seat, row - ROW_W0, 1);
    g_pvp.row[pad] = (uint8_t)(row + 1);
    return;
  }
  if (row == ROW_READY && seat >= 0 && kit_ready((unsigned)seat))
    g_pvp.ready[seat] ^= 1;
}

void MmxPvpSetupPoll(uint16_t pad0, uint16_t pad1, int in_stage) {
  uint16_t pads[2], pressed[2];
  unsigned pad;
  if (!g_pvp.enabled || g_pvp.phase != MMX_PVP_SETUP) return;
  pads[0] = pad0;
  pads[1] = pad1;
  for (pad = 0; pad < 2; ++pad) {
    pressed[pad] = (uint16_t)(pads[pad] & ~g_pvp.held[pad]);
    g_pvp.held[pad] = pads[pad];
  }
  if (g_pvp.entering == 2) {
    if (!in_stage) g_pvp.entering = 1;
    else if ((pressed[0] | pressed[1]) & 1u) g_pvp.entering = 0;
    return;
  }
  if (g_pvp.entering) {
    if (in_stage) start_approach();
    return;
  }
  for (pad = 0; pad < 2; ++pad) {
    uint16_t p = pressed[pad];
    unsigned last = last_row(pad);
    if (g_pvp.row[pad] > last) g_pvp.row[pad] = 0;
    if (p & (1u << 4))
      g_pvp.row[pad] = g_pvp.row[pad] == 0 ? (uint8_t)last : (uint8_t)(g_pvp.row[pad] - 1);
    else if (p & (1u << 5))
      g_pvp.row[pad] = g_pvp.row[pad] >= last ? 0 : (uint8_t)(g_pvp.row[pad] + 1);
    else if (p & (1u << 6))
      edit_row(pad, -1);
    else if (p & (1u << 7))
      edit_row(pad, 1);
    else if (p & (1u << 8))
      confirm_row(pad);
    else if (p & 1u)
      release_seat(pad);
  }
  begin_match(in_stage);
}

/* The overlay font is uppercase, digits, and a little punctuation. */
static void upper_copy(char *dst, size_t cap, const char *src) {
  size_t i = 0;
  if (!dst || !cap) return;
  if (!src) src = "";
  for (; src[i] && i + 1 < cap; ++i) {
    char c = src[i];
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    else if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == ' ' || c == '-' || c == '.' || c == ',' || c == '\''))
      c = ' ';
    dst[i] = c;
  }
  dst[i] = 0;
}

static int put_text(MmxPvpSetupItem *items, int cap, int n, int x, int y, int scale, int tone,
                    const char *text) {
  MmxPvpSetupItem *it;
  if (n >= cap) return n;
  it = &items[n];
  memset(it, 0, sizeof(*it));
  it->x = (int16_t)x;
  it->y = (int16_t)y;
  it->scale = (uint8_t)scale;
  it->tone = (uint8_t)tone;
  upper_copy(it->text, sizeof it->text, text);
  return n + 1;
}

static int put_centered(MmxPvpSetupItem *items, int cap, int n, int y, int scale, int tone,
                        const char *text) {
  int len = (int)strlen(text), x = (256 - len * 6 * scale) / 2;
  return put_text(items, cap, n, x < 0 ? 0 : x, y, scale, tone, text);
}

static int put_icon(MmxPvpSetupItem *items, int cap, int n, int x, int y, unsigned id) {
  if (n >= cap) return n;
  memset(&items[n], 0, sizeof(items[n]));
  items[n].x = (int16_t)x;
  items[n].y = (int16_t)y;
  items[n].icon = (uint8_t)id;
  return n + 1;
}

static int put_rule(MmxPvpSetupItem *items, int cap, int n, int x, int y, int width) {
  if (n >= cap) return n;
  memset(&items[n], 0, sizeof(items[n]));
  items[n].x = (int16_t)x;
  items[n].y = (int16_t)y;
  items[n].tone = MMX_PVP_TONE_RULE;
  items[n].width = (int16_t)width;
  return n + 1;
}

/* Two lines of at most 40 characters, broken at a space. */
static int put_blurb(MmxPvpSetupItem *items, int cap, int n, int x, int y, const char *lead,
                     const char *blurb) {
  char all[128], line[48];
  int len, cut;
  snprintf(all, sizeof all, "%s %s", lead, blurb);
  len = (int)strlen(all);
  if (len <= 40) return put_text(items, cap, n, x, y, 1, MMX_PVP_TONE_TEXT, all);
  cut = 40;
  while (cut > 0 && all[cut] != ' ') --cut;
  if (cut <= 0) cut = 40;
  snprintf(line, sizeof line, "%.*s", cut, all);
  n = put_text(items, cap, n, x, y, 1, MMX_PVP_TONE_TEXT, line);
  snprintf(line, sizeof line, "  %.38s", all + cut + (all[cut] == ' '));
  return put_text(items, cap, n, x, y + 9, 1, MMX_PVP_TONE_TEXT, line);
}

static int column_x(unsigned seat) { return seat ? 136 : 16; }

static int weapon_row_y(unsigned slot) { return 54 + (int)slot * 20; }

static int row_y(unsigned row) {
  if (row == ROW_SEAT) return 36;
  if (row >= ROW_W0 && row <= ROW_W2) return weapon_row_y(row - ROW_W0);
  if (row == ROW_READY) return 116;
  if (row == ROW_ARMOR || row == ROW_ZERO) return 180;
  if (row == ROW_STAGE) return 192;
  return 204;
}

static const char *setup_hint(void) {
  if (g_pvp.entering == 2) return "LOADING THE STAGE   B CANCELS";
  if (g_pvp.entering) return "LOADING THE STAGE";
  if (g_pvp.owner[0] == 0xff || g_pvp.owner[1] == 0xff)
    return "A TAKES A SEAT   P2 PAD 2 OR KEYBOARD";
  if (!kit_ready(0) || !kit_ready(1)) return "LEFT RIGHT PICKS THREE WEAPONS";
  if (!MmxArenaRoomAvailable(g_pvp.room)) return "THIS STAGE IS NOT READY";
  return "BOTH READY STARTS THE SET";
}

int MmxPvpSetupLayout(MmxPvpSetupItem *items, int cap, int *cover) {
  int n = 0;
  unsigned seat, slot, pad;
  char line[64];
  MmxArenaRoom room;
  if (!items || cap <= 0 || !g_pvp.enabled || g_pvp.phase != MMX_PVP_SETUP) return 0;
  if (cover) *cover = !g_pvp.gate_ok;
  n = put_centered(items, cap, n, 8, 2, MMX_PVP_TONE_ACCENT, "VERSUS");
  n = put_rule(items, cap, n, 8, 28, 240);
  for (seat = 0; seat < 2; ++seat) {
    int cx = column_x(seat), open = g_pvp.owner[seat] == 0xff;
    n = put_text(items, cap, n, cx, row_y(ROW_SEAT), 1, MMX_PVP_TONE_ACCENT, seat_name(seat));
    n = put_text(items, cap, n, cx + 36, row_y(ROW_SEAT), 1, open ? MMX_PVP_TONE_DIM : MMX_PVP_TONE_TEXT,
                 open ? "OPEN" : (g_pvp.owner[seat] == 0 ? "P1" : "P2"));
    for (slot = 0; slot < MMX_PVP_LOADOUT_SLOTS; ++slot) {
      unsigned id = open ? 0 : g_pvp.loadout[seat][slot];
      const MmxVersusCatalogRow *row = MmxVersusCatalogFind(id);
      int y = weapon_row_y(slot);
      if (row) {
        n = put_icon(items, cap, n, cx, y - 5, id);
        n = put_text(items, cap, n, cx + 20, y, 1, MMX_PVP_TONE_TEXT, row->name);
      } else {
        n = put_text(items, cap, n, cx + 20, y, 1, MMX_PVP_TONE_DIM, open ? "-" : "PICK A WEAPON");
      }
    }
    if (!open)
      n = put_text(items, cap, n, cx, row_y(ROW_READY), 1,
                   g_pvp.ready[seat] ? MMX_PVP_TONE_READY : MMX_PVP_TONE_DIM,
                   g_pvp.ready[seat] ? "READY" : (kit_ready(seat) ? "A TO READY" : "PICK 3 WEAPONS"));
  }
  for (seat = 0; seat < 2; ++seat) {
    unsigned p = g_pvp.owner[seat], row;
    const MmxVersusCatalogRow *pick;
    if (p > 1) continue;
    row = g_pvp.row[p];
    if (row < ROW_W0 || row > ROW_W2) continue;
    pick = MmxVersusCatalogFind(g_pvp.loadout[seat][row - ROW_W0]);
    if (!pick) continue;
    n = put_blurb(items, cap, n, 16, seat ? 154 : 134, seat_name(seat), pick->blurb);
  }
  n = put_rule(items, cap, n, 8, 172, 240);
  n = put_text(items, cap, n, 16, row_y(ROW_ARMOR), 1, MMX_PVP_TONE_DIM, "ARMOR");
  n = put_text(items, cap, n, 64, row_y(ROW_ARMOR), 1, MMX_PVP_TONE_TEXT, g_pvp.armor ? "FULL X" : "STOCK X");
  n = put_text(items, cap, n, 136, row_y(ROW_ZERO), 1, MMX_PVP_TONE_DIM, "ZERO");
  n = put_text(items, cap, n, 172, row_y(ROW_ZERO), 1, MMX_PVP_TONE_TEXT, g_pvp.zero_modern ? "MODERN" : "X3");
  if (!MmxArenaRoomGet(g_pvp.room, &room)) room.name = "STAGE";
  n = put_text(items, cap, n, 16, row_y(ROW_STAGE), 1, MMX_PVP_TONE_DIM, "STAGE");
  n = put_text(items, cap, n, 64, row_y(ROW_STAGE), 1,
               MmxArenaRoomAvailable(g_pvp.room) ? MMX_PVP_TONE_TEXT : MMX_PVP_TONE_DIM, room.name);
  n = put_text(items, cap, n, 16, row_y(ROW_LAYOUT), 1, MMX_PVP_TONE_DIM, "FLOOR");
  n = put_text(items, cap, n, 64, row_y(ROW_LAYOUT), 1, MMX_PVP_TONE_TEXT,
               g_pvp.layout ? "PLATFORMS" : "FLAT");
  n = put_text(items, cap, n, 136, row_y(ROW_LAYOUT), 1, MMX_PVP_TONE_DIM, "P1 SETS THE RULES");
  for (pad = 0; pad < 2; ++pad) {
    unsigned row = g_pvp.row[pad];
    int seat_now = seat_of(pad), x;
    if (row > last_row(pad)) continue;
    if (row == ROW_SEAT) x = column_x(g_pvp.seat_pick[pad] & 1u);
    else if (row <= ROW_READY) x = column_x(seat_now >= 0 ? (unsigned)seat_now : (g_pvp.seat_pick[pad] & 1u));
    else if (row == ROW_ZERO) x = 136;
    else x = 16;
    n = put_text(items, cap, n, x - 12 + (int)pad * 6, row_y(row), 1, MMX_PVP_TONE_CURSOR, pad ? "2" : "1");
  }
  if (!g_pvp.entering && g_pvp.gate_ok && (g_pvp.wins[0] || g_pvp.wins[1]) &&
      g_pvp.owner[0] != 0xff && g_pvp.owner[1] != 0xff && kit_ready(0) && kit_ready(1)) {
    snprintf(line, sizeof line, "LAST SET X %u - %u ZERO", g_pvp.wins[0], g_pvp.wins[1]);
    n = put_centered(items, cap, n, 214, 1, MMX_PVP_TONE_ACCENT, line);
  } else {
    n = put_centered(items, cap, n, 214, 1, MMX_PVP_TONE_DIM, setup_hint());
  }
  return n;
}

