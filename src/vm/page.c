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
   is a hash table mapping upage (VPN) to a spt_entry struct,
   stored in the thread's proc_info.

   SPT is initialized via spt_init () in start_process ()
   before loading the executable, and is destroyed via
   spt_destroy () in process_exit (). */

/* VPN (upage) as hash key */
static unsigned 
page_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct spt_entry *p = hash_entry 
                                      (e, struct spt_entry, h_elem);
  return (unsigned) p->upage;
}

/* Compare by VPN (upage) */
static bool 
page_less (const struct hash_elem *a, 
           const struct hash_elem *b, void *aux UNUSED)
{
  struct spt_entry *p1 = hash_entry 
                                      (a, struct spt_entry, h_elem);
  struct spt_entry *p2 = hash_entry 
                                      (b, struct spt_entry, h_elem);
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
  struct spt_entry *spe = malloc (sizeof *spe);
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
  lock_init (&spe->spte_lock);
  return true;
}

/* Create and insert a new supplemental page table entry for
   zeroed page upage. 
   Should acquire and release spt_lock before and after! */
bool spt_install_zero_page (struct hash *spt, upage_t upage, 
                                   bool writable)
{
  struct spt_entry *spe = malloc (sizeof *spe);
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
  lock_init (&spe->spte_lock);
  return true;
}

/* Lookup SPT entry by user page (NULL if not found). */
static struct spt_entry *
suppagedir_find (struct hash *spt, upage_t upage) 
{
  struct spt_entry spe = { .upage = upage };
  struct hash_elem *he = hash_find (spt, &spe.h_elem);
  return he ? hash_entry (he, struct spt_entry, h_elem) : NULL;
}

/* On page fault: load page by spt. */
bool 
load_page_by_spt (void *fault_addr) 
{
  upage_t upage = pg_round_down (fault_addr);
  struct thread *t = thread_current ();
  struct hash *spt = &t->proc_info->sup_page_table;
  struct lock *spt_lock = &t->proc_info->spt_lock;

  /* Allocate frame before lookup SPTE, this makes sure that if the
     current page fault is due to an in progress eviction, there will
     not be a race condition for the SPTE. */
  kpage_t kpage = frame_alloc (upage);

  lock_acquire (spt_lock);
  struct spt_entry *spe_ptr = suppagedir_find (spt, upage);
  lock_release (spt_lock);
  if (!spe_ptr) return false;

  struct lock *spte_lock = &spe_ptr->spte_lock;
  /* Copy spte to avoid race condition! */
  lock_acquire (spte_lock);
  struct spt_entry spe = *spe_ptr;
  lock_release (spte_lock);

  switch (spe.type) 
    {
      case PAGE_ZERO:
        memset (kpage, 0, PGSIZE);
    
        if (!pagedir_set_page (t->pagedir, upage, kpage, spe.writable)) 
        {
          frame_free (kpage);
          return false;
        }
        pagedir_set_dirty (t->pagedir, upage, true);
        frame_set_pinned (kpage, false);
        return true;
    
      case PAGE_BIN:
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
    
        memset(kpage + spe.read_bytes, 0, spe.zero_bytes);
    
        if (!pagedir_set_page (t->pagedir, upage, kpage, spe.writable)) 
        {
          frame_free (kpage);
          return false;
        }
    
        frame_set_pinned (kpage, false);
        return true;
    
      case PAGE_SWAP:
        swap_read (spe.swap_slot, kpage);
    
        lock_acquire (spte_lock);
        spe_ptr->swap_slot = (block_sector_t) -1;
        lock_release (spte_lock);
    
        if (!pagedir_set_page (t->pagedir, upage, kpage, spe.writable)) 
        {
          frame_free (kpage);
          return false;
        }
        pagedir_set_dirty (t->pagedir, upage, true);
        frame_set_pinned (kpage, false);
        return true;
    
      default:
        PANIC ("load_page_from_spt: unknown page type");
    }
  NOT_REACHED ();
}

/* Set the swap slot in the supplemental page table entry. 
   Synchronized.  */
void
spt_set_page_swapped (struct hash *spt, struct lock *spt_lock,
                      upage_t upage, block_sector_t swap_slot)
{
  lock_acquire (spt_lock);
  struct spt_entry *spe = suppagedir_find (spt, upage);
  lock_release (spt_lock);
  if (spe) 
  {
    struct lock *spte_lock = &spe->spte_lock;
    lock_acquire (spte_lock);
    spe->type = PAGE_SWAP;
    spe->swap_slot = swap_slot;
    lock_release (spte_lock);
  }
}

static void destroy_spe (struct hash_elem *e, void *aux UNUSED);

/* Destroy the supplemental page table, freeing supplemental page
   table entries, swap slots, frame table entries and kernel pages.
   Should acquire and release spt_lock before and after! */
void 
spt_destroy (struct hash *spt, struct lock *spt_lock)
{
  lock_acquire (&frame_lock);
  lock_acquire (spt_lock);
  hash_destroy (spt, destroy_spe);
  lock_release (spt_lock);
  lock_release (&frame_lock);
}

/* Destroy a supplemental page table entry. 
   Called by hash_destroy (). */
static void
destroy_spe (struct hash_elem *e, void *aux UNUSED)
{
  struct spt_entry *spe = hash_entry (e, struct spt_entry, h_elem);

  struct lock *spte_lock = &spe->spte_lock;
  lock_acquire (spte_lock);
  struct spt_entry spe_copy = *spe;
  lock_release (spte_lock);

  /* Free swap slot if used */
  if (spe_copy.type == PAGE_SWAP && spe_copy.swap_slot != (block_sector_t) -1) 
    swap_free (spe_copy.swap_slot);

  kpage_t kpage = pagedir_get_page (thread_current ()->pagedir, spe_copy.upage);
  pagedir_clear_page (thread_current ()->pagedir, spe->upage);
  if (kpage) 
    {
      frame_free (kpage);
    }
  free (spe);
}
