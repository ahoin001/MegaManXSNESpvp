#pragma once
#include <stddef.h>
#include <stdint.h>

/* One boss room from the retail stages. A new room is another row.
 * gate_doors is how many doors stand between the stage's last checkpoint
 * and the boss: the arena locks behind the last one. */
typedef struct MmxArenaRoom {
  uint8_t stage_id;
  uint8_t gate_doors;
  const char *name;
  const char *blurb;
} MmxArenaRoom;

int MmxArenaRoomCount(void);
int MmxArenaRoomGet(int index, MmxArenaRoom *out);
/* The stage's own checkpoint table, $86:A780, indexed by $1F7A then $1F81. */
void MmxArenaSetRom(const uint8_t *rom, size_t size);
/* -1 until the ROM table has been read, or when that stage has no checkpoint. */
int MmxArenaLastCheckpoint(int index);
/* 1 when the room can be chosen: its last checkpoint is known. */
int MmxArenaRoomAvailable(int index);
