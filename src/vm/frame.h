#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdbool.h>
#include "threads/thread.h"
#include "lib/kernel/hash.h"

typedef void * kpage_t; /* Kernel page (physical address) */
typedef void * upage_t; /* User page (virtual address) */

/* Frame table entry */
struct frame_entry
  {
    struct hash_elem h_elem;   /* Hash element (keyed by kpage) */
    struct list_elem l_elem;   /* List element for global frame list */
    kpage_t kpage;               /* Kernel (physical) address of frame */
    struct thread *owner;      /* Owning process/thread */
    upage_t upage;               /* User virtual address mapped here */
    bool pinned;               /* If true, do not evict */
  };

/* Initializes the global frame table (call in thread_init). */
void frame_init (void);

/* Allocates a user frame for user page 'upage', evicting if needed.
   Called in load_page_from_spt () */
kpage_t frame_alloc (upage_t upage);

/* Frees a frame when a process exits (unmaps and releases). 
   Called in destroy_spe () */
void frame_free (kpage_t kpage);

/* Pins/unpins a frame to prevent/allow eviction. */
void frame_set_pinned (kpage_t kpage, bool pinned) ;

#endif /**< vm/frame.h */
