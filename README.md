# A) Swap Space

During the creation of processes in the xv6 operating system, physical memory pages are allocated and assigned. As processes consume more memory, the system can run out of available physical memory. To mitigate this issue, the introduction of a swap space in xv6 is necessary. This swap space on disk stores unused memory pages, freeing up memory for other processes. 
### Disk Organization
The current disk layout of xv6 is 
```[ boot block | sb block | log | inode blocks | free bit map | data blocks ]```
To save the unused memory pages on disk, we must reserve a set of disk blocks. Now, we introduce a *swap blocks* partition between sb block and log of the disk layout (refer to *mkfs.c*).
```[ boot block | sb block | swap | log | inode blocks | free bit map | data blocks ]```
We furthur split swap blocks into an array of swap slots, each slot represents eight consecutive disk blocks to store a page. Each slot is of type *struct* with two attributes: *page_perm* (int) and *is_free* (int). *page_perm* stores the permission of a swapped memory page, and *is_free* denotes the availability of a swap slot. Note that we must initialize the array of swap slots at the time of boot.

The process of saving a memory page to disk involves storing the contents of the page into eight consecutive disk blocks. Unlike file system blocks, disk writes to swap blocks bypass the log layer because they store volatile memory pages (refer to *fs.c*). This ensures that the process of saving the contents of the memory pages to disk is not unnecessarily slowed down by the log layer.

### Memory Organization
To allocate a memory page, xv6 invokes the kalloc() function. Currently, the system crashes if we allocate more than the available memory space. To address this issue, we add page swap functionality to xv6 in a new file e.g. pageswap.c. 

#### Page Replacement:
Prior discussing the swapping-in and swapping-out procedures, we first define a policy to find a process and its page for swapping (referred as victim process and victim page). To find a victim process, we first choose a process whose majority of pages are residing in memory. For this, we augment the current *struct proc* with another attribute *rss*, which denotes the amount of memory pages residing on the memory. In case two processes have the same rss value, choose the one with the lower pid. Upon completion of the process, clean the unused swap slots.

To find a victim page, we iterate through the pages of a victim process and choose a page with the `PTE_P` flag set and the `PTE_A` flag unset. The `PTE_P` flag indicates whether the page is present in the memory, and the `PTE_A` flag indicates whether the page is accessed by the process, which is set by the xv6 paging hardware (refer Ch-2 of the xv6 book). 

#### Swapping-out Procedure:
If the available memory space exceeds, we first find the victim page from a victim process. If we fail to find such a page, then we convert 10% of accessed pages to non-accessed by unsetting the `PTE_A` flag. After the update, we again try to find a victim page. The victim page is written into an available swap slot, and we update the page table entry of the swapped-out page.

#### Swapping-in Procedure:
If a process attempts to access a swapped-out page, then x86 will generate the *T_PGFLT* trap signal (refer to *trap.c*). We invoke a page fault handler on receiving this signal. The page fault handler first reads the virtual address of the page from the cr2 register. Then, we find the starting disk block id of the swapped page from the page table entry. To load the page into memory, we first allocate a memory page using kalloc(), then copy data from disk to memory, restore the page premissions, and update the page table entry of the swapped-in page.


# B) Copy-On-Write with Demand Paging

During the fork system call, xv6 creates a new page directory and copies the memory contents of the parent process into the child process. If the process *P1* allocates an array of 1MB and subsequently invokes the fork system call, xv6 assigns an additional 1MB of memory to the child process *P2* while copying the contents of *P1* into *P2*. If neither *P1* nor *P2* modify the 1MB array, then we unnecessarily created a copy. In order to improve memory utilization, we would like to extend xv6 with the copy-on-write (COW) mechanism.

The COW mechanism is a resource management technique that enables the parent and child processes to initially have access to the same memory pages without copying them. When a process, whether it is the parent or child, performs a write operation on the shared page, the page is copied and then altered.

## Design
In this section, we will systematically implement the COW mechanism along with demand paging. For simplicity, we assume that only the same virtual page number of *P1* and *P2* may point to the same physical page number. Consider an example where we create a parent process, referred to as *P1*, and subsequently create its child process, denoted as *P2* using the fork system call.

### 1. Enable Memory Page Sharing
Let's assume that the system has deactivated the swap space. In this part, process *P1* saturates the physical memory by allocating many pages, and then proceeds to execute the fork system call. xv6's fork implementation will fail because the OS will not be able to allocate same amount of memory for *P2* due to insufficient space. We should first address this issue by duplicating the page table entries (PTEs) rather than the memory pages within the `copyuvm` function in `vm.c`. Enabling the sharing of memory pages can result in one process overwriting the contents written by another process. In order to prevent this, we designate the memory page as read-only.

### 2. Handle Writes on Shared Pages
Once *P1* and *P2* have been successfully created, we now need to manage the write operations on the shared memory pages. If a process attempts to write to a shared page that was previously marked as read-only, x86 will generate the `T_PGFLT` trap signal (refer `trap.c`). Upon receiving this signal, the page fault handler shall create a new page with the same contents as the shared page. Subsequently, the page table entry of the process shall be modified to point to the new page.

#### 2.1. Optimization
In the above implementation if P1 and P2 both write to a shared page, we will end up creating three copies of that page in the memory. To reduce one copy, we shall now implement a reverse map data structure called `rmap` that tracks the number of processes that are currently referencing a memory page (*PAGE_ID* &rarr; *#PROCS*). During create or fork operations, we shall increment the value of *rmap*. Likewise, when encountering exit or kill operations, we shall decrement the value of *rmap*. In addition to these activities, during the invocation of the page fault handler, we update the values of *rmap* corresponding to the shared page and the new page accordingly. If we notice that a shared page is accessed by only one process during a page fault, we can directly mark the page as writeable instead of creating a new copy of it.  

### 3. COW with Swapping
Finally, we shall activate the swap space. If a memory page is not available, we may need to remove a shared memory page. With the implementation of Copy-on-Write (COW), it is necessary to update the page table entry (PTE) of not just the victim process, but also any other processes that are accessing the same page. The existing *rmap* just contains the reference count, which is insufficient for this operation. Thus, we extend the *rmap* data structure to retain information about the processes that are associated with the physical page. Now, we can modify the page table entries (PTEs) of the processes that are utilizing the same page. Note that in our implementation, a physical page reverse maps to same virtual page numbers in different processes. 

Additionally, it will be necessary to save the current *rmap* data corresponding to the physical page that is in the swap space. This will ensure when we bring the memory page back from disk into memory at a later time, we must update all the PTEs properly. Note: It is important to update both the swap slot and rmap data structures.

# C) Process Scheduling
The `xv6` scheduler implements a primitive round robin scheduling algorithm. Although this scheduling algorithm ensures that no process is starved of CPU time, there is no notion of "priority" or "types" of processes - all processes get equal CPU time irrespective of the importance of the tasks they perform. In this part, we implemented another fairly simple scheduling algorithm, which will allow processes to specify whether they are "foreground" or "background", and they will be allocated CPU time accordingly. 

## Introduction
Broadly, processes can be classified into the following two types: 
- **Foreground Processes**: These are tasks that require a lot of user interaction, and are time-sensitive. For example, a music player on a phone, or a text editor like Vim running in the terminal. Such tasks need to be scheduled regularly for a smooth user experience.  

- **Background Processes**: These are tasks that are not very time-sensitive. They may be long computational tasks that do not require much user interaction. For example, downloading large data from a server may be a background process. 

We would like to add a functionality using which a process can indicate to the operating system whether it is a foreground process or a background process. The scheduler must also be designed so that time slices are given to processes according to the preference each process indicates. 

## System Calls 
In the first part of the lab, we implemented 2 system calls.
- `int set_sched_policy(void)`: Using this system call, a process can indicate to the operating system whether it is a foreground or a background process. A foreground process would have `policy=0`, while a background process would have `policy=1`. The system call should return `0` if the policy for the process was set successfully, and `-22` in all other cases. 
- `int get_sched_policy(void)`: Using this system call, a process can get the scheduling policy for itself.
## Scheduler 
The scheduling algorithm is simple in principle: Overall, foreground processes must be allotted 90% of the CPU time, and background processes 10%. Within foreground and background processes, we may continue to schedule in a round-robin manner. 
