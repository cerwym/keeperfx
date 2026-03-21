/*
 * vita-uvdb — intra-process GDB remote stub for PS Vita
 * Original: https://github.com/sleirsgoevy/vita-uvdb  (sleirsgoevy)
 * Fork additions: Z0/z0 software breakpoints, s/vCont single-step,
 *                 D (detach), swbreak stop-reason.
 *
 * Breakpoint mechanism:
 *   - Z0: save original instruction, write UDF (ARM) or BKPT (Thumb),
 *     flush I-cache via kuKernelFlushCaches.
 *   - On UNDEFINED_INSTRUCTION at a breakpoint addr: restore original,
 *     report SIGTRAP with swbreak.
 *   - Continue-past-breakpoint: single-step one instruction past the
 *     restored original, then re-insert the breakpoint.
 *
 * Single-step mechanism:
 *   - Decode the instruction at PC to find next-PC target(s).
 *   - Place temporary breakpoint(s) at the target(s).
 *   - Continue; on break at temp BP, remove it, report SIGTRAP.
 *   - ARM + Thumb instruction decoding for branches, LDR PC, etc.
 */

#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <stdarg.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <psp2/net/net_syscalls.h>
#include <psp2/kernel/threadmgr/msgpipe.h>
#include <kubridge.h>
#include "uvdb.h"

/* ── External symbols ──────────────────────────────────────────────── */

void _sceKernelExitProcessForUser(int);
int _sceKernelSendMsgPipeVector(SceUID, const SceKernelAddrPair*, unsigned int, uint32_t* rest);
int _sceKernelReceiveMsgPipeVector(SceUID, const SceKernelAddrPair*, unsigned int, uint32_t* rest);
extern char __executable_start[];
extern char __init_array_start[];

/* ── Locking ───────────────────────────────────────────────────────── */

static int uvdb_lock_state;

static void uvdb_lock(void)
{
    for(;;)
    {
        int old_value = 0;
        if(__atomic_compare_exchange_n(&uvdb_lock_state, &old_value, 1, 0,
            __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
            return;
    }
}

static void uvdb_unlock(void)
{
    __atomic_store_n(&uvdb_lock_state, 0, __ATOMIC_SEQ_CST);
}

/* ── Globals ───────────────────────────────────────────────────────── */

static int uvdb_socket = -1;
static SceUID uvdb_pipe = -1;

/* ── Buffer I/O ────────────────────────────────────────────────────── */

struct buffer
{
    SceUID memblock_uid;
    char* buf;
    size_t size;
    size_t cap;
    size_t packet_start;
};

static void buffer_popleft(struct buffer* buf, size_t cnt)
{
    memmove(buf->buf, buf->buf+cnt, buf->size-cnt);
    buf->size -= cnt;
}

static size_t buffer_getspace(struct buffer* buf, char** pos)
{
    if(buf->size == buf->cap)
    {
        size_t cap2 = buf->cap * 2;
        if(!cap2)
            cap2 = 4096;
        SceUID memblock2 = sceKernelAllocMemBlock("gdb socket buffer",
            SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, cap2, NULL);
        if(memblock2 < 0)
        {
            *pos = NULL;
            return 0;
        }
        void* base;
        sceKernelGetMemBlockBase(memblock2, &base);
        memcpy(base, buf->buf, buf->size);
        sceKernelFreeMemBlock(buf->memblock_uid);
        buf->memblock_uid = memblock2;
        buf->buf = base;
        buf->cap = cap2;
    }
    *pos = buf->buf + buf->size;
    return buf->cap - buf->size;
}

static size_t buffer_poll(struct buffer* buf, char** pos)
{
    size_t chk_size = buffer_getspace(buf, pos);
    uint32_t args[6] = {uvdb_socket, (uint32_t)*pos, chk_size, 0, 0, 0};
    ssize_t ans = sceNetSyscallRecvfrom((void*)args);
    if(ans < 0)
        ans = 0;
    buf->size += ans;
    return ans;
}

static void buffer_write(struct buffer* buf, const char* data, size_t sz)
{
    while(sz)
    {
        char* pos;
        size_t chk = buffer_getspace(buf, &pos);
        if(chk > sz)
            chk = sz;
        memcpy(pos, data, chk);
        data += chk;
        buf->size += chk;
        sz -= chk;
    }
}

static void buffer_start_packet(struct buffer* buf)
{
    buffer_write(buf, "$", 1);
    buf->packet_start = buf->size;
}

static char int2hex(int value)
{
    if(value < 10)
        return value + '0';
    return value - 10 + 'a';
}

static void buffer_end_packet(struct buffer* buf)
{
    uint8_t cksum = 0;
    for(size_t i = buf->packet_start; i < buf->size; i++)
        cksum += (uint8_t)buf->buf[i];
    uint8_t footer[3] = {'#', int2hex(cksum>>4), int2hex(cksum&15)};
    buffer_write(buf, (const char*)footer, 3);
}

static void buffer_flush(struct buffer* buf)
{
    size_t pos = 0;
    while(pos < buf->size)
    {
        uint32_t args[6] = {uvdb_socket, (uint32_t)(buf->buf+pos), buf->size-pos, 0, 0, 0};
        ssize_t chk = sceNetSyscallSendto((void*)args);
        if(chk < 0)
            chk = 0;
        pos += chk;
    }
    buf->size = 0;
}

static struct buffer in_buf, out_buf;

#define POLL() while(cur == end) { size_t sz = buffer_poll(&in_buf, &cur); end = cur + sz; }

static size_t recv_packet(char** data)
{
    char* cur = in_buf.buf;
    char* end = cur + in_buf.size;
retry:;
    char c = 0;
    while(c != '$')
    {
        POLL();
        c = *cur++;
    }
    size_t start_packet = cur - in_buf.buf;
    while(c != '#')
    {
        POLL();
        c = *cur++;
    }
    size_t end_packet = cur - in_buf.buf - 1;
    uint8_t cksum = 0;
    for(size_t i = start_packet; i < end_packet; i++)
        cksum += (uint8_t)in_buf.buf[i];
    POLL();
    char c1 = *cur++;
    POLL();
    char c2 = *cur++;
    if(c1 != int2hex(cksum>>4) || c2 != int2hex(cksum&15))
        goto retry;
    buffer_write(&out_buf, "+", 1);
    buffer_flush(&out_buf);
    *data = in_buf.buf + start_packet;
    in_buf.buf[end_packet] = 0;
    return end_packet - start_packet;
}

static void discard_packet(char* data, size_t sz)
{
    buffer_popleft(&in_buf, data - in_buf.buf + sz + 3);
}

static void send_packet(void)
{
    buffer_end_packet(&out_buf);
    buffer_flush(&out_buf);
    char* cur = in_buf.buf;
    char* end = cur + in_buf.size;
    char c = 0;
    while(c != '+')
    {
        POLL();
        c = *cur++;
    }
    buffer_popleft(&in_buf, cur - in_buf.buf);
}

#undef POLL

/* ── Hex / stream helpers ──────────────────────────────────────────── */

#define IS(s)         (sz == sizeof(s) - 1 && !memcmp(pkt, s, sizeof(s) - 1))
#define STARTSWITH(s) (sz >= sizeof(s) - 1 && !memcmp(pkt, s, sizeof(s) - 1))
#define STRING(s)     s, sizeof(s) - 1

struct stream { uint64_t cur; uint64_t start; uint64_t end; };

#define PARSE_HEX(type, name, cond) static type name(char** s)\
{\
    type ans = 0;\
    for(cond)\
    {\
        char c = *(*s)++;\
        if(c >= '0' && c <= '9')\
            ans = 16 * ans + (c - '0');\
        else\
        {\
            c &= -33;\
            if(c >= 'A' && c <= 'F')\
                ans = 16 * ans + 10 + (c - 'A');\
            else\
                return ans;\
        }\
    }\
    return ans;\
}

PARSE_HEX(uint64_t, parse_hex, ;;)
PARSE_HEX(uint8_t, parse_hex_byte, int c = 0; c < 2; c++)

#undef PARSE_HEX

static struct stream parse_stream(char* s)
{
    struct stream ans = {};
    ans.start = parse_hex(&s);
    ans.end = parse_hex(&s);
    return ans;
}

static void stream_write(struct stream* st, char* buf, size_t sz)
{
    if(st->cur < st->start)
    {
        size_t chk = st->start - st->cur;
        if(sz <= chk) { st->cur += sz; return; }
    }
    if(st->cur < st->end)
    {
        size_t chk = st->end - st->cur;
        if(sz < chk) chk = sz;
        if(chk && st->cur == st->start)
            buffer_write(&out_buf, "m", 1);
        buffer_write(&out_buf, buf, chk);
        st->cur += chk;
    }
}

static void stream_close(struct stream* st)
{
    if(st->cur <= st->start)
        buffer_write(&out_buf, "l", 1);
}

static void write_hex(char* start, size_t sz)
{
    while(sz--)
    {
        uint8_t c = *start++;
        uint8_t q[2] = {int2hex(c>>4), int2hex(c&15)};
        buffer_write(&out_buf, (const char*)q, 2);
    }
}

static void read_hex(char** p, char* start, size_t sz)
{
    while(sz--)
    {
        if(**p == 'x' && (*p)[1] == 'x')
        {
            *p += 2;
            start++;
        }
        else if(!**p || !(*p)[1])
            *start++ = 0;
        else
            *start++ = parse_hex_byte(p);
    }
}

static void skip_hex(char** p, size_t cnt)
{
    *p += strnlen(*p, 2*cnt);
}

static void write_x(size_t sz)
{
    while(sz--)
        buffer_write(&out_buf, "xx", 2);
}

/* ── Safe memory copy (via kernel msg-pipe, catches faults) ────────── */

static size_t safe_memcpy(char* dst, const char* src, size_t sz)
{
    size_t ans = 0;
    while(sz)
    {
        size_t chk;
        uint32_t rest[3] = {1, (uint32_t)&chk, 0};
        SceKernelAddrPair q = {(uint32_t)src, sz};
        if(_sceKernelSendMsgPipeVector(uvdb_pipe, &q, 1, rest))
            break;
        ans += chk;
        src += chk;
        sz -= chk;
        while(chk)
        {
            size_t chk2;
            uint32_t rest[3] = {1, (uint32_t)&chk2, 0};
            SceKernelAddrPair q = {(uint32_t)dst, chk};
            if(_sceKernelReceiveMsgPipeVector(uvdb_pipe, &q, 1, rest))
                chk2 = 0;
            dst += chk2;
            chk -= chk2;
        }
    }
    return ans;
}

/* ── Software breakpoint table ─────────────────────────────────────── */

#define MAX_BREAKPOINTS 64

struct sw_breakpoint {
    uint32_t addr;       /* instruction address (bit 0 clear) */
    uint32_t orig_instr; /* saved original bytes (2 or 4) */
    uint8_t  kind;       /* 2 = Thumb (16-bit), 3 = Thumb-2 (32-bit), 4 = ARM (32-bit) */
    uint8_t  active;     /* 1 = breakpoint is live in memory */
};

static struct sw_breakpoint bp_table[MAX_BREAKPOINTS];
static int bp_count = 0;

/* Temporary breakpoints for single-step (at most 2: taken + not-taken) */
static struct sw_breakpoint step_bp[2];
static int step_bp_count = 0;

/* When we hit a persistent breakpoint and need to continue past it,
 * we remember which one so we can re-insert after stepping. */
static int reinsert_bp_idx = -1;

/* ARM UDF instruction: always undefined, used as software breakpoint */
#define ARM_BKPT_INSTR   0xE7F001F0u  /* UDF #0 in ARM mode */
#define THUMB_BKPT_INSTR 0xDE01u      /* UDF #1 in Thumb mode (16-bit) */

static struct sw_breakpoint* bp_find(uint32_t addr)
{
    for(int i = 0; i < bp_count; i++)
        if(bp_table[i].addr == addr && bp_table[i].active)
            return &bp_table[i];
    return NULL;
}

static int bp_insert(uint32_t addr, uint8_t kind)
{
    /* Already have one at this address? */
    if(bp_find(addr))
        return 0; /* OK, already set */

    if(bp_count >= MAX_BREAKPOINTS)
        return -1;

    struct sw_breakpoint* bp = &bp_table[bp_count];
    bp->addr = addr;
    bp->kind = kind;
    bp->active = 1;

    /* Save original instruction */
    size_t instr_size = (kind == 2) ? 2 : 4;
    if(safe_memcpy((char*)&bp->orig_instr, (const char*)addr, instr_size) != instr_size)
        return -1;

    /* Write breakpoint instruction */
    if(kind == 2)
    {
        uint16_t bkpt = (uint16_t)THUMB_BKPT_INSTR;
        kuKernelCpuUnrestrictedMemcpy((void*)addr, &bkpt, 2);
        kuKernelFlushCaches((void*)addr, 2);
    }
    else
    {
        uint32_t bkpt = ARM_BKPT_INSTR;
        kuKernelCpuUnrestrictedMemcpy((void*)addr, &bkpt, 4);
        kuKernelFlushCaches((void*)addr, 4);
    }

    bp_count++;
    return 0;
}

static int bp_remove(uint32_t addr)
{
    for(int i = 0; i < bp_count; i++)
    {
        if(bp_table[i].addr == addr && bp_table[i].active)
        {
            size_t instr_size = (bp_table[i].kind == 2) ? 2 : 4;
            kuKernelCpuUnrestrictedMemcpy((void*)addr, &bp_table[i].orig_instr, instr_size);
            kuKernelFlushCaches((void*)addr, instr_size);
            bp_table[i].active = 0;

            /* Compact: move last entry into this slot */
            if(i < bp_count - 1)
                bp_table[i] = bp_table[bp_count - 1];
            bp_count--;
            return 0;
        }
    }
    return -1; /* not found */
}

static void bp_remove_all(void)
{
    while(bp_count > 0)
        bp_remove(bp_table[0].addr);
}

/* Insert/remove a temporary step breakpoint */
static int step_bp_insert(uint32_t addr, uint8_t kind)
{
    if(step_bp_count >= 2)
        return -1;
    struct sw_breakpoint* bp = &step_bp[step_bp_count];
    bp->addr = addr;
    bp->kind = kind;
    bp->active = 1;

    size_t instr_size = (kind == 2) ? 2 : 4;
    if(safe_memcpy((char*)&bp->orig_instr, (const char*)addr, instr_size) != instr_size)
        return -1;

    if(kind == 2)
    {
        uint16_t bkpt = (uint16_t)THUMB_BKPT_INSTR;
        kuKernelCpuUnrestrictedMemcpy((void*)addr, &bkpt, 2);
        kuKernelFlushCaches((void*)addr, 2);
    }
    else
    {
        uint32_t bkpt = ARM_BKPT_INSTR;
        kuKernelCpuUnrestrictedMemcpy((void*)addr, &bkpt, 4);
        kuKernelFlushCaches((void*)addr, 4);
    }

    step_bp_count++;
    return 0;
}

static void step_bp_remove_all(void)
{
    for(int i = 0; i < step_bp_count; i++)
    {
        if(step_bp[i].active)
        {
            size_t instr_size = (step_bp[i].kind == 2) ? 2 : 4;
            kuKernelCpuUnrestrictedMemcpy((void*)step_bp[i].addr,
                &step_bp[i].orig_instr, instr_size);
            kuKernelFlushCaches((void*)step_bp[i].addr, instr_size);
            step_bp[i].active = 0;
        }
    }
    step_bp_count = 0;
}

static int is_step_bp(uint32_t addr)
{
    for(int i = 0; i < step_bp_count; i++)
        if(step_bp[i].addr == addr && step_bp[i].active)
            return 1;
    return 0;
}

/* ── ARM/Thumb instruction decoder for single-step ─────────────────── */
/*
 * Given the current PC and SPSR, determine the next PC(s).
 * Returns the number of targets (1 or 2 for conditional branches).
 * Targets are written to next_pc[0] (and next_pc[1] if conditional).
 *
 * We only need to identify PC-modifying instructions; everything else
 * is "next_pc = PC + instruction_size".
 */

static uint32_t safe_read32(uint32_t addr)
{
    uint32_t val = 0;
    safe_memcpy((char*)&val, (const char*)addr, 4);
    return val;
}

static uint16_t safe_read16(uint32_t addr)
{
    uint16_t val = 0;
    safe_memcpy((char*)&val, (const char*)addr, 2);
    return val;
}

static uint32_t reg_val(KuKernelExceptionContext* ctx, int reg)
{
    if(reg < 16)
        return ((uint32_t*)ctx)[reg]; /* r0-r15 are the first 16 words */
    return 0;
}

/* Sign-extend a value from bit_count bits to 32 bits */
static int32_t sign_extend(uint32_t val, int bit_count)
{
    if(val & (1u << (bit_count - 1)))
        val |= ~((1u << bit_count) - 1);
    return (int32_t)val;
}

/* Evaluate ARM condition code against SPSR flags */
static int arm_condition_passes(uint32_t spsr, uint8_t cond)
{
    int N = (spsr >> 31) & 1;
    int Z = (spsr >> 30) & 1;
    int C = (spsr >> 29) & 1;
    int V = (spsr >> 28) & 1;

    switch(cond >> 1)
    {
    case 0: return Z;             /* EQ/NE */
    case 1: return C;             /* CS/CC */
    case 2: return N;             /* MI/PL */
    case 3: return V;             /* VS/VC */
    case 4: return C && !Z;       /* HI/LS */
    case 5: return N == V;        /* GE/LT */
    case 6: return N == V && !Z;  /* GT/LE */
    case 7: return 1;             /* AL */
    }
    /* Bit 0 inverts the condition (except AL) */
    return 0; /* unreachable */
}

static int compute_next_pc_arm(KuKernelExceptionContext* ctx, uint32_t pc,
                               uint32_t instr, uint32_t next_pc[2])
{
    uint8_t cond = (instr >> 28) & 0xF;
    uint32_t sequential = pc + 4;

    /* If condition will not pass AND it's not AL, next is sequential */
    if(cond != 0xE && !arm_condition_passes(ctx->SPSR, cond))
    {
        next_pc[0] = sequential;
        return 1;
    }

    uint8_t op1 = (instr >> 25) & 0x7;

    /* B / BL (op1 = 0b101) */
    if(op1 == 5)
    {
        int32_t offset = sign_extend(instr & 0x00FFFFFFu, 24) << 2;
        uint32_t target = pc + 8 + offset;

        /* BLX (bit 24 set + cond=0xF in newer encoding) */
        if(cond == 0xF)
        {
            /* BLX immediate: H bit (bit 24) adds 2 for Thumb target */
            int H = (instr >> 24) & 1;
            target = pc + 8 + offset + (H << 1);
            target |= 1; /* Thumb bit */
        }

        if(cond != 0xE && cond != 0xF)
        {
            /* Conditional branch: two targets */
            next_pc[0] = target;
            next_pc[1] = sequential;
            return 2;
        }
        next_pc[0] = target;
        return 1;
    }

    /* BX / BLX register (0x012FFF1x / 0x012FFF3x) */
    if((instr & 0x0FFFFF00) == 0x012FFF00)
    {
        uint8_t op = (instr >> 4) & 0xF;
        if(op == 1 || op == 3) /* BX or BLX */
        {
            int rm = instr & 0xF;
            uint32_t target = reg_val(ctx, rm);
            next_pc[0] = target;
            return 1;
        }
    }

    /* Data processing with Rd = PC (instruction modifies PC) */
    if(op1 <= 1)
    {
        uint8_t rd = (instr >> 12) & 0xF;
        if(rd == 15)
        {
            /* PC is destination — too complex to decode fully,
             * fall through to sequential + software step will catch it.
             * In practice this is rare in compiler-generated code. */
            next_pc[0] = sequential;
            return 1;
        }
    }

    /* LDR Rd, [...] with Rd = PC */
    if((op1 == 2 || op1 == 3) && ((instr >> 20) & 1)) /* Load bit set */
    {
        uint8_t rd = (instr >> 12) & 0xF;
        if(rd == 15)
        {
            /* LDR PC, [...] — compute the effective address and read */
            /* For simplicity, just use sequential; the BP will catch
             * the actual destination when it executes. */
            next_pc[0] = sequential;
            return 1;
        }
    }

    /* LDM/POP with PC in register list */
    if(op1 == 4 && ((instr >> 20) & 1) && (instr & (1 << 15)))
    {
        /* Count registers below PC to find stack offset */
        uint16_t reglist = instr & 0xFFFF;
        int count = 0;
        for(int i = 0; i < 15; i++)
            if(reglist & (1 << i))
                count++;

        uint8_t rn = (instr >> 16) & 0xF;
        uint32_t base = reg_val(ctx, rn);
        int U = (instr >> 23) & 1; /* Up/Down */
        int P = (instr >> 24) & 1; /* Pre/Post */

        uint32_t pc_addr;
        if(U)
            pc_addr = base + (P ? (count + 1) * 4 : count * 4);
        else
            pc_addr = base - (P ? (count + 1) * 4 : count * 4);

        uint32_t target = safe_read32(pc_addr);
        next_pc[0] = target;
        return 1;
    }

    /* Default: sequential */
    next_pc[0] = sequential;
    return 1;
}

static int compute_next_pc_thumb(KuKernelExceptionContext* ctx, uint32_t pc,
                                 uint32_t next_pc[2])
{
    uint16_t hw1 = safe_read16(pc);
    uint32_t sequential = pc + 2;

    /* Check if this is a 32-bit Thumb instruction */
    int is_32bit = ((hw1 >> 11) >= 0x1D); /* 0b11101, 0b11110, 0b11111 */

    if(is_32bit)
    {
        uint16_t hw2 = safe_read16(pc + 2);
        uint32_t instr32 = ((uint32_t)hw1 << 16) | hw2;
        sequential = pc + 4;

        /* BL / BLX (Thumb-2) */
        if((hw1 & 0xF800) == 0xF000 && (hw2 & 0xD000) == 0xD000)
        {
            /* BL immediate: S:I1:I2:imm10:imm11 */
            int S = (hw1 >> 10) & 1;
            int I1 = !((hw2 >> 13) & 1) ^ S;
            int I2 = !((hw2 >> 11) & 1) ^ S;
            uint32_t imm = (S << 24) | (I1 << 23) | (I2 << 22) |
                           ((hw1 & 0x3FF) << 12) | ((hw2 & 0x7FF) << 1);
            int32_t offset = sign_extend(imm, 25);
            uint32_t target = pc + 4 + offset;

            /* BLX: bit 12 of hw2 == 0 means exchange to ARM */
            if(!((hw2 >> 12) & 1))
                target &= ~3u; /* Align to 4 for ARM mode */

            next_pc[0] = target;
            return 1;
        }

        /* Conditional branch (B<cond>.W) */
        if((hw1 & 0xF800) == 0xF000 && (hw2 & 0xD000) == 0x8000)
        {
            uint8_t cond = (hw1 >> 6) & 0xF;
            int S = (hw1 >> 10) & 1;
            int J1 = (hw2 >> 13) & 1;
            int J2 = (hw2 >> 11) & 1;
            uint32_t imm = (S << 20) | (J2 << 19) | (J1 << 18) |
                           ((hw1 & 0x3F) << 12) | ((hw2 & 0x7FF) << 1);
            int32_t offset = sign_extend(imm, 21);
            uint32_t target = pc + 4 + offset;

            if(!arm_condition_passes(ctx->SPSR, cond))
            {
                next_pc[0] = sequential;
                return 1;
            }
            /* Conditional: provide both targets */
            next_pc[0] = target | 1; /* stay in Thumb */
            next_pc[1] = sequential | 1;
            return 2;
        }

        /* Unconditional branch (B.W) */
        if((hw1 & 0xF800) == 0xF000 && (hw2 & 0xD000) == 0x9000)
        {
            int S = (hw1 >> 10) & 1;
            int I1 = !((hw2 >> 13) & 1) ^ S;
            int I2 = !((hw2 >> 11) & 1) ^ S;
            uint32_t imm = (S << 24) | (I1 << 23) | (I2 << 22) |
                           ((hw1 & 0x3FF) << 12) | ((hw2 & 0x7FF) << 1);
            int32_t offset = sign_extend(imm, 25);
            next_pc[0] = (pc + 4 + offset) | 1;
            return 1;
        }

        /* LDR.W Rd, [...] with Rd = PC — rare, use sequential */
        /* TBB/TBH — table branch, use sequential */
        next_pc[0] = sequential;
        return 1;
    }

    /* ── 16-bit Thumb instructions ─────────────────────────────────── */

    /* Conditional branch: B<cond> (narrow) */
    if((hw1 & 0xF000) == 0xD000)
    {
        uint8_t cond = (hw1 >> 8) & 0xF;
        if(cond == 0xE)
        {
            /* UDF — not a branch */
            next_pc[0] = sequential;
            return 1;
        }
        if(cond == 0xF)
        {
            /* SVC — not a branch for our purposes */
            next_pc[0] = sequential;
            return 1;
        }
        int32_t offset = sign_extend((hw1 & 0xFF) << 1, 9);
        uint32_t target = pc + 4 + offset;
        if(!arm_condition_passes(ctx->SPSR, cond))
        {
            next_pc[0] = sequential;
            return 1;
        }
        next_pc[0] = target | 1;
        next_pc[1] = sequential | 1;
        return 2;
    }

    /* Unconditional branch B (narrow) */
    if((hw1 & 0xF800) == 0xE000)
    {
        int32_t offset = sign_extend((hw1 & 0x7FF) << 1, 12);
        next_pc[0] = (pc + 4 + offset) | 1;
        return 1;
    }

    /* BX / BLX register */
    if((hw1 & 0xFF80) == 0x4700)
    {
        int rm = (hw1 >> 3) & 0xF;
        next_pc[0] = reg_val(ctx, rm);
        return 1;
    }

    /* MOV PC, Rm  (ADD PC, Rm  encoded as MOV) */
    if((hw1 & 0xFF87) == 0x4687)
    {
        int rm = (hw1 >> 3) & 0xF;
        next_pc[0] = reg_val(ctx, rm);
        return 1;
    }

    /* POP with PC */
    if((hw1 & 0xFF00) == 0xBD00)
    {
        /* Count registers below PC to find SP offset */
        uint8_t reglist = hw1 & 0xFF;
        int count = 0;
        for(int i = 0; i < 8; i++)
            if(reglist & (1 << i))
                count++;
        uint32_t sp = reg_val(ctx, 13);
        uint32_t target = safe_read32(sp + count * 4);
        next_pc[0] = target;
        return 1;
    }

    /* Default: sequential Thumb */
    next_pc[0] = sequential | 1;
    return 1;
}

/*
 * Top-level: compute next PC from context.  Handles ARM/Thumb mode.
 * The bit-0 of returned addresses indicates Thumb mode (GDB convention).
 */
static int compute_next_pc(KuKernelExceptionContext* ctx, uint32_t next_pc[2])
{
    uint32_t pc = ctx->pc;
    int thumb = (ctx->SPSR & 32) != 0;

    if(thumb)
    {
        return compute_next_pc_thumb(ctx, pc, next_pc);
    }
    else
    {
        uint32_t instr = safe_read32(pc);
        return compute_next_pc_arm(ctx, pc, instr, next_pc);
    }
}

/* Place temporary step breakpoints and continue */
static void do_single_step(KuKernelExceptionContext* ctx)
{
    uint32_t targets[2];
    int n = compute_next_pc(ctx, targets);

    step_bp_remove_all(); /* clean up any stale ones */

    for(int i = 0; i < n; i++)
    {
        uint32_t addr = targets[i];
        uint8_t kind;
        if(addr & 1)
        {
            addr &= ~1u;
            kind = 2; /* Thumb */
        }
        else
        {
            kind = 4; /* ARM */
        }

        /* Don't set a step BP on top of an existing persistent BP */
        if(!bp_find(addr))
            step_bp_insert(addr, kind);
    }
}

/* ── Stop reason tracking ──────────────────────────────────────────── */

/* Why we stopped: used to build the T-packet stop reply */
enum stop_reason {
    STOP_SIGNAL,     /* generic signal (SIGSEGV, SIGILL, etc) */
    STOP_SWBREAK,    /* software breakpoint hit */
    STOP_STEP,       /* single-step completed */
};

static enum stop_reason last_stop_reason = STOP_SIGNAL;

/* ── Main GDB RSP loop ────────────────────────────────────────────── */

static void uvdb_main_loop(KuKernelExceptionContext* ctx, int stop_signal)
{
    for(;;)
    {
        char* pkt;
        size_t sz = recv_packet(&pkt);
        buffer_start_packet(&out_buf);

        if(STARTSWITH("qSupported:"))
        {
            buffer_write(&out_buf, STRING("swbreak+;hwbreak-;qXfer:features:read+;vContSupported+"));
        }
        else if(STARTSWITH("qXfer:features:read:target.xml:"))
        {
            struct stream st = parse_stream(pkt + sizeof("qXfer:features:read:target.xml:") - 1);
            stream_write(&st, STRING(
                "<?xml version=\"1.0\"?>\n"
                "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n"
                "<target>\n"
                "<architecture>armv7</architecture>\n"
                "<osabi>GNU/Linux</osabi>\n"
                "</target>\n"));
            stream_close(&st);
        }
        else if(IS("?"))
        {
            /* Stop reply with reason */
            uint8_t rpkt[3] = {'T', int2hex(stop_signal>>4), int2hex(stop_signal&15)};
            buffer_write(&out_buf, (const char*)rpkt, 3);
            if(last_stop_reason == STOP_SWBREAK)
                buffer_write(&out_buf, STRING("swbreak:;"));
        }
        else if(IS("g"))
        {
            write_hex((void*)ctx, 16*4);
            write_x(25*4);
            write_hex((void*)&ctx->SPSR, 4);
        }
        else if(STARTSWITH("m"))
        {
            char* p = pkt + 1;
            uintptr_t addr = parse_hex(&p);
            size_t size = parse_hex(&p);
            while(size)
            {
                size_t chk = size;
                if(chk > 64) chk = 64;
                char buf[64];
                size_t copy_sz = safe_memcpy(buf, (void*)addr, chk);
                write_hex(buf, copy_sz);
                if(copy_sz < chk) break;
                addr += chk;
                size -= chk;
            }
        }
        else if(STARTSWITH("G"))
        {
            char* p = pkt + 1;
            read_hex(&p, (void*)ctx, 16*4);
            skip_hex(&p, 25*4);
            read_hex(&p, (void*)&ctx->SPSR, 4);
            buffer_write(&out_buf, "OK", 2);
        }
        else if(STARTSWITH("M"))
        {
            char* p = pkt + 1;
            uintptr_t addr = parse_hex(&p);
            size_t size = parse_hex(&p);
            while(size)
            {
                size_t chk = size;
                if(chk > 64) chk = 64;
                char buf[64];
                read_hex(&p, buf, chk);
                char test[64];
                size_t safe_size = safe_memcpy(test, (void*)addr, chk);
                kuKernelCpuUnrestrictedMemcpy((void*)addr, buf, safe_size);
                kuKernelFlushCaches((void*)addr, safe_size);
                if(safe_size < chk) break;
                addr += chk;
                size -= chk;
            }
            if(size)
                buffer_write(&out_buf, "E0e", 3);
            else
                buffer_write(&out_buf, "OK", 2);
        }
        /* ── Z0: set software breakpoint ────────────────────────── */
        else if(STARTSWITH("Z0,"))
        {
            char* p = pkt + 3;
            uint32_t addr = (uint32_t)parse_hex(&p);
            uint32_t kind = (uint32_t)parse_hex(&p);
            if(kind != 2 && kind != 3 && kind != 4)
                kind = 4; /* default to ARM */
            if(bp_insert(addr, (uint8_t)kind) == 0)
                buffer_write(&out_buf, "OK", 2);
            else
                buffer_write(&out_buf, "E01", 3);
        }
        /* ── z0: remove software breakpoint ─────────────────────── */
        else if(STARTSWITH("z0,"))
        {
            char* p = pkt + 3;
            uint32_t addr = (uint32_t)parse_hex(&p);
            /* kind is parsed but not needed for removal */
            if(bp_remove(addr) == 0)
                buffer_write(&out_buf, "OK", 2);
            else
                buffer_write(&out_buf, "E01", 3);
        }
        /* ── vCont? query ───────────────────────────────────────── */
        else if(IS("vCont?"))
        {
            buffer_write(&out_buf, STRING("vCont;c;s"));
        }
        /* ── vCont;s — single step ──────────────────────────────── */
        else if(IS("vCont;s") || STARTSWITH("vCont;s:"))
        {
            do_single_step(ctx);
            /* Resume execution — on next exception we'll be in the step handler */
            memcpy(pkt, "?#3f", 4);
            out_buf.size--;
            buffer_flush(&out_buf);
            return;
        }
        /* ── vCont;c — continue ─────────────────────────────────── */
        else if(IS("vCont;c") || STARTSWITH("vCont;c:"))
        {
            /* If sitting on a persistent breakpoint, step past it first */
            uint32_t pc = ctx->pc;
            struct sw_breakpoint* bp = bp_find(pc);
            if(bp)
            {
                /* Remember to re-insert after step */
                reinsert_bp_idx = bp - bp_table;
                /* Breakpoint was already restored in exception_handler,
                 * so just do a single step; the re-insert happens on
                 * the next exception entry. */
                do_single_step(ctx);
            }

            memcpy(pkt, "?#3f", 4);
            out_buf.size--;
            buffer_flush(&out_buf);
            return;
        }
        /* ── s: single step (deprecated but simple) ─────────────── */
        else if(IS("s"))
        {
            do_single_step(ctx);
            memcpy(pkt, "?#3f", 4);
            out_buf.size--;
            buffer_flush(&out_buf);
            return;
        }
        /* ── D: detach ──────────────────────────────────────────── */
        else if(IS("D"))
        {
            bp_remove_all();
            step_bp_remove_all();
            buffer_write(&out_buf, "OK", 2);
            discard_packet(pkt, sz);
            send_packet();
            /* Close socket and let the program continue */
            /* We don't have sceNetSyscallClose, so just mark socket invalid */
            uvdb_socket = -1;
            return;
        }
        else if(IS("k"))
        {
            bp_remove_all();
            step_bp_remove_all();
            _sceKernelExitProcessForUser(1);
        }
        else if(IS("c"))
        {
            /* If sitting on a persistent breakpoint, step past it first */
            uint32_t pc = ctx->pc;
            struct sw_breakpoint* bp = bp_find(pc);
            if(bp)
            {
                reinsert_bp_idx = bp - bp_table;
                do_single_step(ctx);
            }

            memcpy(pkt, "?#3f", 4);
            out_buf.size--;
            buffer_flush(&out_buf);
            return;
        }
        else if(STARTSWITH("F"))
        {
            char* p = pkt + 1;
            ctx->r0 = parse_hex(&p);
            memcpy(pkt, "?#3f", 4);
            for(size_t i = 1; i < sz; i++)
                pkt[i+3] = 0;
            out_buf.size--;
            buffer_flush(&out_buf);
            return;
        }
        else if(IS("qOffsets"))
        {
            uint8_t packet[33] = "TextSeg=........;DataSeg=........";
            uint32_t value = (uint32_t)__executable_start;
            for(int i = 0; i < 8; i++)
                packet[15-i] = int2hex((value >> (4*i)) & 15);
            value = (uint32_t)__init_array_start;
            for(int i = 0; i < 8; i++)
                packet[32-i] = int2hex((value >> (4*i)) & 15);
            buffer_write(&out_buf, (const char*)packet, sizeof(packet));
        }

        discard_packet(pkt, sz);
        send_packet();
    }
}

#undef STARTSWITH
#undef IS

/* ── Exception handler ─────────────────────────────────────────────── */

static __attribute__((naked)) void uvdb_trap_pc(void)
{
    asm volatile("udf #0");
}

static void exception_handler(KuKernelExceptionContext* ctx)
{
    int signal = SIGSEGV;
    if(ctx->exceptionType == KU_KERNEL_EXCEPTION_TYPE_UNDEFINED_INSTRUCTION)
        signal = SIGILL;

    uint32_t pc = ctx->pc;
    if((ctx->SPSR & 32))
    {
        ctx->SPSR &= ~32u;
        pc |= 1;
    }
    if(pc == (uint32_t)uvdb_trap_pc)
    {
        pc = ctx->r0;
        signal = SIGTRAP;
    }
    if((pc & 1))
    {
        ctx->SPSR |= 32;
        pc &= ~1u;
    }
    ctx->pc = pc;

    last_stop_reason = STOP_SIGNAL;

    /* Check if we hit a temporary (step) breakpoint */
    if(is_step_bp(pc))
    {
        step_bp_remove_all();
        signal = SIGTRAP;

        /* If we were stepping past a persistent BP, re-insert it now */
        if(reinsert_bp_idx >= 0 && reinsert_bp_idx < MAX_BREAKPOINTS)
        {
            struct sw_breakpoint* rbp = &bp_table[reinsert_bp_idx];
            if(!rbp->active)
            {
                /* Re-insert the breakpoint */
                size_t instr_size = (rbp->kind == 2) ? 2 : 4;
                if(rbp->kind == 2)
                {
                    uint16_t bkpt = (uint16_t)THUMB_BKPT_INSTR;
                    kuKernelCpuUnrestrictedMemcpy((void*)rbp->addr, &bkpt, 2);
                }
                else
                {
                    uint32_t bkpt = ARM_BKPT_INSTR;
                    kuKernelCpuUnrestrictedMemcpy((void*)rbp->addr, &bkpt, 4);
                }
                kuKernelFlushCaches((void*)rbp->addr, instr_size);
                rbp->active = 1;
            }
            reinsert_bp_idx = -1;

            /* This was a continue-past-breakpoint step.
             * Don't enter the main loop — just resume execution. */
            return;
        }

        last_stop_reason = STOP_STEP;
        reinsert_bp_idx = -1;
    }
    /* Check if we hit a persistent (user-set) breakpoint */
    else if(bp_find(pc))
    {
        /* Restore the original instruction so single-step can execute it */
        struct sw_breakpoint* bp = bp_find(pc);
        size_t instr_size = (bp->kind == 2) ? 2 : 4;
        kuKernelCpuUnrestrictedMemcpy((void*)pc, &bp->orig_instr, instr_size);
        kuKernelFlushCaches((void*)pc, instr_size);
        bp->active = 0; /* mark inactive; will be re-inserted on continue */

        signal = SIGTRAP;
        last_stop_reason = STOP_SWBREAK;
    }

    uvdb_lock();
    uvdb_main_loop(ctx, signal);
    uvdb_unlock();
}

/* ── Remote syscall ────────────────────────────────────────────────── */

int uvdb_remote_syscall(const char* name, int nargs, ...)
{
    KuKernelExceptionContext ctx = {};
    uvdb_lock();
    if(uvdb_socket < 0)
    {
        uvdb_unlock();
        uvdb_enter();
        uvdb_lock();
    }
    char* pkt;
    size_t sz = recv_packet(&pkt);
    discard_packet(pkt, sz);
    buffer_start_packet(&out_buf);
    buffer_write(&out_buf, "F", 1);
    buffer_write(&out_buf, name, strlen(name));
    va_list va;
    va_start(va, nargs);
    for(int i = 0; i < nargs; i++)
    {
        uintptr_t value = va_arg(va, uintptr_t);
        char packet[9] = ",";
        for(int j = 0; j < 8; j++)
            packet[8-j] = int2hex((value >> (4*j)) & 15);
        buffer_write(&out_buf, packet, 9);
    }
    va_end(va);
    send_packet();
    uvdb_main_loop(&ctx, 0);
    uvdb_unlock();
    return ctx.r0;
}

/* ── Entry point ───────────────────────────────────────────────────── */

static __attribute__((used)) uint64_t real_uvdb_enter(uintptr_t lr)
{
    uint64_t no_trap = (uint64_t)lr << 32 | lr;
    uint64_t trap = (uint64_t)(uint32_t)uvdb_trap_pc << 32 | lr;
    uvdb_lock();
    if(uvdb_socket >= 0)
    {
        uvdb_unlock();
        return trap;
    }
    if(uvdb_pipe < 0)
    {
        uvdb_pipe = sceKernelCreateMsgPipe("pipe to catch efault", 0x40,
            0xc, 4*4096, NULL);
        if(uvdb_pipe < 0)
        {
            uvdb_unlock();
            return no_trap;
        }
    }
    struct KuKernelExceptionHandlerOpt opt = {
        .size = sizeof(opt),
    };
    KuKernelExceptionHandler old;
    if(kuKernelRegisterExceptionHandler(KU_KERNEL_EXCEPTION_TYPE_DATA_ABORT,
            exception_handler, &old, &opt)
        || kuKernelRegisterExceptionHandler(KU_KERNEL_EXCEPTION_TYPE_PREFETCH_ABORT,
            exception_handler, &old, &opt)
        || kuKernelRegisterExceptionHandler(KU_KERNEL_EXCEPTION_TYPE_UNDEFINED_INSTRUCTION,
            exception_handler, &old, &opt))
    {
        uvdb_unlock();
        return no_trap;
    }

    int sock = sceNetSyscallSocket("gdb socket", AF_INET, SOCK_STREAM, 0);
    if(sock < 0)
    {
        uvdb_unlock();
        return no_trap;
    }
    int value = 1;
    uint32_t args[5] = {sock, SOL_SOCKET, SO_REUSEADDR, (uint32_t)&value, sizeof(value)};
    if(sceNetSyscallSetsockopt((void*)&args))
    {
        uvdb_unlock();
        return no_trap;
    }
    args[1] = IPPROTO_TCP;
    args[2] = TCP_NODELAY;
    if(sceNetSyscallSetsockopt((void*)&args))
    {
        uvdb_unlock();
        return no_trap;
    }
    int port = 1234;
    struct sockaddr_in sin = {
        .sin_family = AF_INET,
        .sin_addr = {},
        .sin_port = 0,
    };
    while(port < 65536 && (sin.sin_port = htons(port), sceNetSyscallBind(sock, &sin, sizeof(sin))))
        port++;
    if(port == 65536)
    {
        uvdb_unlock();
        return no_trap;
    }
    if(sceNetSyscallListen(sock, 1) || (uvdb_socket = sceNetSyscallAccept(sock, NULL, NULL)) < 0)
    {
        uvdb_unlock();
        return no_trap;
    }
    uvdb_unlock();
    return trap;
}

__attribute__((naked)) void uvdb_enter(void)
{
    asm volatile(
        "mov r0, lr\n"
        "bl real_uvdb_enter\n"
        "bx r1\n"
    );
}
