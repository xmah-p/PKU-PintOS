#include "vm/vm_region.h"

#include <stdio.h>
#include <list.h>
#include "threads/malloc.h"
#include "threads/vaddr.h"

/* Create and insert a new VM region entry for executable region. */
bool 
vm_region_install (struct list *vm_region_list, 
                   enum region_type type, upage_t start, size_t length)
{
  struct vm_region *region = malloc (sizeof *region);
  if (!region)
    return false;
  region->type = type;
  region->start = start;
  region->length = length;
  list_push_back (vm_region_list, &region->l_elem);
  return true;
}

/* Destroy the VM region list, freeing VM region entries. */
void
vm_region_destroy (struct list *vm_region_list)
{
  struct list_elem *e;
  struct vm_region *region;

  for (e = list_begin (vm_region_list); e != list_end (vm_region_list);)
    {
      region = list_entry (e, struct vm_region, l_elem);
      e = list_remove (e);
      free (region);
    }
}

/* Check if the given region is not overlapping with any existing region
   in the VM region list. */
bool
vm_region_available (struct list *vm_region_list, 
                       upage_t start, size_t length)
{
  struct list_elem *e;
  struct vm_region *region;

  if (!is_user_vaddr (start) || !is_user_vaddr (start + length))
    return false;

  for (e = list_begin (vm_region_list); e != list_end (vm_region_list); 
       e = list_next (e))
    {
      region = list_entry (e, struct vm_region, l_elem);
      if ((start < region->start + region->length) && 
          (region->start < start + length))
        return false;
    }
  return true;
}


/* Remove a VM region entry from the list. */
void
vm_region_uninstall (struct list *vm_region_list, upage_t start)
{
  struct list_elem *e;
  struct vm_region *region;

  for (e = list_begin (vm_region_list); e != list_end (vm_region_list); 
       e = list_next (e))
    {
      region = list_entry (e, struct vm_region, l_elem);
      if (region->start == start)
        {
          list_remove (&region->l_elem);
          free (region);
          return;
        }
    }
  PANIC ("vm_region_uninstall: region not found");
}
