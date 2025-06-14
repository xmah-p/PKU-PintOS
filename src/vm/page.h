#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "frame.h"
#include "vm/vm_region.h"

/* PAGE_FILE: Backed by a file (e.g., executable, mmap)
   PAGE_ZERO: Zeroed page (e.g., stack, heap)
   PAGE_SWAP: Swapped out page (e.g., evicted from memory) */
enum page_type { PAGE_FILE, PAGE_ZERO, PAGE_SWAP };

/* Supplemental page table entry */
struct spt_entry 
  {
    struct hash_elem h_elem;
    upage_t upage;           /* User virtual page (key) */
    enum page_type type;
    struct file *file;       /* Backing file (for PAGE_FILE) */
    off_t ofs;               /* Offset in file */
    size_t read_bytes;       /* Bytes to read */
    size_t zero_bytes;       /* Bytes to zero */
    bool write_back;         /* Should write back to file when swapped out */
    block_sector_t swap_slot; /* Swap slot index if PAGE_SWAP, else -1 */
    bool writable;           /* Writable flag */
    struct lock spte_lock;   /* Lock for this entry */
  };

/* Initialize supplemental page table (called in process creation). */
void spt_init (struct hash *spt);

/* Create and insert new supplemental page table entries for multiple
   contiguous file-backed pages. */
bool spt_install_file_pages (struct file *file, off_t ofs, upage_t upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable,
              enum region_type type);

/* Create and insert a new supplemental page table entry for zeroed page
   upage. */
bool spt_install_zero_page (struct hash *spt, struct lock *spt_lock, 
                            upage_t upage, bool writable);

/* Destroy the supplemental page table, freeing supplemental page
   table entries, swap slots, frame table entries and kernel pages. */
void spt_destroy (struct hash *spt, struct lock *spt_lock);

/* Swap out a dirty page to swap space (PAGE_SWAP) or its backing file
   (PAGE_FILE with write_back == true). */
void spt_swap_out_page (struct hash *spt, struct lock *spt_lock, 
                        upage_t upage, kpage_t kpage);

/* Look up supplemental page table entry by its user virtual page (upage).
   Returns NULL if not found. */
struct spt_entry *suppagedir_lookup (struct hash *spt, upage_t upage);

/* Called on page fault. Load page according to supplemental page table
   entry. */
bool load_page_by_spt (void *fault_addr);

#endif /**< vm/page.h */
