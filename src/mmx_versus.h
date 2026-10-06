#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct MmxVersusWeapon {
  int id;
  char name[32];
  char blurb[96];
  int has_demo;
} MmxVersusWeapon;

int MmxVersusWeaponCount(int x2, int x3);
int MmxVersusWeaponGet(int x2, int x3, int index, MmxVersusWeapon *out);
int MmxVersusPose(int id, unsigned tick, int *width, int *height,
                  const uint8_t **pixels, const uint16_t **colors);
void MmxVersusRetryAssets(void);
/* Loads the X2 (1) or X3 (2) weapon art once, from that source ROM. */
int MmxVersusEnsurePage(unsigned page);

#ifdef __cplusplus
}
#endif
