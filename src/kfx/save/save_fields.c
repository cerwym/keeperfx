/******************************************************************************/
// KeeperFX — reusable TLV field serializer for versioned save chunks.
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "kfx/save/save_fields.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Write side                                                                  */
/******************************************************************************/

TbBool sfbuf_init(SaveFieldBuf *buf, unsigned long initial_capacity)
{
    buf->data     = (unsigned char *)KfxAlloc(initial_capacity);
    buf->capacity = initial_capacity;
    buf->size     = 0;
    buf->error    = (buf->data == NULL);
    return !buf->error;
}

void sfbuf_free(SaveFieldBuf *buf)
{
    KfxFree(buf->data);
    buf->data     = NULL;
    buf->capacity = 0;
    buf->size     = 0;
    buf->error    = false;
}

TbBool sfbuf_write_field(SaveFieldBuf *buf, unsigned short tag,
                         const void *data, unsigned short len)
{
    if (buf->error)
        return false;

    unsigned long need = buf->size + 4UL + len; /* tag(2)+len(2)+payload */
    if (need > buf->capacity) {
        unsigned long new_cap = buf->capacity * 2;
        if (new_cap < need) new_cap = need;
        unsigned char *p = (unsigned char *)KfxRealloc(buf->data, new_cap);
        if (!p) { buf->error = true; return false; }
        buf->data     = p;
        buf->capacity = new_cap;
    }

    unsigned char *p = buf->data + buf->size;
    /* tag — little-endian uint16 */
    p[0] = (unsigned char)(tag & 0xFFu);
    p[1] = (unsigned char)((tag >> 8) & 0xFFu);
    /* len — little-endian uint16 */
    p[2] = (unsigned char)(len & 0xFFu);
    p[3] = (unsigned char)((len >> 8) & 0xFFu);
    /* payload */
    if (len > 0 && data != NULL)
        memcpy(p + 4, data, len);

    buf->size += 4UL + len;
    return true;
}

TbBool sfbuf_flush_to_file(const SaveFieldBuf *buf, TbFileHandle fh)
{
    if (buf->error)
        return false;
    if (buf->size == 0)
        return true;
    return LbFileWrite(fh, buf->data, buf->size) == (long)buf->size;
}

/******************************************************************************/
/* Read side                                                                   */
/******************************************************************************/

TbBool sfield_read_fields(TbFileHandle fh, long chunk_len,
                          SFieldReadCb cb, void *userdata)
{
    unsigned char data[SFIELD_MAX_DATA_LEN];
    long consumed = 0;

    while (consumed < chunk_len)
    {
        unsigned char hdr[4];
        if (LbFileRead(fh, hdr, 4) != 4)
            return false;
        consumed += 4;

        unsigned short tag = (unsigned short)(hdr[0] | ((unsigned short)hdr[1] << 8));
        unsigned short len = (unsigned short)(hdr[2] | ((unsigned short)hdr[3] << 8));

        if (len > SFIELD_MAX_DATA_LEN)
        {
            /* Unknown large field — skip for forward compatibility. */
            if (LbFileSeek(fh, len, Lb_FILE_SEEK_CURRENT) < 0)
                return false;
            consumed += len;
            continue;
        }

        if (len > 0)
        {
            if (LbFileRead(fh, data, len) != len)
                return false;
            consumed += len;
        }

        cb(tag, (len > 0) ? data : NULL, len, userdata);
    }

    return true;
}