/* vm/frame.c */
#include "vm/frame.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "threads/thread.h"
#include <debug.h>

static struct hash frame_map;    /* Hash table: key = kpage */
static struct list frame_list;   /* List of all frames for eviction */
static struct lock frame_lock;   
static struct list_elem *clock_hand;  /* Clock pointer */

static unsigned 
frame_hash (const struct hash_elem *e, void *aux UNUSED)
{
    struct frame *f = hash_entry (e, struct frame, h_elem);
    return (unsigned) (f->kpage) >> PGBITS;
}

static bool 
frame_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
    return hash_entry (a, struct frame, h_elem)->kpage < 
           hash_entry (b, struct frame, h_elem)->kpage;
}

/* Initialize frame table and lock. */
void 
frame_init (void) 
{
    hash_init(&frame_map, frame_hash, frame_less, NULL);
    list_init(&frame_list);
    lock_init(&frame_lock);
    clock_hand = NULL;
}

/* Choose a victim frame using a clock algorithm (skip pinned). */
static struct frame *
pick_victim_frame (void) 
{
    if (clock_hand == NULL)
        clock_hand = list_begin(&frame_list);
    /* Loop until we find a non-pinned, not-recently-used frame */
    while (true) 
    {
        if (clock_hand == list_end(&frame_list))
            clock_hand = list_begin(&frame_list);
        struct frame *f = list_entry(clock_hand, struct frame, l_elem);
        /* Skip pinned frames */
        if (!f->pinned) 
        {
            /* Check accessed bit */
            if (!pagedir_is_accessed(f->owner->pagedir, f->upage))
                return f;  /* choose this victim */
            /* Otherwise, give a second chance */
            pagedir_set_accessed(f->owner->pagedir, f->upage, false);
        }
        clock_hand = list_next(clock_hand);
    }
}

/* Allocate a frame for user page upage, evicting if necessary. */
void *
frame_alloc (void *upage) 
{
    lock_acquire(&frame_lock);

    /* Try to get a free page from user pool */
    void *kpage = palloc_get_page(PAL_USER);
    if (kpage != NULL) 
    {
        /* Create a new frame entry */
        struct frame *f = malloc(sizeof *f);
        f->kpage = kpage;
        f->owner = thread_current();
        f->upage = upage;
        f->pinned = false;
        hash_insert(&frame_map, &f->h_elem);
        list_push_back(&frame_list, &f->l_elem);
        if (clock_hand == NULL)
            clock_hand = list_begin(&frame_list);
        lock_release(&frame_lock);
        return kpage;
    }

    /* No free page: evict one via clock algorithm */
    struct frame *victim = pick_victim_frame();
    /* If dirty, write to swap (swap_write returns slot index) */
    bool dirty = pagedir_is_dirty(victim->owner->pagedir, victim->upage);
    if (dirty) 
    {
        extern block_sector_t swap_write (void *frame); 
        block_sector_t slot = swap_write(victim->kpage);
        /* Update victim's supplemental page entry */
        struct thread *vict = victim->owner;
        // Lookup supplemental entry for (vict->pagedir, vict->upage) and mark SWAP
        // (handled in vm/page.c via page_set_swap_slot)
        vm_set_page_swapped(vict, victim->upage, slot);
    }
    /* Remove old mapping */
    pagedir_clear_page(victim->owner->pagedir, victim->upage);

    /* Reuse this frame for new page */
    victim->owner = thread_current();
    victim->upage = upage;
    victim->pinned = false;

    kpage = victim->kpage;
    lock_release(&frame_lock);
    return kpage;
}

/* Free a frame and its entry (called when process exits). */
void 
frame_free (void *kpage) 
{
    lock_acquire(&frame_lock);
    struct hash_elem *he = hash_find(&frame_map, &(struct frame){.kpage=kpage}.h_elem);
    if (he != NULL) 
    {
        struct frame *f = hash_entry(he, struct frame, h_elem);
        list_remove(&f->l_elem);
        hash_delete(&frame_map, &f->h_elem);
        palloc_free_page(kpage);
        free(f);
    }
    lock_release(&frame_lock);
}

/* Helpers to pin/unpin a frame by its kernel address. */
void frame_pin (void *kpage) 
{
    lock_acquire(&frame_lock);
    struct frame key = { .kpage = kpage };
    struct hash_elem *he = hash_find(&frame_map, &key.h_elem);
    if (he != NULL) 
        hash_entry(he, struct frame, h_elem)->pinned = true;
    lock_release(&frame_lock);
}

void 
frame_unpin (void *kpage) 
{
    lock_acquire(&frame_lock);
    struct frame key = { .kpage = kpage };
    struct hash_elem *he = hash_find(&frame_map, &key.h_elem);
    if (he != NULL) 
        hash_entry(he, struct frame, h_elem)->pinned = false;
    lock_release(&frame_lock);
}
