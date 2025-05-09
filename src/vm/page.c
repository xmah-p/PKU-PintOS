/* vm/page.c */
#include "vm/page.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/frame.h"
#include "vm/swap.h"
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
   file-backed page upage. */
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
  hash_insert (spt, &spe->h_elem);
  return true;
}

/* Create and insert a new supplemental page table entry for
   zeroed page upage. */
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
  hash_insert(spt, &spe->h_elem);
  return true;
}

/* Lookup SPT entry by user page (NULL if not found). */
static struct sup_page_entry *
suppagedir_find (struct hash *spt, void *upage) 
{
  struct sup_page_entry spe = { .upage = upage };
  struct hash_elem *he = hash_find(spt, &spe.h_elem);
  return he ? hash_entry (he, struct sup_page_entry, h_elem) : NULL;
}

/* On page fault: load page from file/swap/zero. */
bool 
load_page_from_spt (void *fault_addr) 
{
  void *upage = pg_round_down (fault_addr);
  struct thread *t = thread_current ();
  struct hash *spt = &t->proc_info->sup_page_table;
  struct sup_page_entry *spe = suppagedir_find (spt, upage);
  if (!spe) return false;

  /* Allocate a frame for this page */
  void *kpage = frame_alloc (upage);
  if (!kpage) return false; 

  /* Fill frame from backing store */
  /* Zeroed page */
  if (spe->type == PAGE_ZERO) 
    memset(kpage, 0, PGSIZE);

  /* Backed by file */
  else if (spe->type == PAGE_BIN) 
    {
      file_seek(spe->file, spe->ofs);
      size_t r = file_read(spe->file, kpage, spe->read_bytes);
      if (r != spe->read_bytes) 
        {
          /* read error */
          palloc_free_page (kpage);
          return false;
        }
      memset(kpage + spe->read_bytes, 0, spe->zero_bytes);
    }

  /* Swapped out */
  else if (spe->type == PAGE_SWAP) 
    {
      swap_read (spe->swap_slot, kpage);
      spe->swap_slot = (block_sector_t) -1;
    }

  /* Install page into page table */
  return pagedir_set_page (t->pagedir, upage, kpage, spe->writable);
}

/* Set the swap slot in the supplemental page table entry. */
void
suppagedir_set_page_swapped (struct hash *spt, void *upage,
                              block_sector_t swap_slot) 
{
  struct sup_page_entry *spe = suppagedir_find (spt, upage);
  if (spe) 
    {
      spe->type = PAGE_SWAP;
      spe->swap_slot = swap_slot;
    }
}

/* Destroy the supplemental page table, freeing entries and
   swap slots. */
void 
suppagedir_destroy (struct hash *spt) 
{
  struct hash_iterator i;
  hash_first (&i, spt);
  while (hash_next (&i)) 
    {
      struct sup_page_entry *spe = hash_entry (hash_cur (&i), 
                                      struct sup_page_entry, h_elem);
      /* Free swap slot if used */
      if (spe->type == PAGE_SWAP && spe->swap_slot != (block_sector_t) -1) 
        swap_free(spe->swap_slot);
      free(spe);
    }
}
