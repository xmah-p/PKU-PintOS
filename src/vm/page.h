#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "frame.h"

/* PAGE_BIN: Backed by a file (e.g., executable, mmap)
   PAGE_ZERO: Zeroed page (e.g., stack, heap)
   PAGE_SWAP: Swapped out page (e.g., evicted from memory) */
enum page_type { PAGE_BIN, PAGE_ZERO, PAGE_SWAP };

/* Supplemental page table entry */
struct sup_page_entry 
  {
    struct hash_elem h_elem;
    upage_t upage;               /* User virtual page (key) */
    enum page_type type;
    struct file *file;        /* Backing file (for PAGE_BIN) */
    off_t ofs;                /* Offset in file */
    size_t read_bytes;        /* Bytes to read */
    size_t zero_bytes;        /* Bytes to zero */
    block_sector_t swap_slot; /* Swap slot index if PAGE_SWAP, else -1 */
    bool writable;
  };

void spt_init (struct hash *spt);

bool spt_install_bin_page (struct hash *spt, upage_t upage,
                                  struct file *file, off_t ofs,
                                  size_t read_bytes, size_t zero_bytes,
                                  bool writable);

bool spt_install_zero_page (struct hash *spt, upage_t upage,
                                   bool writable);

void spt_destroy (struct hash *spt);
void spt_set_page_swapped (struct hash *spt, upage_t upage,
                                 block_sector_t swap_slot);
bool load_page_from_spt (void *fault_addr);

#endif /**< vm/page.h */
