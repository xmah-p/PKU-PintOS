#include "vm/mmap.h"

#include <list.h>
#include <stdio.h>
#include <string.h>
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "vm/page.h"
#include "filesys/filesys.h"

/* Create and insert a new mmap entry for the given file. */
bool
mmap_create (struct list *mmap_list, mapid_t mapid,
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

/* Lookup mmap entry by mapid (NULL if not found). */
struct mmap_entry *
mmap_lookup (struct list *mmap_list, mapid_t mapid)
{
  struct list_elem *e;
  struct mmap_entry *entry;

  for (e = list_begin (mmap_list); e != list_end (mmap_list); 
       e = list_next (e))
    {
      entry = list_entry (e, struct mmap_entry, l_elem);
      if (entry->mapid == mapid)
        return entry;
    }
  return NULL;
}

/* Write back each mmap entry and destroy the mmap list. */
void
mmap_write_back_and_destroy (struct list *mmap_list)
{
  struct list_elem *e;
  struct mmap_entry *entry;

  for (e = list_begin (mmap_list); e != list_end (mmap_list);)
    {
      entry = list_entry (e, struct mmap_entry, l_elem);
      write_back_mmap (entry);
      e = list_remove (e);
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


/* Write back mmap entry to file. */
void
write_back_mmap (struct mmap_entry *entry) 
{
  ASSERT (entry != NULL);
  struct thread *cur = thread_current ();
  struct proc_info *proc_info = cur->proc_info;
  struct file *file = entry->file;
  struct lock *spt_lock = &proc_info->spt_lock;

  size_t length = entry->length;
  size_t page_cnt = entry->page_cnt;
  upage_t uaddr = entry->uaddr;

  for (size_t i = 0; i < page_cnt; i++)
    {
      upage_t file_page = uaddr + i * PGSIZE;
      lock_acquire (spt_lock);
      struct spt_entry *spte = suppagedir_lookup (&proc_info->sup_page_table, 
                                               file_page);
      lock_release (spt_lock);
      if (spte == NULL)
        PANIC ("write_back_mmap: spte is NULL");
      
      lock_acquire (&spte->spte_lock);
      struct spt_entry spte_copy = *spte;
      lock_release (&spte->spte_lock);

      if (pagedir_is_dirty (cur->pagedir, file_page))
        {
          lock_acquire (&filesys_lock);
          file_seek (file, i * PGSIZE);
          file_write (file, file_page, spte_copy.read_bytes);
          lock_release (&filesys_lock);
        }
    }
  lock_acquire (&filesys_lock);
  file_close (file);
  lock_release (&filesys_lock);
  vm_region_uninstall (&proc_info->vm_region_list, entry->uaddr);
}