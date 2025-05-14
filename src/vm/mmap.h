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
    size_t length;                 /* Length of the mapping */
    upage_t uaddr;                 /* User virtual address */
    size_t page_cnt;             /* Number of pages mapped */
  };

/* Create and insert a new mmap entry for the given file. */
bool mmap_insert (struct list *mmap_list, mapid_t mapid,
                   struct file *file, size_t length, upage_t uaddr);

/* Destroy the mmap list, freeing mmap entries. */
void mmap_destroy (struct list *mmap_list);

/* Remove a mmap entry from the list. */
void mmap_delete (struct list *mmap_list, struct mmap_entry *entry);

#endif /**< vm/mmap.h */