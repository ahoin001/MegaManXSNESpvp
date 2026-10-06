#include "mmx_versus_catalog.h"
#include <stddef.h>

static const MmxVersusCatalogRow kCatalog[] = {
  {0x01, 0, "Shotgun Ice", "Fires a spread of ice that rides the ground."},
  {0x02, 0, "Electric Spark", "A spark that climbs walls and ceilings."},
  {0x03, 0, "Rolling Shield", "A shield that rolls along the ground."},
  {0x04, 0, "Homing Torpedo", "A missile that turns toward an enemy."},
  {0x05, 0, "Boomerang Cutter", "A cutter that flies out and returns."},
  {0x06, 0, "Chameleon Sting", "A short stab, and a charged invisibility."},
  {0x07, 0, "Storm Tornado", "A rising column of wind."},
  {0x08, 0, "Fire Wave", "A flame that spreads across the ground."},
  {0x11, 1, "Crystal Hunter", "A crystal that traps what it hits."},
  {0x12, 1, "Bubble Splash", "Bubbles that bounce and can be charged into a shield."},
  {0x13, 1, "Silk Shot", "A blob that changes when it hits a surface."},
  {0x14, 1, "Spin Wheel", "A wheel that rolls along the ground."},
  {0x15, 1, "Sonic Slicer", "A cutter that splits into two blades."},
  {0x16, 1, "Strike Chain", "A chain that grabs and pulls."},
  {0x17, 1, "Magnet Mine", "A mine that sticks, then detonates."},
  {0x18, 1, "Speed Burner", "A dash of flame."},
  {0x21, 2, "Acid Burst", "Acid that bursts on contact."},
  {0x22, 2, "Parasitic Bomb", "A bomb that latches on, then blows."},
  {0x23, 2, "Triad Thunder", "A lightning strike around the body."},
  {0x24, 2, "Spinning Blade", "A blade that circles, then launches."},
  {0x25, 2, "Ray Splasher", "A spread of light shots."},
  {0x26, 2, "Gravity Well", "A well that pulls what is nearby."},
  {0x27, 2, "Frost Shield", "A shield of ice that can be planted."},
  {0x28, 2, "Tornado Fang", "A drill charge."},
};

int MmxVersusCatalogCount(void) { return (int)(sizeof(kCatalog) / sizeof(kCatalog[0])); }

const MmxVersusCatalogRow *MmxVersusCatalogAt(int index) {
  if (index < 0 || index >= MmxVersusCatalogCount()) return NULL;
  return &kCatalog[index];
}

const MmxVersusCatalogRow *MmxVersusCatalogFind(unsigned id) {
  int i;
  for (i = 0; i < MmxVersusCatalogCount(); ++i)
    if (kCatalog[i].id == id) return &kCatalog[i];
  return NULL;
}
