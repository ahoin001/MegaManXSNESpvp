#include "mmx_pvp.h"
#include <string.h>

static MmxPvpState g_pvp;
static MmxPvpState g_pending;
static unsigned g_seat;
static uint8_t g_pending_valid;
static uint8_t g_arm_valid;
static uint8_t g_boot_skip;
static uint8_t g_arm_character[2];
static uint8_t g_arm_loadout[2][MMX_PVP_LOADOUT_SLOTS];

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
  body[0x27] = (uint8_t)((body[0x27] & 0x80) | hp);
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
void MmxPvpClearArm(void) { g_arm_valid = 0; }
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
    g_boot_skip = 1;
  } else if (g_pending_valid) {
    g_pvp = g_pending;
    g_pvp.enabled = 1;
    g_pending_valid = 0;
    g_boot_skip = 0;
  } else {
    MmxPvpInit(&g_pvp);
    g_boot_skip = 0;
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
  if (!s || s->enabled > 1 || s->phase > MMX_PVP_MATCH_END || s->best_of > 9) return false;
  if (s->enabled && s->best_of < 1) return false;
  if (s->phase_timer > MMX_PVP_ROUND_HOLD || s->baseline_hp > 127 || s->arena_ready > 1 || s->stage > 15)
    return false;
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
bool MmxPvpInputLocked(void) { return g_pvp.enabled && g_pvp.phase != MMX_PVP_FIGHT; }
bool MmxPvpBlocksCampaignDeath(void) { return MmxPvpInputLocked(); }
bool MmxPvpArenaReady(void) { return g_pvp.enabled && g_pvp.arena_ready; }

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
  if (!g_pvp.enabled || !g_boot_skip || gameplay) return 0;
  return frame == 120 || frame == 240 || frame == 400 || frame == 560 || frame == 720;
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
  s->phase = MMX_PVP_ROUND_END;
  s->phase_timer = MMX_PVP_ROUND_HOLD;
  return true;
}

bool MmxPvpRegisterDraw(MmxPvpState *s) {
  if (!s || !s->enabled || s->phase != MMX_PVP_FIGHT) return false;
  s->phase = MMX_PVP_ROUND_END;
  s->phase_timer = MMX_PVP_ROUND_HOLD;
  return true;
}

int MmxPvpTickClock(MmxPvpState *s) {
  if (!s || !s->enabled) return 0;
  if (s->phase == MMX_PVP_FIGHT) {
    MmxPvpTickCooldowns(s);
    return 0;
  }
  if (s->phase != MMX_PVP_ROUND_END) return 0;
  if (s->phase_timer) s->phase_timer--;
  if (s->phase_timer) return 0;
  if (s->wins[0] >= wins_needed(s) || s->wins[1] >= wins_needed(s)) {
    s->phase = MMX_PVP_MATCH_END;
    return 0;
  }
  s->phase = MMX_PVP_FIGHT;
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
  seat->body[4] = 0;
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
  if (stage_id != g_pvp.stage) {
    g_pvp.stage = stage_id;
    g_pvp.arena_ready = 0;
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
  if (seats[0].status == MMX_PVP_SEAT_ALIVE && seats[1].status == MMX_PVP_SEAT_ALIVE &&
      seats[0].body && seats[1].body && !g_pvp.arena_ready) {
    MmxPvpBuildArena(read_coord(seats[0].body, 5), read_coord(seats[0].body, 8),
                     read_coord(seats[1].body, 5), read_coord(seats[1].body, 8),
                     min_x, max_x, min_y, max_y, &g_pvp.arena);
    g_pvp.arena_ready = 1;
    publish_hp(&seats[0], g_pvp.baseline_hp);
    publish_hp(&seats[1], g_pvp.baseline_hp);
    equip_first(&seats[0], 0);
    equip_first(&seats[1], 1);
  }
  if (g_pvp.phase == MMX_PVP_FIGHT &&
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
  } else if (g_pvp.phase != MMX_PVP_FIGHT) {
    for (i = 0; i < 2; ++i)
      if (seats[i].body && read_hp(seats[i].body) == 0)
        write_hp(seats[i].body, 1);
  }
  if (g_pvp.arena_ready) {
    clamp_seat(&g_pvp.arena, &seats[0]);
    clamp_seat(&g_pvp.arena, &seats[1]);
  }
}
