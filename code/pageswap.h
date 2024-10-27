#include "types.h"
#include "param.h"
// #include "mmu.h"
#include "memlayout.h"
#define NSLOTS SWAPBLOCKS / 8

struct swap_slot{
  int page_perm;  // Permission of a swapped page
  int is_free;    // Availability of swap slot
  uint addr;
  int rmap;
  pte_t* ptes[NPROC];
};
int rmap[PHYSTOP>>12];
pte_t * ptes[PHYSTOP>>12][NPROC];
struct swap_slot swap_slots[NSLOTS]; // Array of swap slots
