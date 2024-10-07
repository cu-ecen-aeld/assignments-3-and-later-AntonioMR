/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#include <stdlib.h>
#endif

#ifdef __KERNEL__
    #define my_alloc(size)  kmalloc(size, GFP_KERNEL)
    #define my_free(ptr)    kfree(ptr)
#else
    #define my_alloc(size)  malloc(size)
    #define my_free(ptr)    free(ptr)
#endif

//#include <errno-base.h>
#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    struct aesd_buffer_entry *buffer_entry_rtn = NULL;
    
    if ((buffer != NULL) && (entry_offset_byte_rtn != NULL)) {

        struct list_buffer_entry_s *list_entry;
        list_for_each_entry(list_entry, &buffer->list_head, entries) {

            if (list_entry->entry.size > char_offset) {
                buffer_entry_rtn = &list_entry->entry;
                *entry_offset_byte_rtn = char_offset;
                break;

            } else {
                char_offset -= list_entry->entry.size;
            }
        }
    }

    return buffer_entry_rtn;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
* @return pointer to the buffer that must be deallocated when circular buffer is full
* NULL if circular buffer is no full.
*/
const char *aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    const char *ret_buffptr = NULL;
    struct list_buffer_entry_s *new_entry = NULL;

    if ((buffer == NULL) || (add_entry == NULL)){
        CBERROR("NULL parameter received in buffer_init function\n");
        return NULL;
    }

    // if list has AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED elements remove header element
    if (buffer->element_count == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
        struct list_buffer_entry_s *first_entry = list_first_entry(&buffer->list_head, struct list_buffer_entry_s, entries);
        ret_buffptr = first_entry->entry.buffptr;
        CBDEBUG("buffer with size %d, unlink from circular buffer. Sent to free by caller\n", (int)first_entry->entry.size);
        list_del(&first_entry->entries);
        my_free(first_entry);
        CBDEBUG("deallocated %d bytes from list element entry\n", (int)sizeof(struct list_buffer_entry_s));
        buffer->element_count--;
    }

    // insert the new element at the tail
    new_entry = my_alloc(sizeof(struct list_buffer_entry_s));
    if (new_entry != NULL) {
        CBDEBUG("allocated %d bytes for list element entry\n", (int)sizeof(struct list_buffer_entry_s));
        new_entry->entry.buffptr = add_entry->buffptr;
        new_entry->entry.size = add_entry->size;
        list_add_tail(&new_entry->entries, &buffer->list_head);
        buffer->element_count++;

        CBDEBUG("buffer with size %d, link to circular buffer tail. buffer size: %d\n", \
                                        (int)new_entry->entry.size, \
                                        (int)buffer->element_count);

    } else {
        CBERROR("Unable to allocate memory for new list element entry\n");
        return NULL;  // Error de asignación de memoria
    }

    return ret_buffptr;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    if (buffer == NULL){
        CBERROR("NULL parameter received in buffer_init function\n");
        return;
    }

    // Init list head
    INIT_LIST_HEAD(&buffer->list_head);
    
    // Initial state list empty
    buffer->element_count = 0;

    CBDEBUG("Buffer init. Elements cnt %d\n", buffer->element_count);
}
