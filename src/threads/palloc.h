#ifndef THREADS_PALLOC_H
#define THREADS_PALLOC_H

#include <stddef.h>

typedef void * kpage_t; /* Kernel page (physical address) */
typedef void * upage_t; /* User page (virtual address) */

/** How to allocate pages. */
enum palloc_flags
  {
    PAL_ASSERT = 001,           /**< Panic on failure. */
    PAL_ZERO = 002,             /**< Zero page contents. */
    PAL_USER = 004              /**< User page. */
  };

void palloc_init (size_t user_page_limit);
kpage_t palloc_get_page (enum palloc_flags);
kpage_t palloc_get_multiple (enum palloc_flags, size_t page_cnt);
void palloc_free_page (kpage_t);
void palloc_free_multiple (kpage_t, size_t page_cnt);

#endif /**< threads/palloc.h */
