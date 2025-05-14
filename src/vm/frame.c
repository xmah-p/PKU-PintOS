/* vm/frame.c */
#include "vm/frame.h"

#include "devices/block.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/page.h"
#include "vm/swap.h"

static struct hash frame_map;    /* Hash table: key = kpage */
static struct list frame_list;   /* List of all frames for eviction */
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
  return (unsigned) fe->kpage;
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

/* Choose a victim frame using clock algorithm (skip pinned). 
   The victim frame will be pinned.
   Called by frame_alloc (). */
static struct frame_entry *
pick_victim_frame (void) 
{
  if (clock_hand == NULL)
    clock_hand = list_begin (&frame_list);

  while (true)
    {
      if (clock_hand == list_end (&frame_list))
          clock_hand = list_begin (&frame_list);
      struct frame_entry *fe = list_entry (clock_hand, 
                                           struct frame_entry, l_elem);
      if (!fe->pinned) 
        {
          if (!pagedir_is_accessed (fe->owner->pagedir, fe->upage))
            {
              /* Pin and return */
              fe->pinned = true;
              return fe;
            }
          pagedir_set_accessed (fe->owner->pagedir, fe->upage, false);
        }
      clock_hand = list_next (clock_hand);
    }
  NOT_REACHED ();
}

/* Allocate a frame for user page upage, evicting if necessary. 
   Will only be called in load_page_from_spt */
kpage_t
frame_alloc (upage_t upage) 
{
  ASSERT (is_user_vaddr (upage));
  lock_acquire (&frame_lock);

  kpage_t kpage = palloc_get_page (PAL_USER);
  if (kpage != NULL) 
    {
      /* Create a new frame entry */
      struct frame_entry *fe = malloc (sizeof *fe);
      if (fe == NULL)
        {
          lock_release (&frame_lock);
          PANIC ("frame_alloc: malloc failed");
        }
      fe->kpage = kpage;
      fe->owner = thread_current ();
      fe->upage = upage;
      fe->pinned = true;

      hash_insert (&frame_map, &fe->h_elem);
      list_push_back (&frame_list, &fe->l_elem);

      if (clock_hand == NULL)
          clock_hand = list_begin (&frame_list);
          
      lock_release (&frame_lock);
      return kpage;
    }

  /* palloc failed. Evict. */

  /* Pick a victim and pin it using pick_victim_frame () */
  struct frame_entry *victim = pick_victim_frame ();

  /* If dirty, write to swap and update SPT */
  bool dirty = pagedir_is_dirty (victim->owner->pagedir, victim->upage);
  /* Remove old mapping */
  pagedir_clear_page (victim->owner->pagedir, victim->upage);
  /* Now any access to victim->kpage will fault */

  if (dirty) 
    {
      struct hash *spt = &victim->owner->proc_info->sup_page_table;
      struct lock *spt_lock = &victim->owner->proc_info->spt_lock;
      spt_swap_out_page (spt, spt_lock, victim->upage, victim->kpage);
    }

  /* Update victim's owner and upage. */
  victim->owner = thread_current ();
  victim->upage = upage;
  victim->pinned = true;
  kpage = victim->kpage;
  
  lock_release (&frame_lock);
  return kpage;
}

/* Lookup frame_entry by its kernel address.
   Should acquire and release frame_lock before and after! */
static struct frame_entry *
frame_find (kpage_t kpage) 
{
  struct frame_entry key_fe = { .kpage = kpage };
  struct hash_elem *he = hash_find (&frame_map, &(key_fe.h_elem));
  if (he == NULL) 
    {
      return NULL;
    }
  struct frame_entry *fe = hash_entry (he, struct frame_entry, h_elem);
  return fe;
}

/* Free a frame and its entry (called when process exits). */
void 
frame_free (kpage_t kpage) 
{
  struct frame_entry *fe = frame_find (kpage);

  if (fe == NULL)
    {
      PANIC ("frame_free: frame not found");
    }

  if (clock_hand == &fe->l_elem)
    clock_hand = list_next (clock_hand);
  list_remove (&fe->l_elem);
  hash_delete (&frame_map, &fe->h_elem);
  palloc_free_page (kpage);
  free (fe);
}

/* Pin/unpin a frame by its kernel address. */
void frame_set_pinned (kpage_t kpage, bool pinned) 
{
  lock_acquire (&frame_lock);
  struct frame_entry *fe = frame_find (kpage);
  if (fe == NULL)
    {
      PANIC ("frame_set_pinned: frame not found");
    }
  fe->pinned = pinned;
  lock_release (&frame_lock);
}
