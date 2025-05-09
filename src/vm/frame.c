/* vm/frame.c */
#include "vm/frame.h"

#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/page.h"
#include "devices/block.h"

static struct hash frame_map;    /* Hash table: key = kpage */
static struct list frame_list;   /* List of all frames for eviction */
static struct lock frame_lock;   
static struct list_elem *clock_hand;  /* Clock pointer */

/** The PintOS frame table is a global hash table mapping kpage (PPN)
   to a frame_entry struct.

   The frame table is initialized in threads/init.c via frame_init ()
   after the file system initialization. */

/* PPN (kpage) as hash key */
static unsigned 
frame_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct frame_entry *fe = hash_entry 
                                  (e, struct frame_entry, h_elem);
  return hash_bytes (fe->kpage, sizeof fe->kpage);
}

/* Compare by PPN (kpage) */
static bool 
frame_less (const struct hash_elem *a, 
            const struct hash_elem *b, void *aux UNUSED)
{
  struct frame_entry *fe1 = hash_entry (a, struct frame_entry, h_elem);
  struct frame_entry *fe2 = hash_entry (b, struct frame_entry, h_elem);
  return fe1->kpage < fe2->kpage;
}

/* Initialize frame table and lock. */
void 
frame_init (void) 
{
  hash_init (&frame_map, frame_hash, frame_less, NULL);
  list_init (&frame_list);
  lock_init (&frame_lock);
  clock_hand = NULL;
}

/* Choose a victim frame using clock algorithm (skip pinned). */
static struct frame_entry *
pick_victim_frame (void) 
{
  if (clock_hand == NULL)
      clock_hand = list_begin (&frame_list);

  /* Loop until we find a non-pinned, not-recently-used frame */
  while (true) 
    {
      if (clock_hand == list_end (&frame_list))
          clock_hand = list_begin (&frame_list);
      struct frame_entry *fe = list_entry (clock_hand, 
                                          struct frame_entry, l_elem);
      /* Skip pinned frames */
      if (!fe->pinned) 
        {
          /* Check accessed bit: pick if not set */
          if (!pagedir_is_accessed(fe->owner->pagedir, fe->upage))
              return fe;  /* choose this victim */
          /* Otherwise, give a second chance */
          pagedir_set_accessed(fe->owner->pagedir, fe->upage, false);
        }
      clock_hand = list_next(clock_hand);
    }
}

/* Allocate a frame for user page upage, evicting if necessary. */
void *
frame_alloc (void *upage) 
{
  lock_acquire (&frame_lock);

  /* Try to get a free page from user pool */
  void *kpage = palloc_get_page (PAL_USER);
  if (kpage != NULL) 
    {
      /* Create a new frame entry */
      struct frame_entry *fe = malloc (sizeof *fe);
      fe->kpage = kpage;
      fe->owner = thread_current ();
      fe->upage = upage;
      fe->pinned = false;

      hash_insert (&frame_map, &fe->h_elem);
      list_push_back (&frame_list, &fe->l_elem);

      if (clock_hand == NULL)
          clock_hand = list_begin (&frame_list);
      lock_release (&frame_lock);
      return kpage;
    }

  /* No free page: evict one via clock algorithm */
  struct frame_entry *victim = pick_victim_frame ();

  /* If dirty, write to swap (swap_write returns slot index) */
  bool dirty = pagedir_is_dirty (victim->owner->pagedir, victim->upage);
  if (dirty) 
    {
      extern block_sector_t swap_write (void *frame_entry); 
      block_sector_t slot = swap_write (victim->kpage);
      /* Update victim's supplemental page entry */
      struct hash *spt = &victim->owner->proc_info->sup_page_table;
      suppagedir_set_page_swapped (spt, victim->upage, slot);
    }

  /* Remove old mapping */
  pagedir_clear_page (victim->owner->pagedir, victim->upage);

  /* Reuse this frame for new page */
  victim->owner = thread_current ();
  victim->upage = upage;
  victim->pinned = false;

  kpage = victim->kpage;
  lock_release (&frame_lock);
  return kpage;
}

/* Lookup frame_entry by its kernel address (kpage). Synchronized. */
static struct frame_entry *
frame_find (void *kpage) 
{
  lock_acquire (&frame_lock);
  struct frame_entry key_fe = { .kpage = kpage };
  struct hash_elem *he = hash_find (&frame_map, &key_fe.h_elem);
  if (he == NULL) 
    {
      lock_release (&frame_lock);
      return NULL;
    }
  struct frame_entry *fe = hash_entry (he, struct frame_entry, h_elem);
  lock_release (&frame_lock);
  return fe;
}

/* Free a frame and its entry (called when process exits). */
void 
frame_free (void *kpage) 
{
  struct frame_entry *fe = frame_find (kpage);
  if (fe == NULL) return;
  
  lock_acquire (&frame_lock);
  list_remove (&fe->l_elem);
  hash_delete (&frame_map, &fe->h_elem);
  palloc_free_page (kpage);
  free (fe);
  lock_release (&frame_lock);
}

/* Pin/unpin a frame by its kernel address. */
void frame_set_pinned (void *kpage, bool pinned) 
{
  struct frame_entry *fe = frame_find (kpage);
  if (fe == NULL) return;
  lock_acquire (&frame_lock);
  fe->pinned = pinned;
  lock_release (&frame_lock);
}
