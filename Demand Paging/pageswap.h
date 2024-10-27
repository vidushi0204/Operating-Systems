#include "types.h"
#include "param.h"

struct swap_slot{
  int page_perm;  // Permission of a swapped page
  int is_free;    // Availability of swap slot
  uint addr;
};

struct swap_slot swap_slots[NSLOTS]; // Array of swap slots
