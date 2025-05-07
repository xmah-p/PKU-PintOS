/* vm/page.h */
#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <hash.h>
#include "filesys/file.h"

enum page_type { PAGE_BIN, PAGE_ZERO, PAGE_SWAP };

/* Supplemental page table entry */
struct sup_page {
    void *upage;              /* User virtual page (key) */
    enum page_type type;
    struct file *file;        /* Backing file (for PAGE_BIN) */
    off_t ofs;                /* Offset in file */
    size_t read_bytes, zero_bytes; /* Bytes to read/zero */
    block_sector_t swap_slot; /* Swap slot index if PAGE_SWAP, else -1 */
    bool writable;
    struct hash_elem h_elem;
};

void suppagedir_init (struct hash *spt);
struct sup_page *suppagedir_find (struct hash *spt, void *upage);
void suppagedir_destroy (struct hash *spt);

#endif /* vm/page.h */
