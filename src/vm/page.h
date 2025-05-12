/* vm/page.h */
#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"
#include "filesys/file.h"
#include "threads/synch.h"

/* PAGE_BIN: Backed by a file (e.g., executable, mmap)
   PAGE_ZERO: Zeroed page (e.g., stack, heap)
   PAGE_SWAP: Swapped out page (e.g., evicted from memory) */
enum page_type { PAGE_BIN, PAGE_ZERO, PAGE_SWAP };

/* Supplemental page table entry */
struct sup_page_entry 
  {
    struct hash_elem h_elem;
    void *upage;               /* User virtual page (key) */
    enum page_type type;
    struct file *file;        /* Backing file (for PAGE_BIN) */
    off_t ofs;                /* Offset in file */
    size_t read_bytes;        /* Bytes to read */
    size_t zero_bytes;        /* Bytes to zero */
    block_sector_t swap_slot; /* Swap slot index if PAGE_SWAP, else -1 */
    bool writable;
  };

void suppagedir_init (struct hash *spt);

bool suppagedir_install_bin_page (struct hash *spt, void *upage,
                                  struct file *file, off_t ofs,
                                  size_t read_bytes, size_t zero_bytes,
                                  bool writable);

bool suppagedir_install_zero_page (struct hash *spt, void *upage,
                                   bool writable);

void suppagedir_destroy (struct hash *spt);
void suppagedir_set_page_swapped (struct hash *spt, void *upage,
                                 block_sector_t swap_slot, 
                                 struct lock *spt_lock);
bool load_page_from_spt (void *fault_addr);

#endif 
/* vm/page.h */
