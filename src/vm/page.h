#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "frame.h"

/* PAGE_BIN: Backed by a file (e.g., executable)
   PAGE_ZERO: Zeroed page (e.g., stack, heap)
   PAGE_SWAP: Swapped out page (e.g., evicted from memory) */
enum page_type { PAGE_BIN, PAGE_ZERO, PAGE_SWAP };

/* Supplemental page table entry */
struct spt_entry 
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
    struct lock spte_lock;    /* Lock for this entry */
  };

/* Initialize supplemental page table (called in process creation). */
void spt_init (struct hash *spt);

/* Create and insert a new supplemental page table entry for file-backed 
   page upage. */
bool spt_install_bin_page (struct hash *spt, upage_t upage,
                                  struct file *file, off_t ofs,
                                  size_t read_bytes, size_t zero_bytes,
                                  bool writable);

/* Create and insert a new supplemental page table entry for zeroed page
   upage. */
bool spt_install_zero_page (struct hash *spt, upage_t upage,
                                   bool writable);

/* Destroy the supplemental page table, freeing supplemental page
   table entries, swap slots, frame table entries and kernel pages. */
void spt_destroy (struct hash *spt, struct lock *spt_lock);

/* Set swap slot in the supplemental page table entry. */
void spt_set_page_swapped (struct hash *spt, struct lock *spt_lock,
                           upage_t upage, block_sector_t swap_slot);

/* Called on page fault. Load page according to supplemental page table
   entry. */
bool load_page_by_spt (void *fault_addr);

#endif /**< vm/page.h */
