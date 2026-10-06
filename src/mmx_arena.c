#include "mmx_arena.h"
#include <string.h>

/* Stage ids match the table in docs/weapons/silk-shot.md. Highway and the
 * fortress are not duel rooms. Each boss sits behind two doors. */
static const MmxArenaRoom kRooms[] = {
  {1, 2, "Launch Octopus", "Ocean base. Tight walls and a wet floor."},
  {2, 2, "Sting Chameleon", "Forest gallery. Vertical room, soft footing."},
  {3, 2, "Armored Armadillo", "Mine conveyor. Low ceiling, solid walls."},
  {4, 2, "Flame Mammoth", "Factory pit. Heat under the floor."},
  {5, 2, "Storm Eagle", "Airship deck. Open sky, hard edges."},
  {6, 2, "Spark Mandrill", "Power plant. Bright floor, close walls."},
  {7, 2, "Boomer Kuwanger", "Tower shaft. Tall room, narrow sides."},
  {8, 2, "Chill Penguin", "Snow cave. Ice underfoot."},
};

enum { ROOM_MAX = 8, STAGE_TABLE_ENTRIES = 13 };

static int8_t g_last_checkpoint[ROOM_MAX] = {-1, -1, -1, -1, -1, -1, -1, -1};

int MmxArenaRoomCount(void) { return (int)(sizeof(kRooms) / sizeof(kRooms[0])); }

int MmxArenaRoomGet(int index, MmxArenaRoom *out) {
  if (!out || index < 0 || index >= MmxArenaRoomCount()) return 0;
  *out = kRooms[index];
  return 1;
}

/* $80:E68E: X = word[$A780 + stage * 2], then word[$A780 + X + checkpoint * 2]
 * with the data bank at $86. A stage's checkpoint count is the gap to the
 * next stage's list. LoROM puts $86:A780 at file offset $32780. */
void MmxArenaSetRom(const uint8_t *rom, size_t size) {
  const size_t base = 6u * 0x8000u + 0x2780u;
  int i;
  for (i = 0; i < ROOM_MAX; ++i) g_last_checkpoint[i] = -1;
  if (!rom) return;
  if ((size & 0x7fff) == 512) { rom += 512; size -= 512; }
  if (size < base + STAGE_TABLE_ENTRIES * 2 + 2) return;
  for (i = 0; i < MmxArenaRoomCount() && i < ROOM_MAX; ++i) {
    unsigned stage = kRooms[i].stage_id, first, next, count;
    if (stage + 1 >= STAGE_TABLE_ENTRIES) continue;
    first = rom[base + stage * 2] | (rom[base + stage * 2 + 1] << 8);
    next = rom[base + stage * 2 + 2] | (rom[base + stage * 2 + 3] << 8);
    if (next <= first || ((next - first) & 1) || base + next > size) continue;
    count = (next - first) / 2;
    if (count < 1 || count > 8) continue;
    g_last_checkpoint[i] = (int8_t)(count - 1);
  }
}

int MmxArenaLastCheckpoint(int index) {
  if (index < 0 || index >= MmxArenaRoomCount() || index >= ROOM_MAX) return -1;
  return g_last_checkpoint[index];
}

int MmxArenaRoomAvailable(int index) { return MmxArenaLastCheckpoint(index) >= 0; }
