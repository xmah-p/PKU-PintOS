#include "vm/mmap.h"

#include <list.h>
#include <stdio.h>
#include <string.h>
#include "threads/vaddr.h"
#include "threads/malloc.h"

/* Create and insert a new mmap entry for the given file. */
bool
mmap_insert (struct list *mmap_list, mapid_t mapid,
               struct file *file, size_t length, upage_t uaddr)
{
  struct mmap_entry *entry = malloc (sizeof *entry);
  if (!entry)
    return false;
  entry->mapid = mapid;
  entry->file = file;
  entry->length = length;
  entry->uaddr = uaddr;
  entry->page_cnt = length / PGSIZE + (length % PGSIZE != 0);
  list_push_back (mmap_list, &entry->l_elem);
  return true;
}

/* Destroy the mmap list, freeing mmap entries. */
void
mmap_destroy (struct list *mmap_list)
{
  struct list_elem *e;
  struct mmap_entry *entry;

  for (e = list_begin (mmap_list); e != list_end (mmap_list);)
    {
      entry = list_entry (e, struct mmap_entry, l_elem);
      e = list_remove (e);
      file_close (entry->file);
      free (entry);
    }
}

/* Remove a mmap entry from the list. */
void
mmap_delete (struct list *mmap_list, struct mmap_entry *entry)
{
  list_remove (&entry->l_elem);
  file_close (entry->file);
  free (entry);
}
