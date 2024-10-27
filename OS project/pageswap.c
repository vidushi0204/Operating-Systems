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
        swap_slots[i].rmap = 0;
        for(int j=0; j<NPROC; j++) swap_slots[i].ptes[j]=0;
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
    // cprintf("Victim pid %d\n", p->pid);
    pte_t* pte = find_victim_page(p);
    // cprintf("Victim pte %x\n", *pte);
    uint physical_page = PTE_ADDR(*pte);
    // cprintf("Victim page %x\n", physical_page);
    struct swap_slot* sslot = find_free_swap_slot();
    int index = GETIDX(sslot->addr);
    writepage(ROOTDEV, (char*) P2V(physical_page), sslot->addr);
    // cprintf("Page written successfully!\n");
    
    // Configure swap slot
    sslot->is_free = 0;
    sslot->page_perm = PTE_FLAGS(*pte);
    sslot->rmap = rmap[physical_page>>12];

    for(int i=0; i<NPROC;i++){
        sslot->ptes[i]=0;
    }

    rmap[physical_page>>12]=0;
    kfree((char*)P2V(physical_page));
    // cprintf("Page freed successfully!\n"); 

    // Configure pte
    pte_t ** tempptes = ptes[physical_page>>12];
    for(int i=0; i<NPROC; i++){
        if(tempptes[i]!=0){
            *tempptes[i] = index << 12;
            *tempptes[i] |= sslot->page_perm;
            *tempptes[i] |= PTE_S;
            *tempptes[i] &= (~PTE_P);
            get_proc_by_index(i)->rss -= PGSIZE; // [AB]
            lcr3(V2P(get_proc_by_index(i)->pgdir)); // [AB]
        }
        sslot->ptes[i]=tempptes[i];
        tempptes[i]=0;
        // cprintf("sslot->ptes[i]=%x\n", sslot->ptes[i]);
    }
    // cprintf("Swapout done!\n"); 
}

void 
swap_in(){
    // cprintf("swap_in called\n");
    uint va = rcr2();
    char* newpage = kalloc();
    // Need to do this for all the processes mapping to it
    struct proc * p = myproc();
    pte_t * pte = walkpgdir(p->pgdir, (void*) va,0);
    if ((*pte & PTE_S) == 0){
        panic("Not a swapped out page!");
    }
    // cprintf("Found pte successfully!\n");
    int index = *pte >> 12;
    rmap[V2P(newpage)>>12] = swap_slots[index].rmap;
    // cprintf("Index received in swapin: %d", index);
    // configure pte for all ptes
    for(int i=0; i<NPROC; i++){
        if (swap_slots[index].ptes[i]!=0){
            struct proc* rmap_proc = get_proc_by_index(i);
            // pte_t * rmap_pte = walkpgdir(rmap_proc->pgdir,(void *) va, 0);
            pte_t * rmap_pte = swap_slots[index].ptes[i];
            *rmap_pte = PTE_ADDR(V2P(newpage));
            *rmap_pte |= swap_slots[index].page_perm; 
            *rmap_pte |= PTE_P;
            *rmap_pte &= (~PTE_S) ;
            ptes[V2P(newpage)>>12][i] = rmap_pte;
            rmap_proc->rss += PGSIZE;
            lcr3(V2P(rmap_proc->pgdir));
        }
        else{
            ptes[V2P(newpage)>>12][i] = 0;
        }
    }
    // Empty swap slot
    readpage(ROOTDEV, newpage, swap_slots[index].addr);
    swap_slots[index].is_free = 1;
    swap_slots[index].page_perm = 0;
    swap_slots[index].rmap = 0;
    for(int i=0; i<NPROC; i++) swap_slots[index].ptes[i] = 0; 
    
    // cprintf("Read page from disk!\n");
}

void
copy_on_write(){
    uint va;
    char* newpage, *oldpage;
    struct proc* p;
    pte_t * pte;
    int count;

    va = rcr2();
    p = myproc();

    pte = walkpgdir(p->pgdir, (void*) va, 0);
    if ((*pte & PTE_S) && !(*pte & PTE_P)){
      swap_in();
      return;
    }
    else if (!(*pte & PTE_W)){
        // cprintf("cow called\n");
        // rmap pa >> 12 
        oldpage = (char*) P2V(PTE_ADDR(*pte));
        count = get_rmap(PTE_ADDR(*pte));
        // if (DEBUG_BIT) cprintf("Count in cow: %d\t Pid %d\t Page 0x%x\n",count, p->pid, *pte);
        if (count==1){ // Exclusive page
            *pte |= PTE_W;
            lcr3(V2P(p->pgdir));
        }
        else{
            if((newpage = kalloc())==0) 
                panic("Out of memory in cow"); 
            memmove(newpage, oldpage, PGSIZE);
            *pte = V2P(newpage) | PTE_FLAGS(*pte) | PTE_W;
            add_pte((uint)V2P(newpage), p->index, pte); // Add the new page table entry
            //   decrement_rmap(V2P(oldpage)); // Decrement for oldpage
            if (DEBUG_BIT) cprintf("subpte from cow\n");
            if (*pte & PTE_P)
                sub_pte((uint)V2P(oldpage), p->index); // Remove the old pte
            else if (*pte & PTE_S)
                freeslot(PTE_ADDR(*pte)>>12,p->index);
        }
        lcr3(V2P(p->pgdir));
    }
    else{
        cprintf("Pid %d\t Page 0x%x\n", p->pid, *pte);
        panic ("PAGE FAULT CANT BE HANDLED") ;
    }
}


// Some helper functions

void 
freeslot(int swapindex,int x){ // TODO UPDATE
    // if (swap_slots[swapindex].ptes[pindex]==0){
    //     panic("Index already 0 in freeslot");
    // }
    swap_slots[swapindex].rmap--; // Setting 0 for no pte
    if (swap_slots[swapindex].rmap<=0) swap_slots[swapindex].is_free=1;
    // change_rss(pindex, -1); // Check
}

void 
addslot(int swapindex, int pindex, pte_t* pte){
    // if (swap_slots[swapindex].ptes[pindex]!=0){
    //     panic("Index already occupied in addslot");
    // }
    swap_slots[swapindex].ptes[pindex] = pte; // Setting 0 for no pte
    swap_slots[swapindex].rmap++;
    // change_rss(pindex, -1); // Check
}

int
get_rmap(uint pa){
  return rmap[pa>>12];
}

void 
clear_rmap(uint pa){
  rmap[pa>>12] = 0;
}

void
add_pte(uint pa, int index, pte_t* pte){
  ptes[pa>>12][index] = pte;
  rmap[pa>>12]++;
  get_proc_by_index(index)->rss+=PGSIZE;
}

void 
sub_pte(uint pa, int index){
    if (DEBUG_BIT) cprintf("in subpte 0x%x\t %d\t %d \n", pa, index, rmap[pa>>12]);
    if (ptes[pa>>12][index]==0){
        // panic("Index already 0 in sub_pte");
        return;
    }
    ptes[pa>>12][index] = 0; // Setting 0 for no pte
    rmap[pa>>12]--;
    get_proc_by_index(index)->rss-=PGSIZE; // Check
}

pte_t** 
get_ptes(uint pa){
  return ptes[pa>>12];
}