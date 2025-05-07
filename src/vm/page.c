/* vm/page.c */
#include "vm/page.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include <string.h>

/* Hash functions keyed by upage */
static unsigned 
page_hash (const struct hash_elem *e, void *aux UNUSED)
{
    struct sup_page *p = hash_entry(e, struct sup_page, h_elem);
    return hash_bytes(&p->upage, sizeof p->upage);
}

static bool 
page_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
    struct sup_page *p1 = hash_entry(a, struct sup_page, h_elem);
    struct sup_page *p2 = hash_entry(b, struct sup_page, h_elem);
    return p1->upage < p2->upage;
}

/* Initialize supplemental page table hash. */
void 
suppagedir_init (struct hash *spt) {
    hash_init(spt, page_hash, page_less, NULL);
}

/* Create and insert a new page table entry for virtual page upage. */
bool 
suppagedir_add_page (struct hash *spt, void *upage,
                          struct file *file, off_t ofs,
                          size_t read_bytes, size_t zero_bytes,
                          bool writable) {
    struct sup_page *sp = malloc(sizeof *sp);
    if (!sp) return false;
    sp->upage = upage;
    sp->type = PAGE_BIN;
    sp->file = file;
    sp->ofs = ofs;
    sp->read_bytes = read_bytes;
    sp->zero_bytes = zero_bytes;
    sp->writable = writable;
    sp->swap_slot = (block_sector_t)-1;
    hash_insert(spt, &sp->h_elem);
    return true;
}

/* Lookup SPT entry by user page (NULL if not found). */
struct sup_page *
suppagedir_find (struct hash *spt, void *upage) 
{
    struct sup_page p; p.upage = upage;
    struct hash_elem *e = hash_find(spt, &p.h_elem);
    return e ? hash_entry(e, struct sup_page, h_elem) : NULL;
}

/* On page fault: load page from file/swap/zero. */
bool 
load_page_from_spt (void *fault_addr) 
{
    void *upage = pg_round_down(fault_addr);
    struct sup_page *sp = suppagedir_find(&thread_current()->sup_pt, upage);
    if (!sp) return false;
    /* Allocate a frame for this page */
    void *kpage = frame_alloc(upage);
    if (!kpage) return false; 
    /* Fill frame from backing store */
    if (sp->type == PAGE_ZERO) {
        memset(kpage, 0, PGSIZE);
    } else if (sp->type == PAGE_BIN) {
        file_seek(sp->file, sp->ofs);
        size_t r = file_read(sp->file, kpage, sp->read_bytes);
        if (r != sp->read_bytes) {
            /* read error */
            palloc_free_page(kpage);
            return false;
        }
        memset(kpage + sp->read_bytes, 0, sp->zero_bytes);
    } else if (sp->type == PAGE_SWAP) {
        swap_read(sp->swap_slot, kpage);
        sp->swap_slot = (block_sector_t)-1;
    }
    /* Install page into page table */
    bool ok = pagedir_set_page(thread_current()->pagedir, upage, kpage, sp->writable);
    return ok;
}

/* Destroy the supplemental page table, freeing resources. */
void 
suppagedir_destroy (struct hash *spt) 
{
    struct hash_iterator i;
    hash_first(&i, spt);
    while (hash_next(&i)) {
        struct sup_page *sp = hash_entry(hash_cur(&i), struct sup_page, h_elem);
        /* Free swap slot if used */
        if (sp->type == PAGE_SWAP && sp->swap_slot != (block_sector_t)-1) {
            swap_free(sp->swap_slot);
        }
        free(sp);
    }
}
