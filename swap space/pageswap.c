/* 
Policy for swapping in and swapping out:
    * To find a victim process, we first choose a process whose majority of pages are residing in memory. 
        - For this, we augment the current struct proc with another attribute rss, which denotes the amount of memory pages residing on the memory. 
        - In case two processes have the same rss value, choose the one with the lower pid. Upon completion of the process, clean the unused swap slots.

    *To find a victim page, we iterate through the pages of a victim process and choose a page with the PTE_P flag set and the PTE_A flag unset. 
        - The PTE_P flag indicates whether the page is present in the memory, 
        - The PTE_A flag indicates whether the page is accessed by the process, which is set by the xv6 paging hardware
Swapping-out Procedure:
If the available memory space exceeds, we first find the victim page from a victim process. If we fail to find such a page, then we convert 10% of accessed pages to non-accessed by unsetting the PTE_A flag. After the update, we again try to find a victim page. The victim page is written into an available swap slot, and we update the page table entry of the swapped-out page.

Swapping-in Procedure:
If a process attempts to access a swapped-out page, then x86 will generate the T_PGFLT trap signal (refer to trap.c). We invoke a page fault handler on receiving this signal. The page fault handler first reads the virtual address of the page from the cr2 register. Then, we find the starting disk block id of the swapped page from the page table entry. To load the page into memory, we first allocate a memory page using kalloc(), then copy data from disk to memory, restore the page premissions, and update the page table entry of the swapped-in page.

*/

#include "pageswap.h"
#include "param.h"
#include "defs.h"
#include "mmu.h"
#include "memlayout.h"
#include "proc.h"
#include "x86.h"

#define GETIDX(addr) ((addr-2)/8)

void 
swapinit()
{
    for(int i=0; i<NSLOTS; i++){
        swap_slots[i].page_perm = 0;
        swap_slots[i].is_free = 1;
        swap_slots[i].addr = (i*8+2);
   }
   cprintf("Swapslots initialized!\n");
}

struct swap_slot *
find_free_swap_slot(){
    for(int i=0; i<NSLOTS; i++){
        if (swap_slots[i].is_free == 1){
            return &swap_slots[i];
        }
   }
   panic("No free swap slot found!");
}

void 
swap_out(){
    /* Find a free swap block on disk
        • Copy page to the free block
        • Run INVLPG instruction to remove page from TLB
        • Mark not present, remember swap block number in PTE
        • Add page to free list
    */
    struct proc * p = find_victim_proc();
    pte_t* pte = find_victim_page(p);
    struct swap_slot* sslot = find_free_swap_slot();
    writepage(ROOTDEV, (char*) P2V(PTE_ADDR(*pte)), sslot->addr);
    // cprintf("Page written successfully!\n");
    kfree((char*)P2V(PTE_ADDR(*pte)));
    p->rss -= PGSIZE; // [AB]
    // cprintf("Page freed successfully!\n");
    // Configure swap slot
    sslot->is_free = 0;
    sslot->page_perm = PTE_FLAGS(*pte);

    // Configure pte
    int index = GETIDX(sslot->addr);
    *pte = index << 12;
    *pte |= sslot->page_perm;
    *pte |= PTE_S;
    *pte &= (~PTE_P);
    // *pte |= 0x008; // [AB] Why this?
    // cprintf("Index of swap slot : %d", index);
    lcr3(V2P(p->pgdir)); // [AB]
}

void 
swap_in(){
    uint va = rcr2();
    char* newpage = kalloc();
    struct proc * p = myproc();
    pte_t * pte = walkpgdir(p->pgdir, (void*) va,0);
    if ((*pte & PTE_S) == 0){
        panic("Not a swapped out page!");
    }
    p->rss += PGSIZE;
    int index = *pte >> 12;
    // cprintf("Index received in swapin: %d", index);
    // configure pte
    *pte = PTE_ADDR(V2P(newpage));
    *pte |= swap_slots[index].page_perm; 
    *pte |= PTE_P;
    // Empty swap slot
    readpage(ROOTDEV, newpage, swap_slots[index].addr);
    swap_slots[index].is_free = 1;
    swap_slots[index].page_perm=0;
    lcr3(V2P(p->pgdir));
    // cprintf("Read page from disk!\n");
}

void freeslot(int x){
    swap_slots[x].is_free = 1;
}