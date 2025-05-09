/* vm/frame.h */
#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdbool.h>
#include "threads/thread.h"
#include "lib/kernel/hash.h"


struct frame_entry
  {
    struct hash_elem h_elem;   /* Hash element (keyed by kpage) */
    struct list_elem l_elem;   /* List element for global frame list */
    void *kpage;               /* Kernel (physical) address of frame */
    struct thread *owner;      /* Owning process/thread */
    void *upage;               /* User virtual address mapped here */
    bool pinned;               /* If true, do not evict */
  };

/* Initializes the global frame table (call in thread_init). */
void frame_init (void);

/* Allocates a user frame for user page 'upage', evicting if needed. */
void *frame_alloc (void *upage);

/* Frees a frame when a process exits (unmaps and releases). */
void frame_free (void *kpage);

/* Pins/unpins a frame to prevent/allow eviction. */
void frame_set_pinned (void *kpage, bool pinned) ;

#endif /* vm/frame.h */
