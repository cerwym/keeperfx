/******************************************************************************/
// KeeperFX — reusable TLV field serializer for versioned save chunks.
/******************************************************************************/
/** @file kfx/save/save_fields.h
 *     Generic tag-length-value field writer/reader.
 *
 *     Each save chunk owns its own tag enum (defined in its own header).
 *     This file provides only the byte-level mechanism:
 *
 *     Wire layout per field:   [tag : uint16_t] [len : uint16_t] [data : len bytes]
 *
 *     Readers skip any tag whose len exceeds SFIELD_MAX_DATA_LEN, allowing
 *     forward compatibility when new fields are added.  Unknown tags whose len
 *     is within budget are delivered to the callback; the callback ignores tags
 *     it doesn't recognise.
 */
/******************************************************************************/
#ifndef KFX_SAVE_FIELDS_H
#define KFX_SAVE_FIELDS_H

#include "globals.h"
#include "bflib_fileio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum data payload per field that will be delivered to the read callback.
 * Fields with len > this value are skipped (forward-compat).
 * Camera fields are all <= 4 bytes; 64 is a safe headroom for future chunks. */
#define SFIELD_MAX_DATA_LEN 64

/******************************************************************************/
/* Write side — accumulate fields into a growable buffer, then flush to file.  */
/******************************************************************************/

typedef struct {
    unsigned char *data;
    unsigned long  capacity;
    unsigned long  size;
    TbBool         error;
} SaveFieldBuf;

/** Initialise a SaveFieldBuf with the given initial capacity.
 *  Returns false (and sets buf->error) if the allocation fails. */
TbBool sfbuf_init(SaveFieldBuf *buf, unsigned long initial_capacity);

/** Release heap memory held by buf.  Safe to call on a zero-initialised buf. */
void   sfbuf_free(SaveFieldBuf *buf);

/** Append one field (tag + len + data bytes) to the buffer.
 *  data may be NULL when len == 0 (sentinel / marker fields).
 *  Sets buf->error on allocation failure; subsequent calls are no-ops. */
TbBool sfbuf_write_field(SaveFieldBuf *buf, unsigned short tag,
                         const void *data, unsigned short len);

/** Write buf->size bytes to fh.  Returns false on I/O error or buf->error. */
TbBool sfbuf_flush_to_file(const SaveFieldBuf *buf, TbFileHandle fh);

/* Convenience macros for the common 1-byte and 4-byte cases.
 * All values are serialised as little-endian regardless of host byte order. */
#define SFBUF_WRITE_U8(buf, tag, val) \
    do { unsigned char _v = (unsigned char)(val); \
         sfbuf_write_field((buf), (tag), &_v, 1); } while (0)

#define SFBUF_WRITE_S32(buf, tag, val) \
    do { unsigned int _u = (unsigned int)(int)(val); \
         unsigned char _b[4]; \
         _b[0] = (unsigned char)(_u        & 0xFFu); \
         _b[1] = (unsigned char)((_u >>  8) & 0xFFu); \
         _b[2] = (unsigned char)((_u >> 16) & 0xFFu); \
         _b[3] = (unsigned char)((_u >> 24) & 0xFFu); \
         sfbuf_write_field((buf), (tag), _b, 4); } while (0)

/******************************************************************************/
/* Read side — iterate fields from file, call cb for each.                     */
/******************************************************************************/

/** Callback invoked once per field while reading.
 *  tag      — field tag (chunk-local enum value).
 *  data     — pointer to field payload (stack buffer, valid only during call).
 *             NULL when len == 0.
 *  len      — payload byte count (0 for marker/sentinel fields).
 *  userdata — opaque pointer passed through from sfield_read_fields(). */
typedef void (*SFieldReadCb)(unsigned short tag, const void *data,
                             unsigned short len, void *userdata);

/** Read exactly chunk_len bytes from fh, calling cb for each field.
 *  Fields with payload > SFIELD_MAX_DATA_LEN are skipped silently.
 *  Returns false on I/O error or if fewer than chunk_len bytes are available. */
TbBool sfield_read_fields(TbFileHandle fh, long chunk_len,
                          SFieldReadCb cb, void *userdata);

/** Read a little-endian uint8 from a field payload.  Returns 0 on bad len. */
static inline unsigned char sfield_u8(const void *data, unsigned short len)
{ return (len >= 1) ? *(const unsigned char *)data : 0; }

/** Read a little-endian int32 from a field payload.  Returns 0 on bad len. */
static inline int sfield_s32(const void *data, unsigned short len)
{
    if (len < 4) return 0;
    const unsigned char *p = (const unsigned char *)data;
    return (int)((unsigned int)p[0]
               | ((unsigned int)p[1] << 8)
               | ((unsigned int)p[2] << 16)
               | ((unsigned int)p[3] << 24));
}

#ifdef __cplusplus
}
#endif
#endif /* KFX_SAVE_FIELDS_H */
