/*
 * aesd-circular-buffer.h
 *
 *  Created on: March 1st, 2020
 *      Author: Dan Walkes
 */

#ifndef AESD_CIRCULAR_BUFFER_H
#define AESD_CIRCULAR_BUFFER_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/list.h>
#include <linux/slab.h>
#else
#include <stddef.h> // size_t
#include <stdint.h> // uintx_t
#include <stdbool.h>
#include <stdio.h>
#include "my_list.h"
#endif

#define CIRCULAR_BUFFER_DEBUG 1  //Remove comment on this line to enable debug

#undef CBDEBUG             /* undef it, just in case */
#ifdef CIRCULAR_BUFFER_DEBUG
#  ifdef __KERNEL__
     /* This one if debugging is on, and kernel space */
#    define CBDEBUG(fmt, args...) printk( KERN_DEBUG "circular_buffer: " fmt, ## args)
#  else
     /* This one for user space */
#    define CBDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define CBDEBUG(fmt, args...) /* not debugging: nothing */
#endif

#  ifdef __KERNEL__
     /* This one if debugging is on, and kernel space */
#    define CBWARNING(fmt, args...) printk( KERN_WARNING "circular_buffer: " fmt, ## args)
#    define CBERROR(fmt, args...)   printk( KERN_WARNING "circular_buffer: " fmt, ## args)
#  else
     /* This one for user space */
#    define CBWARNING(fmt, args...) fprintf(stderr, fmt, ## args)
#    define CBERROR(fmt, args...)   fprintf(stderr, fmt, ## args)
#  endif

#define AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED 10

struct aesd_buffer_entry
{
    /**
     * A location where the buffer contents in buffptr are stored
     */
    const char *buffptr;
    /**
     * Number of bytes stored in buffptr
     */
    size_t size;
};

struct list_buffer_entry_s {

    struct aesd_buffer_entry entry;
    struct list_head entries;
};

struct aesd_circular_buffer
{
    /**
     * Head of the buffer list
     */
    struct list_head list_head;
    /**
     * Number of elements in the buffer list.
     */
    uint8_t element_count;
};

extern struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn );

extern const char *aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry);

extern void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer);

/**
 * Create a for loop to iterate over each member of the circular buffer.
 * Useful when you've allocated memory for circular buffer entries and need to free it
 * @param entryptr is a struct aesd_buffer_entry* to set with the current entry
 * @param buffer is the struct aesd_buffer * describing the buffer
 * @param index is a uint8_t stack allocated value used by this macro for an index
 * Example usage:
 * uint8_t index;
 * struct aesd_circular_buffer buffer;
 * struct aesd_buffer_entry *entry;
 * AESD_CIRCULAR_BUFFER_FOREACH(entry,&buffer,index) {
 *      free(entry->buffptr);
 * }
 */
/*
#define AESD_CIRCULAR_BUFFER_FOREACH(entryptr,buffer,index) \
    for(index=0, entryptr=&((buffer)->entry[index]); \
            index<AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; \
            index++, entryptr=&((buffer)->entry[index]))
*/


#endif /* AESD_CIRCULAR_BUFFER_H */
