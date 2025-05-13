/* vm/page.c */
#include "vm/page.h"

#include <stdio.h>
#include <string.h>

#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "filesys/filesys.h"

/** Each process has a Supplementart Page Table (SPT), which
   is a hash table mapping upage (VPN) to a sup_page_entry struct,
   stored in the thread's proc_info.

   SPT is initialized via spt_init () in start_process ()
   before loading the executable, and is destroyed via
   spt_destroy () in process_exit (). */



/* VPN (upage) as hash key */
static unsigned 
page_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct sup_page_entry *p = hash_entry 
                                      (e, struct sup_page_entry, h_elem);
  return (unsigned) p->upage;
}

/* Compare by VPN (upage) */
static bool 
page_less (const struct hash_elem *a, 
           const struct hash_elem *b, void *aux UNUSED)
{
  struct sup_page_entry *p1 = hash_entry 
                                      (a, struct sup_page_entry, h_elem);
  struct sup_page_entry *p2 = hash_entry 
                                      (b, struct sup_page_entry, h_elem);
  return p1->upage < p2->upage;
}

/* Initialize supplemental page table hash. */
void 
spt_init (struct hash *spt) 
{
  hash_init (spt, page_hash, page_less, NULL);
}

/* Create and insert a new supplemental page table entry for 
   file-backed page upage. 
   Should acquire and release spt_lock before and after! */
bool spt_install_bin_page(struct hash *spt, upage_t upage,
                                 struct file *file, off_t ofs,
                                 size_t read_bytes, size_t zero_bytes,
                                 bool writable) 
{
  struct sup_page_entry *spe = malloc (sizeof *spe);
  if (!spe)
    return false;
  spe->upage = upage;
  spe->type = PAGE_BIN;
  spe->file = file;
  spe->ofs = ofs;
  spe->read_bytes = read_bytes;
  spe->zero_bytes = zero_bytes;
  spe->writable = writable;
  spe->swap_slot = (block_sector_t) -1;
  hash_insert (spt, &spe->h_elem);
  return true;
}

/* Create and insert a new supplemental page table entry for
   zeroed page upage. 
   Should acquire and release spt_lock before and after! */
bool spt_install_zero_page (struct hash *spt, upage_t upage, 
                                   bool writable)
{
  struct sup_page_entry *spe = malloc (sizeof *spe);
  if (!spe)
      return false;
  spe->upage = upage;
  spe->type = PAGE_ZERO;
  spe->file = NULL;
  spe->ofs = 0;
  spe->read_bytes = 0;
  spe->zero_bytes = 0;
  spe->writable = writable;
  spe->swap_slot = (block_sector_t) -1;
  hash_insert (spt, &spe->h_elem);
  return true;
}

/* Lookup SPT entry by user page (NULL if not found). */
static struct sup_page_entry *
suppagedir_find (struct hash *spt, upage_t upage) 
{
  struct sup_page_entry spe = { .upage = upage };
  struct hash_elem *he = hash_find (spt, &spe.h_elem);
  return he ? hash_entry (he, struct sup_page_entry, h_elem) : NULL;
}

/* On page fault: load page from file/swap/zero. */
bool 
load_page_from_spt (void *fault_addr) 
{
  upage_t upage = pg_round_down (fault_addr);
  struct thread *t = thread_current ();
  struct hash *spt = &t->proc_info->sup_page_table;
  struct lock *spt_lock = &t->proc_info->spt_lock;

  kpage_t kpage = frame_alloc (upage);

  lock_acquire (spt_lock);
  struct sup_page_entry *spe_ptr = suppagedir_find (spt, upage);
  if (!spe_ptr) 
    {
      lock_release (spt_lock);
      return false;
    }

  struct sup_page_entry spe = *spe_ptr;
  lock_release (spt_lock);

  if (spe.type == PAGE_ZERO) 
    {
      memset (kpage, 0, PGSIZE);

      if (!pagedir_set_page (t->pagedir, upage, kpage, spe.writable))
        {
          frame_free (kpage);
          return false;
        }
      pagedir_set_dirty (t->pagedir, upage, true);

      frame_set_pinned (kpage, false);
      return true;
    }

  /* Backed by file */
  else if (spe.type == PAGE_BIN) 
    {      
      lock_acquire (&filesys_lock);
      file_seek (spe.file, spe.ofs);
      size_t r = file_read (spe.file, kpage, spe.read_bytes);
      lock_release (&filesys_lock);
      if (r != spe.read_bytes) 
        {
          /* read error */
          frame_free (kpage);
          return false;
        }

      memset (kpage + spe.read_bytes, 0, spe.zero_bytes);   

      bool succ = pagedir_set_page (t->pagedir, upage, kpage, spe.writable);

      frame_set_pinned (kpage, false);
      return succ;
    }

  /* Swapped out */
  else if (spe.type == PAGE_SWAP) 
    {
      swap_read (spe.swap_slot, kpage);

      lock_acquire (spt_lock);
      spe_ptr->swap_slot = (block_sector_t) -1;    /* Do not modify copy */
      lock_release (spt_lock);

      bool succ = pagedir_set_page (t->pagedir, upage, kpage, spe.writable);
      pagedir_set_dirty (t->pagedir, upage, true);

      frame_set_pinned (kpage, false);
      return succ;
    }
  
  NOT_REACHED ();
}

/* Set the swap slot in the supplemental page table entry. 
   Should acquire and release spt_lock before and after! */
void
spt_set_page_swapped (struct hash *spt, upage_t upage,
                             block_sector_t swap_slot)
{
  struct sup_page_entry *spe = suppagedir_find (spt, upage);
  if (spe) 
  {
    spe->type = PAGE_SWAP;
    spe->swap_slot = swap_slot;
  }
}

static void destroy_spe (struct hash_elem *e, void *aux UNUSED);

/* Destroy the supplemental page table, freeing supplemental page
   table entries, swap slots, frame table entries and kernel pages.
   Should acquire and release spt_lock before and after! */
void 
spt_destroy (struct hash *spt) 
{
  hash_destroy (spt, destroy_spe);
}

/* Destroy a supplemental page table entry. 
   Called by hash_destroy (). */
static void
destroy_spe (struct hash_elem *e, void *aux UNUSED)
{
  struct sup_page_entry *spe = hash_entry (e, struct sup_page_entry, h_elem);

  /* Free swap slot if used */
  if (spe->type == PAGE_SWAP && spe->swap_slot != (block_sector_t) -1) 
    swap_free (spe->swap_slot);

  kpage_t kpage = pagedir_get_page (thread_current ()->pagedir, spe->upage);
  if (kpage) 
    {
      frame_free (kpage);
    }
  pagedir_clear_page (thread_current ()->pagedir, spe->upage);
  free (spe);
}
