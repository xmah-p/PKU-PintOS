/* vm/swap.c */
#include "vm/swap.h"

#include <bitmap.h>

#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"

/** The swap subsystem organizes the swap block device as a bitmap
   of slots. Each slot is a page-sized chunk of the swap device. It
   provides a layer of abstraction over the swap block device, enabling
   page-level access without operating on the block device sectors.

   swap_init () are called in threads/init.c.
   swap_write () is called in vm/frame.c when a page is evicted from
   memory to swap. It returns the slot index of the swap device where the page
   was written, which is then stored in the page's supplemental page table entry.
   swap_read () is called on page fault for swap-backed pages.
   swap_free () is called in suppagedir_destroy () to free the swap slot
   when the process exits. */

static struct bitmap *swap_table;  /* Bitmap of swap slots, false = free */
static struct lock swap_lock;      /* Lock for swap table access */
static struct block *swap_block;   /* Swap block device */

static const size_t SECTORS_PER_PAGE = PGSIZE / BLOCK_SECTOR_SIZE;

/* Initialize swap subsystem (bitmap) */
void 
swap_init (void) 
{
  swap_block = block_get_role (BLOCK_SWAP);
  if (!swap_block) return; /* No swap device */

  size_t sectors = block_size (swap_block);
  size_t pages = sectors * BLOCK_SECTOR_SIZE / PGSIZE;
  swap_table = bitmap_create (pages);
  bitmap_set_all (swap_table, false);
  lock_init (&swap_lock);
}

/* Write one page from frame into an unused swap slot.
   Return the slot index. */
block_sector_t 
swap_write (void *frame) 
{
  lock_acquire (&swap_lock);
  size_t slot = bitmap_scan_and_flip (swap_table, 0, 1, false);
  if (slot == BITMAP_ERROR) 
    {
      lock_release (&swap_lock);
      PANIC("swap: out of space");
    }

  /* Write sectors to swap device */
  for (int i = 0; i < SECTORS_PER_PAGE; i++)
    block_write (swap_block, 
                 slot * SECTORS_PER_PAGE + i, 
                 frame + i * BLOCK_SECTOR_SIZE);
  lock_release (&swap_lock);
  return (block_sector_t) slot;
}

/* Read a page back from swap slot into frame. The slot will be
   marked as free in the swap table. */
void 
swap_read (block_sector_t slot, void *frame) 
{
  lock_acquire (&swap_lock);
  for (int i = 0; i < SECTORS_PER_PAGE; i++)
    block_read (swap_block, 
                slot * SECTORS_PER_PAGE + i, 
                frame + i * BLOCK_SECTOR_SIZE);
  bitmap_reset (swap_table, slot);
  lock_release (&swap_lock);
}

/* Free a swap slot (e.g. on process exit). */
void 
swap_free (block_sector_t slot) 
{
  if (slot != (block_sector_t) -1) 
    {
      lock_acquire (&swap_lock);
      bitmap_reset (swap_table, slot);
      lock_release (&swap_lock);
    }
}
