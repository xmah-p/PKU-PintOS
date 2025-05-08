/* vm/swap.h */
#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <bitmap.h>
#include "devices/block.h"

/* Initialize swap subsystem (bitmap) */
void swap_init (void);

/* Write a frame’s data to swap, returning its slot index */
block_sector_t swap_write (void *frame);

/* Read a page from swap slot into frame */
void swap_read (block_sector_t slot, void *frame);

/* Free a swap slot */
void swap_free (block_sector_t slot);

#endif /* vm/swap.h */
