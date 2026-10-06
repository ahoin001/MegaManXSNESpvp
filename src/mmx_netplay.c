/* MMX's session rules. Transport, lobby discovery and rollback stay shared. */
#include "mmx_netplay.h"
#include "mod_runtime.h"
#include "mmx_coop.h"
#include "mmx_pvp.h"
#include "mmx_versus.h"
#include "mmx_arena.h"
#include "mmx_weapons.h"
#include <stdio.h>
#include <string.h>

#if defined(RECOMP_LAUNCHER) && !MMX_VARIANT_JP
#include "recomp_launcher.h"

static const char *const kCoop = "megaman-x.coop";
static const char *const kZero = "megaman-x.character.zero";
static const char *const kWide = "megaman-x.enhancement.widescreen";
static const char *const kX2 = "megaman-x.weapons.x2";
static const char *const kX3 = "megaman-x.weapons.x3";
static const RecompLauncherCModProvider *s_mods;
static const RecompLauncherCNetplayCallbacks *s_net;
static RecompLauncherCModProvider s_provider;
static RecompLauncherCNetplayCallbacks s_callbacks;
static int s_active;
static int s_versus;
static char s_error[512];

typedef struct VersusPick {
  char name[64];
  int w0, w1, w2;
  int valid;
} VersusPick;
static VersusPick s_picks[8];
static int s_room;
static int s_layout;

int MmxNetplayActive(void) { return s_active; }

int MmxNetplayReady(char *reason, size_t cap) {
  if (!MmxCoopEnabled()) {
    snprintf(reason, cap, "X + Zero co-op could not load. Check the selected X3 ROM and cache directory.");
    return 0;
  }
  for (unsigned page = 1; page <= 2; ++page) {
    const char *id = page == 1 ? "megaman-x.weapons.x2" : "megaman-x.weapons.x3";
    if (snes_mod_runtime_feature_enabled_c(id, "weapons") && !MmxWeaponsPageEnabled(page)) {
      snprintf(reason, cap, "The selected X%u weapon pack could not load.", page + 1);
      return 0;
    }
  }
  return 1;
}

static int fail(const char *message) {
  snprintf(s_error, sizeof(s_error), "%s", message);
  return 0;
}
static const char *mod_error(void *ctx) {
  return s_error[0] ? s_error : s_mods->last_error(ctx);
}
static const char *net_error(void *ctx) {
  return s_error[0] ? s_error : s_net->last_error ? s_net->last_error(ctx) : "";
}
static void clear_error(void *ctx) {
  s_error[0] = 0;
  if (s_net->clear_last_error) s_net->clear_last_error(ctx);
}

static void enable_present_packs(void) {
  static const char *const packages[] = {"megaman-x.weapons.x2", "megaman-x.weapons.x3"};
  static const char *const roms[] = {"x2-rom", "x3-rom"};
  int i;
  if (!s_mods) return;
  for (i = 0; i < 2; ++i) {
    char path[4096];
    path[0] = 0;
    if (snes_mod_runtime_resource_path_c(packages[i], "weapons", roms[i], path, sizeof(path)) && path[0])
      s_mods->feature_enable(s_mods->ctx, packages[i], "weapons", 1);
  }
}

static void mode_changed(int enabled) {
  s_error[0] = 0;
  if (!enabled) {
    snes_mod_runtime_end_temporary_c();
    s_active = 0;
    s_versus = 0;
    memset(s_picks, 0, sizeof(s_picks));
    return;
  }
  if (s_active) return;
  s_mods = snes_mod_runtime_launcher_provider_c();
  if (!s_mods || !snes_mod_runtime_begin_temporary_c()) {
    fail("Netplay requires the bundled X + Zero co-op mod.");
    return;
  }
  s_active = 1;
  s_mods->feature_enable(s_mods->ctx, kZero, "zero", 0);
  if (!s_mods->feature_enable(s_mods->ctx, kCoop, "coop", 1))
    fail("The X + Zero co-op mod is missing. Restore the bundled mods.");
  /* Preserve an existing fixed choice; Adaptive becomes 16:9 in this
   * temporary plan only. Disabled widescreen remains the native view. */
  char aspect[32] = {0};
  snes_mod_runtime_feature_option_value_c(kWide, "widescreen", "aspect", aspect, sizeof(aspect));
  if (!strcmp(aspect, "adaptive"))
    s_mods->feature_set_option(s_mods->ctx, kWide, "widescreen", "aspect", "16:9");
}

static int rules_arena(void) {
  char rules[32] = {0};
  snes_mod_runtime_feature_option_value_c(kCoop, "coop", "rules", rules, sizeof(rules));
  return strcmp(rules, "arena") == 0;
}
static int validate_plan(void) {
  if (!s_active || !snes_mod_runtime_feature_enabled_c(kCoop, "coop"))
    return fail("Netplay requires X + Zero co-op. Reopen Netplay to prepare the room.");
  if (snes_mod_runtime_feature_enabled_c(kZero, "zero"))
    return fail("Character switching must be off during co-op netplay.");
  char cameras[32] = {0};
  snes_mod_runtime_feature_option_value_c(kCoop, "coop", "cameras", cameras, sizeof(cameras));
  if (strcmp(cameras, "independent") && strcmp(cameras, "unified"))
    return fail("Choose Independent or Unified netplay cameras. Restore the bundled co-op mod if this option is missing.");
  if (rules_arena() && strcmp(cameras, "unified"))
    return fail("Versus uses one shared screen. Switch the room to Campaign for separate cameras.");
  if (snes_mod_runtime_feature_enabled_c(kWide, "widescreen")) {
    char aspect[32] = {0};
    snes_mod_runtime_feature_option_value_c(kWide, "widescreen", "aspect", aspect, sizeof(aspect));
    if (strcmp(aspect, "16:9") && strcmp(aspect, "21:9") && strcmp(aspect, "32:9"))
      return fail("Choose a fixed netplay view: 16:9, 21:9, or 32:9.");
  }
  s_error[0] = 0;
  return 1;
}

static int feature_enable(void *ctx, const char *pkg, const char *feature, int enabled) {
  s_error[0] = 0;
  if (s_active && ((!strcmp(pkg, kCoop) && !enabled) || (!strcmp(pkg, kZero) && enabled)))
    return fail("Netplay requires X / Zero Co-op; Add Zero is unavailable.");
  return s_mods->feature_enable(ctx, pkg, feature, enabled);
}
static int set_enabled(void *ctx, const char *pkg, int enabled) {
  if (s_active && ((!strcmp(pkg, kCoop) && !enabled) || (!strcmp(pkg, kZero) && enabled)))
    return fail("Netplay requires X + Zero co-op.");
  s_error[0] = 0;
  return s_mods->set_enabled(ctx, pkg, enabled);
}
static int feature_get(void *ctx, int index, RecompLauncherCModFeature *out) {
  if (!s_mods->feature_get(ctx, index, out)) return 0;
  if (!s_active && !strcmp(out->package_id, kCoop)) {
    RecompLauncherCModOption option;
    for (int i=0; s_mods->feature_option_get(ctx, kCoop, out->id, i, &option); ++i)
      if (!strcmp(option.id, "cameras")) --out->option_count;
  }
  if (s_active && !strcmp(out->package_id, kZero)) out->hidden = 1;
  if (s_active && !strcmp(out->package_id, kCoop) && !out->has_error) {
    if (s_versus || rules_arena()) {
      snprintf(out->name, sizeof(out->name), "Versus");
      snprintf(out->description, sizeof(out->description),
          "Best of three on one screen. Both players share the same health, and specials recharge instead of using energy tanks. The fight starts once both players are in the stage.");
      snprintf(out->status, sizeof(out->status), "Versus lobby");
    } else {
      snprintf(out->status, sizeof(out->status), "Required for netplay");
    }
  }
  return 1;
}
static int option_get(void *ctx, const char *pkg, const char *fid, int index,
                      RecompLauncherCModOption *out) {
  if (!s_active && !strcmp(pkg, kCoop)) {
    int visible=0, found=0;
    for (int i=0; s_mods->feature_option_get(ctx, pkg, fid, i, out); ++i) {
      if (!strcmp(out->id, "cameras")) continue;
      if (visible++==index) { found=1; break; }
    }
    if (!found) return 0;
  } else if (!s_mods->feature_option_get(ctx, pkg, fid, index, out)) return 0;
  if (s_active && !strcmp(pkg, kWide) && !strcmp(out->id, "aspect")) {
    out->choice_count = 3;
    snprintf(out->default_value, sizeof(out->default_value), "16:9");
    snprintf(out->description, sizeof(out->description),
        "The host chooses the same fixed view for both players. Resizing scales the picture. Netplay uses original CRT pixel proportions.");
  }
  if (s_active && !strcmp(pkg, kCoop) && !strcmp(out->id, "rules")) {
    snprintf(out->label, sizeof(out->label), "Room");
    snprintf(out->description, sizeof(out->description),
        "Versus is a best-of-three fight on one screen. Campaign keeps the co-op story and can use separate cameras.");
  }
  if (s_active && !strcmp(pkg, kCoop) && !strcmp(out->id, "cameras") && (s_versus || rules_arena())) {
    out->disabled = 1;
    snprintf(out->description, sizeof(out->description),
        "Versus keeps both players on one screen. Switch the room to Campaign to choose independent cameras.");
  }
  return 1;
}
static int choice_get(void *ctx, const char *pkg, const char *fid, const char *option,
                      int index, RecompLauncherCModChoice *out) {
  if (s_active && !strcmp(pkg, kCoop) && !strcmp(option, "rules")) {
    if (!s_mods->feature_choice_get(ctx, pkg, fid, option, index, out)) return 0;
    if (!strcmp(out->value, "arena"))
      snprintf(out->label, sizeof(out->label), "Versus");
    else if (!strcmp(out->value, "campaign"))
      snprintf(out->label, sizeof(out->label), "Campaign");
    return 1;
  }
  if (s_active && !strcmp(pkg, kWide) && !strcmp(option, "aspect")) {
    static const char *const ratios[] = {"16:9", "21:9", "32:9"};
    if (index < 0 || index >= 3) return 0;
    snprintf(out->value, sizeof(out->value), "%s", ratios[index]);
    snprintf(out->label, sizeof(out->label), "%s", ratios[index]);
    return 1;
  }
  return s_mods->feature_choice_get(ctx, pkg, fid, option, index, out);
}
static int set_option(void *ctx, const char *pkg, const char *fid, const char *option, const char *value) {
  s_error[0] = 0;
  if (!s_active && !strcmp(pkg, kCoop) && !strcmp(option, "cameras"))
    return fail("Offline play always uses Unified cameras. Open Netplay to choose the room's cameras.");
  if (!strcmp(option, "behavior") &&
      ((!strcmp(pkg, kZero) && !strcmp(fid, "zero")) ||
       (!strcmp(pkg, kCoop) && !strcmp(fid, "coop")))) {
    /* One user preference exposed in both mutually exclusive character
     * features. Keep both catalog values identical so whichever feature is
     * active also carries this rule in saves and the netplay agreement.
     * Netplay's temporary plan provides its existing restore-on-exit scope. */
    const char *other = !strcmp(pkg, kZero) ? kCoop : kZero;
    const char *other_fid = !strcmp(pkg, kZero) ? "coop" : "zero";
    char previous[32] = {0};
    snes_mod_runtime_feature_option_value_c(pkg, fid, option, previous, sizeof(previous));
    if (!s_mods->feature_set_option(ctx, pkg, fid, option, value)) return 0;
    if (!s_mods->feature_set_option(ctx, other, other_fid, option, value)) {
      s_mods->feature_set_option(ctx, pkg, fid, option, previous);
      return fail("Cannot update the shared Zero behavior. Restore both bundled character mods.");
    }
    return 1;
  }
  if (s_versus && !strcmp(pkg, kCoop) && !strcmp(option, "rules") && strcmp(value, "arena"))
    return fail("Versus matches use Arena rules.");
  if (s_active && !strcmp(pkg, kCoop) && !strcmp(option, "cameras") &&
      !strcmp(value, "independent") && (s_versus || rules_arena()))
    return fail("Versus uses one shared screen. Switch the room to Campaign for separate cameras.");
  if (s_active && !strcmp(pkg, kWide) && !strcmp(option, "aspect") &&
      strcmp(value, "16:9") && strcmp(value, "21:9") && strcmp(value, "32:9"))
    return fail("Adaptive view is available offline. Choose a fixed ratio for netplay.");
  if (!s_mods->feature_set_option(ctx, pkg, fid, option, value)) return 0;
  if (s_active && !strcmp(pkg, kCoop) && !strcmp(option, "rules") && !strcmp(value, "arena"))
    s_mods->feature_set_option(ctx, kCoop, "coop", "cameras", "unified");
  return 1;
}

static void remember_pick(const char *name, int w0, int w1, int w2) {
  int slot = -1, i;
  if (!name || !name[0] || !MmxPvpPickReady(w0, w1, w2)) return;
  for (i = 0; i < 8; ++i) {
    if (s_picks[i].name[0] && !strcmp(s_picks[i].name, name)) { slot = i; break; }
    if (slot < 0 && !s_picks[i].name[0]) slot = i;
  }
  if (slot < 0) slot = 0;
  snprintf(s_picks[slot].name, sizeof(s_picks[slot].name), "%s", name);
  s_picks[slot].w0 = w0;
  s_picks[slot].w1 = w1;
  s_picks[slot].w2 = w2;
  s_picks[slot].valid = 1;
}
static const VersusPick *find_pick(const char *name) {
  int i;
  if (!name || !name[0]) return NULL;
  for (i = 0; i < 8; ++i)
    if (s_picks[i].valid && !strcmp(s_picks[i].name, name)) return &s_picks[i];
  return NULL;
}
static int chat_is_hidden(void *ctx, int index) {
  RecompLauncherCNetplayChatMessage msg;
  int w0, w1, w2, room, layout;
  if (!s_net->chat_get || !s_net->chat_get(ctx, index, &msg)) return 0;
  if (MmxPvpParsePick(msg.text, &w0, &w1, &w2)) {
    remember_pick(msg.from, w0, w1, w2);
    return 1;
  }
  if (MmxPvpParseMap(msg.text, &room, &layout)) {
    s_room = room;
    s_layout = layout;
    return 1;
  }
  return 0;
}
static int chat_count(void *ctx) {
  int n = s_net->chat_count ? s_net->chat_count(ctx) : 0, i, hidden = 0;
  for (i = 0; i < n; ++i) if (chat_is_hidden(ctx, i)) ++hidden;
  return n - hidden;
}
static int chat_get(void *ctx, int index, RecompLauncherCNetplayChatMessage *out) {
  int n = s_net->chat_count ? s_net->chat_count(ctx) : 0, i, seen = 0;
  if (!out) return 0;
  for (i = 0; i < n; ++i) {
    if (chat_is_hidden(ctx, i)) continue;
    if (seen++ == index) return s_net->chat_get(ctx, i, out);
  }
  return 0;
}
static void publish_map(void *ctx) {
  char line[48];
  snprintf(line, sizeof(line), "%s%d %d", MMX_PVP_MAP_PREFIX, s_room, s_layout);
  if (s_net && s_net->chat_send) s_net->chat_send(ctx, line);
}
static int stage_host(void *ctx) {
  if (!s_net || !s_net->is_host || !s_net->member_count) return 1;
  if (s_net->member_count(ctx) <= 0) return 1;
  return s_net->is_host(ctx);
}
static int member_get(void *ctx, int index, RecompLauncherCNetplayMember *out) {
  const VersusPick *pick;
  if (!s_net->member_get || !s_net->member_get(ctx, index, out)) return 0;
  out->versus_character = out->slot == 0 ? 0 : 1;
  pick = find_pick(out->display_name);
  if (pick) {
    out->versus_pick_valid = 1;
    out->versus_weapon0 = pick->w0;
    out->versus_weapon1 = pick->w1;
    out->versus_weapon2 = pick->w2;
  }
  return 1;
}
static int leave(void *ctx) {
  memset(s_picks, 0, sizeof(s_picks));
  return s_net->leave ? s_net->leave(ctx) : 0;
}
static int lobby_is_arena(void *ctx) {
  int n, i;
  if (!s_net->lobby_mods_count || !s_net->lobby_mods_get) return 0;
  n = s_net->lobby_mods_count(ctx);
  for (i = 0; i < n; ++i) {
    RecompLauncherCNetplayLobbyMod mod;
    memset(&mod, 0, sizeof(mod));
    if (!s_net->lobby_mods_get(ctx, i, &mod)) continue;
    if (strstr(mod.options, "rules=arena")) return 1;
  }
  return 0;
}
static int versus_active(void *ctx) {
  return s_versus || lobby_is_arena(ctx);
}
static int versus_local(void *ctx) {
  (void)ctx;
  s_error[0] = 0;
  s_versus = 0;
  if (!s_mods) return fail("Couch versus needs the bundled co-op mod.");
  s_mods->feature_enable(s_mods->ctx, kZero, "zero", 0);
  if (!s_mods->feature_enable(s_mods->ctx, kCoop, "coop", 1))
    return fail("Couch versus needs X + Zero co-op and the selected X3 ROM.");
  if (!s_mods->feature_set_option(s_mods->ctx, kCoop, "coop", "rules", "arena"))
    return fail("Could not select Arena rules for couch versus.");
  s_mods->feature_set_option(s_mods->ctx, kCoop, "coop", "player1", "x");
  MmxPvpArmArena((unsigned)s_room, (unsigned)s_layout);
  return 1;
}
static int versus_begin(void *ctx) {
  (void)ctx;
  s_versus = 1;
  memset(s_picks, 0, sizeof(s_picks));
  MmxVersusRetryAssets();
  mode_changed(1);
  if (!s_mods) return 0;
  s_mods->feature_set_option(s_mods->ctx, kCoop, "coop", "rules", "arena");
  s_mods->feature_set_option(s_mods->ctx, kCoop, "coop", "cameras", "unified");
  s_mods->feature_set_option(s_mods->ctx, kCoop, "coop", "player1", "x");
  return 1;
}
static int versus_campaign(void *ctx) {
  (void)ctx;
  s_versus = 0;
  memset(s_picks, 0, sizeof(s_picks));
  if (s_mods && s_active)
    s_mods->feature_set_option(s_mods->ctx, kCoop, "coop", "rules", "campaign");
  return 1;
}
static int pack_listed(void *ctx, const char *package, const char *rom) {
  int n, i;
  char path[4096];
  path[0] = 0;
  if (snes_mod_runtime_feature_enabled_c(package, "weapons")) return 1;
  if (snes_mod_runtime_resource_path_c(package, "weapons", rom, path, sizeof(path)) && path[0]) return 1;
  if (!s_net->lobby_mods_count || !s_net->lobby_mods_get) return 0;
  n = s_net->lobby_mods_count(ctx);
  for (i = 0; i < n; ++i) {
    RecompLauncherCNetplayLobbyMod mod;
    memset(&mod, 0, sizeof(mod));
    if (s_net->lobby_mods_get(ctx, i, &mod) && strstr(mod.id, package)) return 1;
  }
  return 0;
}
static int versus_weapon_count(void *ctx) {
  return MmxVersusWeaponCount(pack_listed(ctx, kX2, "x2-rom"), pack_listed(ctx, kX3, "x3-rom"));
}
static int versus_weapon_get(void *ctx, int index, RecompLauncherCVersusWeapon *out) {
  MmxVersusWeapon weapon;
  if (!out || !MmxVersusWeaponGet(pack_listed(ctx, kX2, "x2-rom"), pack_listed(ctx, kX3, "x3-rom"), index, &weapon))
    return 0;
  memset(out, 0, sizeof(*out));
  out->id = weapon.id;
  snprintf(out->name, sizeof(out->name), "%s", weapon.name);
  snprintf(out->blurb, sizeof(out->blurb), "%s", weapon.blurb);
  out->has_demo = weapon.has_demo;
  return 1;
}
static int versus_pose(void *ctx, int weapon_id, unsigned tick, RecompLauncherCVersusPose *out) {
  (void)ctx;
  if (!out) return 0;
  memset(out, 0, sizeof(*out));
  return MmxVersusPose(weapon_id, tick, &out->width, &out->height, &out->pixels, &out->colors);
}
static int versus_pick_set(void *ctx, int w0, int w1, int w2) {
  char line[80];
  const char *name = s_net->player_name ? s_net->player_name(ctx) : "";
  if (!MmxPvpPickReady(w0, w1, w2)) return 0;
  remember_pick(name, w0, w1, w2);
  snprintf(line, sizeof(line), "%s%d %d %d", MMX_PVP_PICK_PREFIX, w0, w1, w2);
  if (s_net->chat_send) s_net->chat_send(ctx, line);
  return 1;
}
static int versus_behavior_get(void *ctx, char *out, size_t cap) {
  (void)ctx;
  if (!out || !cap) return 0;
  out[0] = 0;
  return snes_mod_runtime_feature_option_value_c(kCoop, "coop", "behavior", out, (uint32_t)cap);
}
static int versus_behavior_set(void *ctx, const char *value) {
  int ok = set_option(ctx, kCoop, "coop", "behavior", value);
  if (ok && s_net->push_match_caps) s_net->push_match_caps(ctx);
  return ok;
}
static int versus_stage_count(void *ctx) {
  (void)ctx;
  return MmxArenaRoomCount();
}
static int versus_stage_get(void *ctx, int index, RecompLauncherCVersusStage *out) {
  MmxArenaRoom room;
  (void)ctx;
  if (!out || !MmxArenaRoomGet(index, &room)) return 0;
  memset(out, 0, sizeof(*out));
  out->stage_id = room.stage_id;
  snprintf(out->name, sizeof(out->name), "%s", room.name);
  snprintf(out->blurb, sizeof(out->blurb), "%s", room.blurb);
  return 1;
}
static int versus_stage_current(void *ctx) {
  int room = 0, layout = 0, n, i;
  if (!s_net || !s_net->chat_count) return s_room;
  n = s_net->chat_count(ctx);
  for (i = 0; i < n; ++i) {
    RecompLauncherCNetplayChatMessage msg;
    if (s_net->chat_get && s_net->chat_get(ctx, i, &msg) && MmxPvpParseMap(msg.text, &room, &layout)) {
      s_room = room;
      s_layout = layout;
    }
  }
  return s_room;
}
static int versus_stage_set(void *ctx, int index) {
  if (index < 0 || index >= MmxArenaRoomCount() || !stage_host(ctx)) return 0;
  s_room = index;
  publish_map(ctx);
  return 1;
}
static int versus_layout_get(void *ctx) {
  (void)versus_stage_current(ctx);
  return s_layout;
}
static int versus_layout_set(void *ctx, int layout) {
  if ((layout != 0 && layout != 1) || !stage_host(ctx)) return 0;
  s_layout = layout;
  publish_map(ctx);
  return 1;
}
static int versus_picks_ready(void *ctx) {
  int got[2] = {0, 0}, n, i;
  if (!versus_active(ctx)) return 1;
  n = s_net->member_count ? s_net->member_count(ctx) : 0;
  for (i = 0; i < n; ++i) {
    RecompLauncherCNetplayMember member;
    if (!member_get(ctx, i, &member)) continue;
    if (member.slot < 0 || member.slot > 1 || !member.display_name[0]) continue;
    if (!member.versus_pick_valid) return 0;
    got[member.slot] = 1;
  }
  return got[0] && got[1];
}
static int commit_netplay(void *ctx, const char *rom) {
  int armed = 0;
  if (!validate_plan()) return 0;
  if (versus_active(ctx)) {
    uint8_t character[2] = {0, 1};
    uint8_t loadout[2][MMX_PVP_LOADOUT_SLOTS];
    memset(loadout, 0, sizeof(loadout));
    if (s_net->is_host && s_net->is_host(ctx) && s_mods) {
      s_mods->feature_set_option(s_mods->ctx, kCoop, "coop", "player1", "x");
      s_mods->feature_set_option(s_mods->ctx, kCoop, "coop", "rules", "arena");
      s_mods->feature_set_option(s_mods->ctx, kCoop, "coop", "cameras", "unified");
    }
    /* Weapons, armor, Zero, and the stage are chosen on the game screen. */
    MmxPvpArmLaunch(character, loadout);
    MmxPvpArmArena(0, 0);
    armed = 1;
  }
  if (!s_mods->commit(ctx, rom)) {
    if (armed) MmxPvpClearArm();
    return 0;
  }
  return 1;
}
static int no(void *ctx) { (void)ctx; return 0; }
static int create(void *ctx, const char *name, char *endpoint, const char *password,
                   const RecompLauncherCSettings *settings, int lan, int slots) {
  (void)slots;
  mode_changed(1);
  if (s_versus && s_mods) {
    s_mods->feature_set_option(s_mods->ctx, kCoop, "coop", "rules", "arena");
    s_mods->feature_set_option(s_mods->ctx, kCoop, "coop", "cameras", "unified");
    s_mods->feature_set_option(s_mods->ctx, kCoop, "coop", "player1", "x");
    enable_present_packs();
  }
  if (!validate_plan()) return -1;
  if (s_net->allow_spectators_set) s_net->allow_spectators_set(ctx, 0);
  const char *room = name;
  if ((s_versus || rules_arena()) && (!name || !name[0] || !strcmp(name, "Netplay Lobby")))
    room = "Versus";
  return s_net->create(ctx, room, endpoint, password, settings, lan, 2);
}
static int fill_launch(void *ctx, RecompLauncherCNetplayLaunch *out) {
  if (!s_net->fill_launch(ctx, out)) return 0;
  if (out->is_spectator || out->host_spectates || out->max_slots != 2 || out->player_count != 2)
    return fail("Mega Man X netplay requires exactly two players.");
  return 1;
}

void MmxNetplayConfigureLauncher(RecompLauncherCGameInfo *info) {
  if (!info->netplay_supported || !info->netplay || !info->mods) return;
  s_mods = info->mods;
  s_provider = *s_mods;
  s_provider.feature_enable = feature_enable;
  s_provider.set_enabled = set_enabled;
  s_provider.feature_get = feature_get;
  s_provider.feature_option_get = option_get;
  s_provider.feature_choice_get = choice_get;
  s_provider.feature_set_option = set_option;
  s_provider.commit_netplay = commit_netplay;
  s_provider.last_error = mod_error;
  info->mods = &s_provider;
  info->netplay_mode_changed = mode_changed;
  s_net = info->netplay;
  s_callbacks = *s_net;
  s_callbacks.create = create;
  s_callbacks.fill_launch = fill_launch;
  s_callbacks.member_get = member_get;
  s_callbacks.chat_count = chat_count;
  s_callbacks.chat_get = chat_get;
  s_callbacks.leave = leave;
  s_callbacks.versus_active = versus_active;
  s_callbacks.versus_begin = versus_begin;
  s_callbacks.versus_local = versus_local;
  s_callbacks.versus_campaign = versus_campaign;
  s_callbacks.versus_weapon_count = versus_weapon_count;
  s_callbacks.versus_weapon_get = versus_weapon_get;
  s_callbacks.versus_pose = versus_pose;
  s_callbacks.versus_pick_set = versus_pick_set;
  s_callbacks.versus_behavior_get = versus_behavior_get;
  s_callbacks.versus_behavior_set = versus_behavior_set;
  s_callbacks.versus_picks_ready = versus_picks_ready;
  s_callbacks.versus_stage_count = versus_stage_count;
  s_callbacks.versus_stage_get = versus_stage_get;
  s_callbacks.versus_stage_current = versus_stage_current;
  s_callbacks.versus_stage_set = versus_stage_set;
  s_callbacks.versus_layout_get = versus_layout_get;
  s_callbacks.versus_layout_set = versus_layout_set;
  s_callbacks.last_error = net_error;
  s_callbacks.clear_last_error = clear_error;
  s_callbacks.allow_spectators_get = NULL;
  s_callbacks.allow_spectators_set = NULL;
  s_callbacks.host_can_spectate = no;
  s_callbacks.automatch_available = no; /* Co-op has no competitive queue. */
  info->netplay = &s_callbacks;
}

int MmxNetplayPrepare(int from_lobby, char *reason, size_t cap) {
  if (!from_lobby) mode_changed(1);
  if (validate_plan()) return 1;
  snprintf(reason, cap, "%s", s_error);
  return 0;
}
#else
int MmxNetplayActive(void) { return 0; }
int MmxNetplayReady(char *reason, size_t cap) { return MmxNetplayPrepare(0, reason, cap); }
void MmxNetplayConfigureLauncher(struct RecompLauncherCGameInfo *info) { (void)info; }
int MmxNetplayPrepare(int from_lobby, char *reason, size_t cap) {
  (void)from_lobby;
  snprintf(reason, cap, "Netplay requires the USA co-op build.");
  return 0;
}
#endif
