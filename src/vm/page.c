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
#include "vm/vm_region.h"
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
   file-backed page upage. */
bool spt_install_bin_page (struct hash *spt, struct lock *spt_lock,
                           upage_t upage, struct file *file, off_t ofs,
                           size_t read_bytes, size_t zero_bytes,
                           bool writable)
{
  struct spt_entry *spte = malloc (sizeof *spte);
  if (!spte)
    return false;
  spte->upage = upage;
  spte->type = PAGE_FILE;
  spte->file = file;
  spte->ofs = ofs;
  spte->read_bytes = read_bytes;
  spte->zero_bytes = zero_bytes;
  spte->writable = writable;
  spte->swap_slot = (block_sector_t) -1;
  lock_init (&spte->spte_lock);
  lock_acquire (spt_lock);
  hash_insert (spt, &spte->h_elem);
  lock_release (spt_lock);
  return true;
}

/* Create and insert a new supplemental page table entry for
   zeroed page upage. */
bool spt_install_zero_page (struct hash *spt, struct lock *spt_lock, 
                            upage_t upage, bool writable)
{
  struct spt_entry *spte = malloc (sizeof *spte);
  if (!spte)
      return false;
  spte->upage = upage;
  spte->type = PAGE_ZERO;
  spte->file = NULL;
  spte->ofs = 0;
  spte->read_bytes = 0;
  spte->zero_bytes = 0;
  spte->writable = writable;
  spte->swap_slot = (block_sector_t) -1;
  lock_init (&spte->spte_lock);
  lock_acquire (spt_lock);
  hash_insert (spt, &spte->h_elem);
  lock_release (spt_lock);
  return true;
}

/* Lookup SPT entry by user page (NULL if not found). */
struct spt_entry *
suppagedir_lookup (struct hash *spt, upage_t upage) 
{
  struct spt_entry spte = { .upage = upage };
  struct hash_elem *he = hash_find (spt, &spte.h_elem);
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
  struct spt_entry *spte_ptr = suppagedir_lookup (spt, upage);
  lock_release (spt_lock);
  if (!spte_ptr) return false;

  struct lock *spte_lock = &spte_ptr->spte_lock;
  /* Copy spte to avoid race condition! */
  lock_acquire (spte_lock);
  struct spt_entry spte = *spte_ptr;
  lock_release (spte_lock);

  switch (spte.type) 
    {
      case PAGE_ZERO:
        memset (kpage, 0, PGSIZE);
    
        if (!pagedir_set_page (t->pagedir, upage, kpage, spte.writable)) 
        {
          frame_free (kpage);
          return false;
        }
        pagedir_set_dirty (t->pagedir, upage, true);
        frame_set_pinned (kpage, false);
        return true;
    
      case PAGE_FILE:
        lock_acquire (&filesys_lock);
        file_seek (spte.file, spte.ofs);
        size_t r = file_read (spte.file, kpage, spte.read_bytes);
        lock_release (&filesys_lock);
    
        if (r != spte.read_bytes) 
        {
          /* read error */
          frame_free (kpage);
          return false;
        }
    
        memset(kpage + spte.read_bytes, 0, spte.zero_bytes);
    
        if (!pagedir_set_page (t->pagedir, upage, kpage, spte.writable)) 
        {
          frame_free (kpage);
          return false;
        }
    
        frame_set_pinned (kpage, false);
        return true;
    
      case PAGE_SWAP:
        swap_read (spte.swap_slot, kpage);
    
        lock_acquire (spte_lock);
        spte_ptr->swap_slot = (block_sector_t) -1;
        lock_release (spte_lock);
    
        if (!pagedir_set_page (t->pagedir, upage, kpage, spte.writable)) 
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
  struct spt_entry *spte = suppagedir_lookup (spt, upage);
  lock_release (spt_lock);
  if (spte) 
  {
    struct lock *spte_lock = &spte->spte_lock;
    lock_acquire (spte_lock);
    spte->type = PAGE_SWAP;
    spte->swap_slot = swap_slot;
    lock_release (spte_lock);
  }
}

static void destroy_spte (struct hash_elem *e, void *aux UNUSED);

/* Destroy the supplemental page table, freeing supplemental page
   table entries, swap slots, frame table entries and kernel pages.
   Should acquire and release spt_lock before and after! */
void 
spt_destroy (struct hash *spt, struct lock *spt_lock)
{
  lock_acquire (&frame_lock);
  lock_acquire (spt_lock);
  hash_destroy (spt, destroy_spte);
  lock_release (spt_lock);
  lock_release (&frame_lock);
}

/* Destroy a supplemental page table entry. 
   Called by hash_destroy (). */
static void
destroy_spte (struct hash_elem *e, void *aux UNUSED)
{
  struct spt_entry *spte = hash_entry (e, struct spt_entry, h_elem);

  struct lock *spte_lock = &spte->spte_lock;
  lock_acquire (spte_lock);
  struct spt_entry spte_copy = *spte;
  lock_release (spte_lock);

  /* Free swap slot if used */
  if (spte_copy.type == PAGE_SWAP && 
      spte_copy.swap_slot != (block_sector_t) -1) 
    swap_free (spte_copy.swap_slot);

  kpage_t kpage = pagedir_get_page (thread_current ()->pagedir, 
                                    spte_copy.upage);
  pagedir_clear_page (thread_current ()->pagedir, spte->upage);
  if (kpage) 
    {
      frame_free (kpage);
    }
  free (spte);
}


/* Only create and insert entries in the supplemental page table, 
   does not actually read file content into pages. */
static bool
lazy_load_page (size_t read_bytes, size_t zero_bytes, struct file *file, 
                off_t ofs, upage_t upage, bool writable)
{
  struct hash *spt = &thread_current ()->proc_info->sup_page_table;
  struct lock *spt_lock = &thread_current ()->proc_info->spt_lock;
  bool success = spt_install_bin_page (spt, spt_lock, upage, file, ofs,
                                       read_bytes, zero_bytes, writable);
  return success;
}

bool
load_segment (struct file *file, off_t ofs, upage_t upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable,
              enum region_type type)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  vm_region_install (&thread_current ()->proc_info->vm_region_list,
                     type, upage, read_bytes + zero_bytes);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      if (!lazy_load_page (page_read_bytes, page_zero_bytes,
                              file, ofs, upage, writable))
        return false;

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
      ofs += page_read_bytes;    /* Cannot use file_tell ()! */
    }
  return true;
}
