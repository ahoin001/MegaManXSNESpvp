#include "mmx_versus.h"
#include "mmx_versus_catalog.h"
#include "mmx_source_assets.h"
#include "mmx_weapons.h"
#include "host_paths.h"
#include "mod_runtime.h"
#include <stdio.h>
#include <string.h>

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

int MmxVersusEnsurePage(unsigned page) { return ensure_page(page); }

static int row_visible(const MmxVersusCatalogRow *row, int x2, int x3) {
  if (!row) return 0;
  if (row->page == 1) return x2;
  if (row->page == 2) return x3;
  return 1;
}

int MmxVersusWeaponCount(int x2, int x3) {
  int n = 0, i;
  for (i = 0; i < MmxVersusCatalogCount(); ++i)
    if (row_visible(MmxVersusCatalogAt(i), x2, x3)) ++n;
  return n;
}

int MmxVersusWeaponGet(int x2, int x3, int index, MmxVersusWeapon *out) {
  int seen = 0, i;
  if (!out || index < 0) return 0;
  for (i = 0; i < MmxVersusCatalogCount(); ++i) {
    const MmxVersusCatalogRow *row = MmxVersusCatalogAt(i);
    if (!row_visible(row, x2, x3)) continue;
    if (seen++ != index) continue;
    memset(out, 0, sizeof(*out));
    out->id = row->id;
    snprintf(out->name, sizeof(out->name), "%s", row->name);
    snprintf(out->blurb, sizeof(out->blurb), "%s", row->blurb);
    out->has_demo = row->page && ensure_page(row->page);
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
