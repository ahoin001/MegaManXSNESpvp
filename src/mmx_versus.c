#include "mmx_versus.h"
#include "mmx_source_assets.h"
#include "mmx_weapons.h"
#include "host_paths.h"
#include "mod_runtime.h"
#include <stdio.h>
#include <string.h>

typedef struct CatalogRow {
  int id;
  const char *name;
  const char *blurb;
  int page;
} CatalogRow;

static const CatalogRow kCatalog[] = {
  {0x01, "Shotgun Ice", "Fires a spread of ice that rides the ground.", 0},
  {0x02, "Electric Spark", "A spark that climbs walls and ceilings.", 0},
  {0x03, "Rolling Shield", "A shield that rolls along the ground.", 0},
  {0x04, "Homing Torpedo", "A missile that turns toward an enemy.", 0},
  {0x05, "Boomerang Cutter", "A cutter that flies out and returns.", 0},
  {0x06, "Chameleon Sting", "A short stab, and a charged invisibility.", 0},
  {0x07, "Storm Tornado", "A rising column of wind.", 0},
  {0x08, "Fire Wave", "A flame that spreads across the ground.", 0},
  {0x11, "Crystal Hunter", "A crystal that traps what it hits.", 1},
  {0x12, "Bubble Splash", "Bubbles that bounce and can be charged into a shield.", 1},
  {0x13, "Silk Shot", "A blob that changes when it hits a surface.", 1},
  {0x14, "Spin Wheel", "A wheel that rolls along the ground.", 1},
  {0x15, "Sonic Slicer", "A cutter that splits into two blades.", 1},
  {0x16, "Strike Chain", "A chain that grabs and pulls.", 1},
  {0x17, "Magnet Mine", "A mine that sticks, then detonates.", 1},
  {0x18, "Speed Burner", "A dash of flame.", 1},
  {0x21, "Acid Burst", "Acid that bursts on contact.", 2},
  {0x22, "Parasitic Bomb", "A bomb that latches on, then blows.", 2},
  {0x23, "Triad Thunder", "A lightning strike around the body.", 2},
  {0x24, "Spinning Blade", "A blade that circles, then launches.", 2},
  {0x25, "Ray Splasher", "A spread of light shots.", 2},
  {0x26, "Gravity Well", "A well that pulls what is nearby.", 2},
  {0x27, "Frost Shield", "A shield of ice that can be planted.", 2},
  {0x28, "Tornado Fang", "A drill charge.", 2},
};

static int s_page_tried[2];

void MmxVersusRetryAssets(void) {
  s_page_tried[0] = s_page_tried[1] = 0;
}

static int ensure_page(unsigned page) {
  char rom[4096], out[4096], leaf[80], error[256];
  const char *package, *resource;
  if (page < 1 || page > 2) return 0;
  if (MmxWeaponsPageEnabled(page)) return 1;
  if (s_page_tried[page - 1]) return 0;
  s_page_tried[page - 1] = 1;
  package = page == 1 ? "megaman-x.weapons.x2" : "megaman-x.weapons.x3";
  resource = page == 1 ? "x2-rom" : "x3-rom";
  rom[0] = 0;
  if (!snes_mod_runtime_resource_path_c(package, "weapons", resource, rom, sizeof(rom)) || !rom[0])
    return 0;
  snprintf(leaf, sizeof(leaf), "cache/mmx-source/x%u-weapons-v5.bin", page + 1);
  if (!snesrecomp_exe_dir_path(leaf, out, sizeof(out))) return 0;
  if (!MmxSourceAssetsBuild(rom, page + 1, 0, out, error, sizeof(error))) return 0;
  return MmxWeaponsLoadPage(out, page) ? 1 : 0;
}

static int row_visible(const CatalogRow *row, int x2, int x3) {
  if (!row) return 0;
  if (row->page == 1) return x2;
  if (row->page == 2) return x3;
  return 1;
}

int MmxVersusWeaponCount(int x2, int x3) {
  int n = 0, i;
  for (i = 0; i < (int)(sizeof(kCatalog) / sizeof(kCatalog[0])); ++i)
    if (row_visible(&kCatalog[i], x2, x3)) ++n;
  return n;
}

int MmxVersusWeaponGet(int x2, int x3, int index, MmxVersusWeapon *out) {
  int seen = 0, i;
  if (!out || index < 0) return 0;
  for (i = 0; i < (int)(sizeof(kCatalog) / sizeof(kCatalog[0])); ++i) {
    if (!row_visible(&kCatalog[i], x2, x3)) continue;
    if (seen++ != index) continue;
    memset(out, 0, sizeof(*out));
    out->id = kCatalog[i].id;
    snprintf(out->name, sizeof(out->name), "%s", kCatalog[i].name);
    snprintf(out->blurb, sizeof(out->blurb), "%s", kCatalog[i].blurb);
    out->has_demo = kCatalog[i].page && ensure_page((unsigned)kCatalog[i].page);
    return 1;
  }
  return 0;
}

int MmxVersusPose(int id, unsigned tick, int *width, int *height,
                  const uint8_t **pixels, const uint16_t **colors) {
  unsigned page = (unsigned)id >> 4, weapon = (unsigned)id & 15;
  const MmxWeaponPose *pose = NULL;
  const uint16_t *palette = NULL;
  if (!width || !height || !pixels || !colors || !page) return 0;
  if (!ensure_page(page)) return 0;
  if (!MmxWeaponsDemoFrame(page, weapon, tick, &pose, &palette) || !pose || !pose->pixels) return 0;
  *width = pose->width;
  *height = pose->height;
  *pixels = pose->pixels;
  *colors = palette;
  return 1;
}
