/* vm/page.c */
#include "vm/page.h"

#include <stdio.h>

#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "filesys/filesys.h"
#include <string.h>

/** Each process has a Supplementart Page Table (SPT), which
   is a hash table mapping upage (VPN) to a sup_page_entry struct,
   stored in the thread's proc_info.

   SPT is initialized via suppagedir_init () in start_process ()
   before loading the executable, and is destroyed via
   suppagedir_destroy () in process_exit (). */

/* VPN (upage) as hash key */
static unsigned 
page_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct sup_page_entry *p = hash_entry 
                                      (e, struct sup_page_entry, h_elem);
  return hash_bytes (&p->upage, sizeof p->upage);
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
suppagedir_init (struct hash *spt) 
{
  hash_init (spt, page_hash, page_less, NULL);
}

/* Create and insert a new supplemental page table entry for 
   file-backed page upage. 
   Should acquire and release spt_lock before and after! */
bool suppagedir_install_bin_page(struct hash *spt, void *upage,
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
  lock_init (&spe->lock);
  hash_insert (spt, &spe->h_elem);
  return true;
}

/* Create and insert a new supplemental page table entry for
   zeroed page upage. 
   Should acquire and release spt_lock before and after! */
bool suppagedir_install_zero_page (struct hash *spt, void *upage, 
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
  lock_init (&spe->lock);
  hash_insert (spt, &spe->h_elem);
  return true;
}

/* Lookup SPT entry by user page (NULL if not found). */
static struct sup_page_entry *
suppagedir_find (struct hash *spt, void *upage) 
{
  struct sup_page_entry spe = { .upage = upage };
  struct hash_elem *he = hash_find (spt, &spe.h_elem);
  return he ? hash_entry (he, struct sup_page_entry, h_elem) : NULL;
}

/* On page fault: load page from file/swap/zero. */
bool 
load_page_from_spt (void *fault_addr) 
{
  void *upage = pg_round_down (fault_addr);
  struct thread *t = thread_current ();
  struct hash *spt = &t->proc_info->sup_page_table;
  struct lock *spt_lock = &t->proc_info->spt_lock;

  lock_acquire (spt_lock);
  struct sup_page_entry *spe = suppagedir_find (spt, upage);
  lock_release (spt_lock);

  if (!spe)
    {
      /* No entry in SPT: page fault error */
      return false;
    }

  /* Allocate a frame for this page */
  void *kpage = frame_alloc (upage);
  if (!kpage)
    {
      /* No free frame: page fault error */
      printf ("load_page_from_spt: no free frame for %p\n", upage);
      return false;
    }
  /* Set the frame as pinned */
  frame_set_pinned (kpage, true);

  lock_acquire (&spe->lock);

  /* Fill frame from backing store */
  /* Zeroed page */
  if (spe->type == PAGE_ZERO) 
    {
      memset (kpage, 0, PGSIZE);
      /* Install page into page table and set its dirty bit */
      if (!pagedir_set_page (t->pagedir, upage, kpage, spe->writable))
        return false;
      
      pagedir_set_dirty (t->pagedir, upage, true);
      lock_release (&spe->lock);
      frame_set_pinned (kpage, false);
      return true;
    }

  /* Backed by file */
  else if (spe->type == PAGE_BIN) 
    {
      #ifndef VM
      printf ("load_page_from_spt: loading %p from file\n", upage);
      #endif
      lock_release (&spe->lock);
      lock_acquire (&filesys_lock);
      lock_acquire (&spe->lock);
      file_seek (spe->file, spe->ofs);
      size_t r = file_read (spe->file, kpage, spe->read_bytes);
      if (r != spe->read_bytes) 
        {
          /* read error */
          palloc_free_page (kpage);
          lock_release (&spe->lock);
          lock_release (&filesys_lock);
          frame_set_pinned (kpage, false);
          printf ("load_page_from_spt: read error for %p\n", upage);
          return false;
        }
      memset (kpage + spe->read_bytes, 0, spe->zero_bytes);
      lock_release (&spe->lock);
      lock_release (&filesys_lock);
      frame_set_pinned (kpage, false);
      /* Install page into page table */
      /* No need to worry about spe->writable race, because there
         should not be any chance that it is modified */
      return pagedir_set_page (t->pagedir, upage, kpage, spe->writable);
    }

  /* Swapped out */
  else if (spe->type == PAGE_SWAP) 
    {
      #ifndef VM
      printf ("load_page_from_spt: loading %p from swap slot %d\n",
             upage, spe->swap_slot);
      #endif
      swap_read (spe->swap_slot, kpage);
      spe->swap_slot = (block_sector_t) -1;
      lock_release (&spe->lock);
      frame_set_pinned (kpage, false);

      /* Install page into page table and set its dirty bit */
      if (!pagedir_set_page (t->pagedir, upage, kpage, spe->writable))
        return false;
      pagedir_set_dirty (t->pagedir, upage, true);
      return true;
    }
  
  NOT_REACHED ();
}

/* Set the swap slot in the supplemental page table entry. 
   Should acquire and release spt_lock before and after! */
void
suppagedir_set_page_swapped (struct hash *spt, void *upage,
                             block_sector_t swap_slot)
{
  struct sup_page_entry *spe = suppagedir_find (spt, upage);
  if (spe) 
  {
      lock_acquire (&spe->lock);
      spe->type = PAGE_SWAP;
      spe->swap_slot = swap_slot;
      lock_release (&spe->lock);
    }
}

static void destroy_spe (struct hash_elem *e, void *aux UNUSED);

/* Destroy the supplemental page table, freeing entries and
   swap slots. 
   Should acquire and release spt_lock before and after! */
void 
suppagedir_destroy (struct hash *spt) 
{
  hash_destroy (spt, destroy_spe);
}

/* Destroy a supplemental page table entry. 
   Called by hash_destroy () */
static void
destroy_spe (struct hash_elem *e, void *aux UNUSED)
{
  struct sup_page_entry *spe = hash_entry (e, struct sup_page_entry, h_elem);
  lock_acquire (&spe->lock);
  void *kpage = pagedir_get_page (thread_current ()->pagedir, spe->upage);

  /* Free swap slot if used */
  if (spe->type == PAGE_SWAP && spe->swap_slot != (block_sector_t) -1) 
    swap_free (spe->swap_slot);
  lock_release (&spe->lock);
  if (kpage) 
  {
    /* Free the frame */
    frame_free (kpage);
    pagedir_clear_page (thread_current ()->pagedir, spe->upage);
  }
  free (spe);
}