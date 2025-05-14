#include "vm/mmap.h"

#include <list.h>
#include <stdio.h>
#include <string.h>
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
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
  entry->uaddr = uaddr;
  entry->page_cnt = length / PGSIZE + (length % PGSIZE != 0);
  list_push_back (mmap_list, &entry->l_elem);
  return true;
}

/* Lookup mmap entry by mapid (panic if not found). */
static struct mmap_entry *
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
  PANIC ("mmap_lookup: entry not found");
  NOT_REACHED ();
}

static void write_back_mmap (struct mmap_entry *entry);

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

/* Write back a mmap entry to file and delete it. 
   The file is closed after writing back, and the corresponding vm region
   will be uninstalled. */
void
mmap_write_back_and_delete (struct list *mmap_list, mapid_t mapid)
{
  struct mmap_entry *entry = mmap_lookup (mmap_list, mapid);
  write_back_mmap (entry);
  list_remove (&entry->l_elem);
  free (entry);
}

/* Write back a mmap entry to file. The file is closed after writing back,
   and the corresponding vm region will be uninstalled. */
static void
write_back_mmap (struct mmap_entry *entry) 
{
  ASSERT (entry != NULL);
  struct thread *cur = thread_current ();
  struct proc_info *proc_info = cur->proc_info;
  struct hash *spt = &proc_info->sup_page_table;
  struct lock *spt_lock = &proc_info->spt_lock;
  struct list *vm_region_list = &proc_info->vm_region_list;

  struct file *file = entry->file;
  size_t page_cnt = entry->page_cnt;
  upage_t uaddr = entry->uaddr;

  for (size_t i = 0; i < page_cnt; i++)
    {
      upage_t file_page = uaddr + i * PGSIZE;
      lock_acquire (spt_lock);
      struct spt_entry *spte = suppagedir_lookup (spt, file_page);
      lock_release (spt_lock);
      if (spte == NULL)
        PANIC ("write_back_mmap: spte is NULL");
      
      lock_acquire (&spte->spte_lock);
      size_t read_bytes = spte->read_bytes;
      lock_release (&spte->spte_lock);

      if (pagedir_is_dirty (cur->pagedir, file_page))
        {
          lock_acquire (&filesys_lock);
          file_seek (file, i * PGSIZE);
          file_write (file, file_page, read_bytes);
          lock_release (&filesys_lock);
        }
    }
  lock_acquire (&filesys_lock);
  file_close (entry->file);
  lock_release (&filesys_lock);
  vm_region_uninstall (vm_region_list, entry->uaddr);
}