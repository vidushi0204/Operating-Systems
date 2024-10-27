# Swap Space

During the creation of processes in the xv6 operating system, physical memory pages are allocated and assigned. As processes consume more memory, the system can run out of available physical memory. To mitigate this issue, the introduction of a swap space in xv6 is necessary. This swap space on disk stores unused memory pages, freeing up memory for other processes. The lab aims to add support for the swap space in xv6.
## Design
In this section, we will discuss the design of swap space for xv6.

### Disk Organization
The current disk layout of xv6 is 
```[ boot block | sb block | log | inode blocks | free bit map | data blocks ]```
To save the unused memory pages on disk, we must reserve a set of disk blocks. Now, we introduce a *swap blocks* partition between sb block and log of the disk layout (refer to *mkfs.c*).
```[ boot block | sb block | swap | log | inode blocks | free bit map | data blocks ]```
We furthur split swap blocks into an array of swap slots, each slot represents eight consecutive disk blocks to store a page. Each slot is of type *struct* with two attributes: *page_perm* (int) and *is_free* (int). *page_perm* stores the permission of a swapped memory page, and *is_free* denotes the availability of a swap slot. Note that we must initialize the array of swap slots at the time of boot.

The process of saving a memory page to disk involves storing the contents of the page into eight consecutive disk blocks. Unlike file system blocks, disk writes to swap blocks bypass the log layer because they store volatile memory pages (refer to *fs.c*). This ensures that the process of saving the contents of the memory pages to disk is not unnecessarily slowed down by the log layer.

<font color= yellow>
[AB] For editing the layout on the disk, change the main function in mkfs.c, parameters in param.h
Currently, the number of swap blocks is 24. Making it 40 crashes the OS.
</font>

### Memory Organization
To allocate a memory page, xv6 invokes the kalloc() function. Currently, the system crashes if we allocate more than the available memory space. To address this issue, we add page swap functionality to xv6 in a new file e.g. pageswap.c. 

#### Page Replacement:
Prior discussing the swapping-in and swapping-out procedures, we first define a policy to find a process and its page for swapping (referred as victim process and victim page). To find a victim process, we first choose a process whose majority of pages are residing in memory. For this, we augment the current *struct proc* with another attribute *rss*, which denotes the amount of memory pages residing on the memory. In case two processes have the same rss value, choose the one with the lower pid. Upon completion of the process, clean the unused swap slots.

To find a victim page, we iterate through the pages of a victim process and choose a page with the `PTE_P` flag set and the `PTE_A` flag unset. The `PTE_P` flag indicates whether the page is present in the memory, and the `PTE_A` flag indicates whether the page is accessed by the process, which is set by the xv6 paging hardware (refer Ch-2 of the xv6 book). 

#### Swapping-out Procedure:
If the available memory space exceeds, we first find the victim page from a victim process. If we fail to find such a page, then we convert 10% of accessed pages to non-accessed by unsetting the `PTE_A` flag. After the update, we again try to find a victim page. The victim page is written into an available swap slot, and we update the page table entry of the swapped-out page.

#### Swapping-in Procedure:
If a process attempts to access a swapped-out page, then x86 will generate the *T_PGFLT* trap signal (refer to *trap.c*). We invoke a page fault handler on receiving this signal. The page fault handler first reads the virtual address of the page from the cr2 register. Then, we find the starting disk block id of the swapped page from the page table entry. To load the page into memory, we first allocate a memory page using kalloc(), then copy data from disk to memory, restore the page premissions, and update the page table entry of the swapped-in page.
