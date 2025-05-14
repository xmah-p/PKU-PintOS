#ifndef VM_VM_REGION_H
#define VM_VM_REGION_H

#include <stdbool.h>
#include <stddef.h>
#include <list.h>
#include "vm/frame.h"

/* REGION_EXEC: Executable region
   REGION_ZERO: Zeroed region
   REGION_MMAP: Memory-mapped region */
enum region_type { REGION_EXEC, REGION_ZERO, REGION_MMAP };

/* VM region list entry */
struct vm_region
  {
    struct list_elem l_elem;       /* List element */
    enum region_type type;         /* Type of the region */
    upage_t start;                 /* Start address of the region */
    size_t length;                 /* Length of the region */
  };

/* Create and insert a new VM region entry into the list. */
bool vm_region_install (struct list *vm_region_list, 
                        enum region_type type, upage_t start, size_t length);

/* Destroy the VM region list, freeing all VM region entries. */
void vm_region_destroy (struct list *vm_region_list);

/* Check if the given region is not overlapping with any existing region
   in the VM region list. */
bool vm_region_available (struct list *vm_region_list, 
                            upage_t start, size_t length);

/* Remove a VM region entry from the list. */
void vm_region_uninstall (struct list *vm_region_list, upage_t start);

#endif /**< vm/vm_region.h */