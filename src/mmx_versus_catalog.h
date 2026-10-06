#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Every weapon a versus kit can hold. id is (page << 4) | weapon. */
typedef struct MmxVersusCatalogRow {
  uint8_t id;
  uint8_t page;
  const char *name;
  const char *blurb;
} MmxVersusCatalogRow;

int MmxVersusCatalogCount(void);
const MmxVersusCatalogRow *MmxVersusCatalogAt(int index);
const MmxVersusCatalogRow *MmxVersusCatalogFind(unsigned id);

#ifdef __cplusplus
}
#endif
