#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <hash.h>
#include "filesys/file.h"
#include "threads/synch.h"
#include "vm/frame.h"

typedef int mapid_t; /**< Mapping ID type. */

/* mmap table entry */
struct mmap_entry
  {
    struct list_elem l_elem;       /* List element */
    mapid_t mapid;                     /* Mapping ID */
    struct file *file;             /* File being mapped */
    upage_t uaddr;                 /* User virtual address */
    size_t page_cnt;             /* Number of pages mapped */
  };

/* Create and insert a new mmap entry for the given file. */
bool mmap_create (struct list *mmap_list, mapid_t mapid,
                   struct file *file, size_t length, upage_t uaddr);

/* Write back all mmap entries and destroy the mmap list. */
void mmap_write_back_and_destroy (struct list *mmap_list);

/* Write back mmap entry to file, and remove it from the list. */
void mmap_write_back_and_delete (struct list *mmap_list, mapid_t mapid);

#endif /**< vm/mmap.h */