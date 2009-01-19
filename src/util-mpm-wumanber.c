/* Implementation of the Wu-Manber pattern matching algorithm.
 *
 * Copyright (c) 2008 Victor Julien <victor@inliniac.net>
 *
 * Ideas:
 *   - the hash contains a list of patterns. Maybe we can 'train' the hash
 *     so the most common patterns always appear first in this list.
 *
 * TODO VJ
 *  - make hash1 a array of ptr and get rid of the flag field in the
 *    WmHashItem
 *  - remove exit() calls
 *  - only calc prefixci_buf for nocase patterns? -- would be in a
 *    loop though, so probably not a performance inprovement.
 *  - make sure runtime counters can be disabled (at compile time)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "util-mpm.h"
#include "util-mpm-wumanber.h"

#include "util-unittest.h"

#define INIT_HASH_SIZE 65535

#define HASH16_SIZE 65536
#define HASH16(a,b) (((a)<<8) | (b))
#define HASH15_SIZE 32768
#define HASH15(a,b) (((a)<<7) | (b))
#define HASH14_SIZE 16384
#define HASH14(a,b) (((a)<<6) | (b))
#define HASH12_SIZE 4096
#define HASH12(a,b) (((a)<<4) | (b))
#define HASH9_SIZE 512
#define HASH9(a,b) (((a)<<1) | (b))

void WmInitCtx (MpmCtx *mpm_ctx);
void WmThreadInitCtx(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, u_int32_t);
void WmDestroyCtx(MpmCtx *mpm_ctx);
void WmThreadDestroyCtx(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx);
int WmAddScanPatternCI(MpmCtx *mpm_ctx, u_int8_t *pat, u_int16_t patlen, u_int16_t offset, u_int16_t depth, u_int32_t pid, u_int32_t sid);
int WmAddScanPatternCS(MpmCtx *mpm_ctx, u_int8_t *pat, u_int16_t patlen, u_int16_t offset, u_int16_t depth, u_int32_t pid, u_int32_t sid);
int WmAddPatternCI(MpmCtx *mpm_ctx, u_int8_t *pat, u_int16_t patlen, u_int16_t offset, u_int16_t depth, u_int32_t pid, u_int32_t sid);
int WmAddPatternCS(MpmCtx *mpm_ctx, u_int8_t *pat, u_int16_t patlen, u_int16_t offset, u_int16_t depth, u_int32_t pid, u_int32_t sid);
int WmPreparePatterns(MpmCtx *mpm_ctx);
u_int32_t WmScan1(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *, u_int8_t *buf, u_int16_t buflen);
u_int32_t WmScan2Hash9(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *, u_int8_t *buf, u_int16_t buflen);
u_int32_t WmScan2Hash12(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *, u_int8_t *buf, u_int16_t buflen);
u_int32_t WmScan2Hash14(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *, u_int8_t *buf, u_int16_t buflen);
u_int32_t WmScan2Hash15(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *, u_int8_t *buf, u_int16_t buflen);
u_int32_t WmScan2Hash16(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *, u_int8_t *buf, u_int16_t buflen);
u_int32_t WmSearch1(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *, u_int8_t *buf, u_int16_t buflen);
u_int32_t WmSearch2Hash9(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *, u_int8_t *buf, u_int16_t buflen);
u_int32_t WmSearch2Hash12(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *, u_int8_t *buf, u_int16_t buflen);
u_int32_t WmSearch2Hash14(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *, u_int8_t *buf, u_int16_t buflen);
u_int32_t WmSearch2Hash15(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *, u_int8_t *buf, u_int16_t buflen);
u_int32_t WmSearch2Hash16(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *, u_int8_t *buf, u_int16_t buflen);
void WmPrintInfo(MpmCtx *mpm_ctx);
void WmPrintSearchStats(MpmThreadCtx *mpm_thread_ctx);
void WmRegisterTests(void);

/* uppercase to lowercase conversion lookup table */
static u_int8_t lowercasetable[256];
/* marco to do the actual lookup */
#define wm_tolower(c) lowercasetable[(c)]

#ifdef WUMANBER_COUNTERS
#define COUNT(counter) \
        (counter)
#else
#define COUNT(counter)
#endif /* WUMANBER_COUNTERS */

void MpmWuManberRegister (void) {
    mpm_table[MPM_WUMANBER].name = "wumanber";
    mpm_table[MPM_WUMANBER].InitCtx = WmInitCtx;
    mpm_table[MPM_WUMANBER].InitThreadCtx = WmThreadInitCtx;
    mpm_table[MPM_WUMANBER].DestroyCtx = WmDestroyCtx;
    mpm_table[MPM_WUMANBER].DestroyThreadCtx = WmThreadDestroyCtx;
    mpm_table[MPM_WUMANBER].AddScanPattern = WmAddScanPatternCS;
    mpm_table[MPM_WUMANBER].AddScanPatternNocase = WmAddScanPatternCI;
    mpm_table[MPM_WUMANBER].AddPattern = WmAddPatternCS;
    mpm_table[MPM_WUMANBER].AddPatternNocase = WmAddPatternCI;
    mpm_table[MPM_WUMANBER].Prepare = WmPreparePatterns;
    mpm_table[MPM_WUMANBER].Scan = WmSearch2Hash16; /* default to WmSearch2. We may fall back to 1 */
    mpm_table[MPM_WUMANBER].Search = WmSearch2Hash16; /* default to WmSearch2. We may fall back to 1 */
    mpm_table[MPM_WUMANBER].Cleanup = MpmMatchCleanup;
    mpm_table[MPM_WUMANBER].PrintCtx = WmPrintInfo;
    mpm_table[MPM_WUMANBER].PrintThreadCtx = WmPrintSearchStats;
    mpm_table[MPM_WUMANBER].RegisterUnittests = WmRegisterTests;

    /* create table for O(1) lowercase conversion lookup */
    u_int8_t c = 0;
    for ( ; c < 255; c++) {
       if (c >= 'A' && c <= 'Z')
           lowercasetable[c] = (c + ('a' - 'A'));
       else
           lowercasetable[c] = c;
    }
}

/* append an endmatch to a pattern
 *
 * Only used in the initialization phase */
static inline void WmEndMatchAppend(MpmCtx *mpm_ctx, WmPattern *p,
    u_int16_t offset, u_int16_t depth, u_int32_t pid, u_int32_t sid)
{
    MpmEndMatch *em = MpmAllocEndMatch(mpm_ctx);
    if (em == NULL) {
        printf("ERROR: WmAllocEndMatch failed\n");
        return;
    }

    em->id = pid;
    em->sig_id = sid;
    em->depth = depth;
    em->offset = offset;

    if (p->em == NULL) {
        p->em = em;
        return;
    }

    MpmEndMatch *m = p->em;
    while (m->next) {
        m = m->next;
    }
    m->next = em;
}

void prt (u_int8_t *buf, u_int16_t buflen) {
    u_int16_t i;

    for (i = 0; i < buflen; i++) {
        if (isprint(buf[i])) printf("%c", buf[i]);
        else                 printf("\\x%X", buf[i]);
    }
    //printf("\n");
}

void WmPrintInfo(MpmCtx *mpm_ctx) {
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx->ctx;

    printf("MPM WuManber Information:\n");
    printf("Memory allocs:   %u\n", mpm_ctx->memory_cnt);
    printf("Memory alloced:  %u\n", mpm_ctx->memory_size);
    printf(" Sizeofs:\n");
    printf("  MpmCtx         %u\n", sizeof(MpmCtx));
    printf("  WmCtx:         %u\n", sizeof(WmCtx));
    printf("  WmPattern      %u\n", sizeof(WmPattern));
    printf("  WmHashItem     %u\n", sizeof(WmHashItem));
    printf("Unique Patterns: %u\n", mpm_ctx->pattern_cnt);
    printf("Scan Patterns:   %u\n", mpm_ctx->scan_pattern_cnt);
    printf("Total Patterns:  %u\n", mpm_ctx->total_pattern_cnt);
    printf("Smallest:        %u\n", mpm_ctx->scan_minlen);
    printf("Largest:         %u\n", mpm_ctx->scan_maxlen);
    printf("Max shiftlen:    %u\n", wm_ctx->scan_shiftlen);
    printf("Hash size:       %u\n", wm_ctx->scan_hash_size);
    printf("Scan function: ");
    if (mpm_ctx->Scan == WmScan1) {
        printf("WmScan1 (allows single byte patterns)\n");
        printf("MBScan funct:  ");
        if (wm_ctx->MBScan == WmScan2Hash16) printf("WmSearch2Hash16\n");
        else if (wm_ctx->MBScan == WmScan2Hash15) printf("WmSearch2Hash15\n");
        else if (wm_ctx->MBScan == WmScan2Hash14) printf("WmSearch2Hash14\n");
        else if (wm_ctx->MBScan == WmScan2Hash12) printf("WmSearch2Hash12\n");
        else if (wm_ctx->MBScan == WmScan2Hash9)  printf("WmSearch2Hash9\n");
    }
    if (mpm_ctx->Scan == WmScan2Hash16) printf("WmScan2Hash16 (only for multibyte patterns)\n");
    else if (mpm_ctx->Scan == WmScan2Hash15) printf("WmScan2Hash15 (only for multibyte patterns)\n");
    else if (mpm_ctx->Scan == WmScan2Hash14) printf("WmScan2Hash14 (only for multibyte patterns)\n");
    else if (mpm_ctx->Scan == WmScan2Hash12) printf("WmScan2Hash12 (only for multibyte patterns)\n");
    else if (mpm_ctx->Scan == WmScan2Hash9)  printf("WmScan2Hash9 (only for multibyte patterns)\n");
    else printf("ERROR\n");
    printf("Smallest:        %u\n", mpm_ctx->search_minlen);
    printf("Largest:         %u\n", mpm_ctx->search_maxlen);
    printf("Max shiftlen:    %u\n", wm_ctx->search_shiftlen);
    printf("Hash size:       %u\n", wm_ctx->search_hash_size);
    printf("Search function: ");
    if (mpm_ctx->Search == WmSearch1) {
        printf("WmSearch1 (allows single byte patterns)\n");
        printf("MBSearch funct:  ");
        if (wm_ctx->MBSearch == WmSearch2Hash16) printf("WmSearch2Hash16\n");
        else if (wm_ctx->MBSearch == WmSearch2Hash15) printf("WmSearch2Hash15\n");
        else if (wm_ctx->MBSearch == WmSearch2Hash14) printf("WmSearch2Hash14\n");
        else if (wm_ctx->MBSearch == WmSearch2Hash12) printf("WmSearch2Hash12\n");
        else if (wm_ctx->MBSearch == WmSearch2Hash9)  printf("WmSearch2Hash9\n");
    }
    if (mpm_ctx->Search == WmSearch2Hash16) printf("WmSearch2Hash16 (only for multibyte patterns)\n");
    else if (mpm_ctx->Search == WmSearch2Hash15) printf("WmSearch2Hash15 (only for multibyte patterns)\n");
    else if (mpm_ctx->Search == WmSearch2Hash14) printf("WmSearch2Hash14 (only for multibyte patterns)\n");
    else if (mpm_ctx->Search == WmSearch2Hash12) printf("WmSearch2Hash12 (only for multibyte patterns)\n");
    else if (mpm_ctx->Search == WmSearch2Hash9)  printf("WmSearch2Hash9 (only for multibyte patterns)\n");
    else printf("ERROR\n");
    printf("\n");
}

static inline WmPattern *WmAllocPattern(MpmCtx *mpm_ctx) {
    WmPattern *p = malloc(sizeof(WmPattern));
    if (p == NULL) {
        printf("ERROR: WmAllocPattern: malloc failed\n");
    }
    memset(p,0,sizeof(WmPattern));

    mpm_ctx->memory_cnt++;
    mpm_ctx->memory_size += sizeof(WmPattern);
    return p;
}

static inline WmHashItem *
WmAllocHashItem(MpmCtx *mpm_ctx) {
    WmHashItem *hi = malloc(sizeof(WmHashItem));
    if (hi == NULL) {
        printf("ERROR: WmAllocHashItem: malloc failed\n");
    }
    memset(hi,0,sizeof(WmHashItem));

    mpm_ctx->memory_cnt++;
    mpm_ctx->memory_size += sizeof(WmHashItem);
    return hi;
}

static inline void memcpy_tolower(u_int8_t *d, u_int8_t *s, u_int16_t len) {
    u_int16_t i;
    for (i = 0; i < len; i++) {
        d[i] = wm_tolower(s[i]);
    }
}

/*
 * INIT HASH START
 */
static inline u_int32_t WmInitHash(WmPattern *p) {
    u_int32_t hash = p->len * p->cs[0];
    if (p->len > 1)
        hash += p->cs[1];

    return (hash % INIT_HASH_SIZE);
}

static inline u_int32_t WmInitHashRaw(u_int8_t *pat, u_int16_t patlen) {
    u_int32_t hash = patlen * pat[0];
    if (patlen > 1)
        hash += pat[1];

    return (hash % INIT_HASH_SIZE);
}

static inline int WmInitHashAdd(WmCtx *wm_ctx, WmPattern *p) {
    u_int32_t hash = WmInitHash(p);

    //printf("WmInitHashAdd: %u\n", hash);

    if (wm_ctx->init_hash[hash] == NULL) {
        wm_ctx->init_hash[hash] = p;
        //printf("WmInitHashAdd: hash %u, head %p\n", hash, wm_ctx->init_hash[hash]);
        return 0;
    }

    WmPattern *t = wm_ctx->init_hash[hash], *tt = NULL;
    for ( ; t != NULL; t = t->next) {
        tt = t;
    }
    tt->next = p;
    //printf("WmInitHashAdd: hash %u, head %p\n", hash, wm_ctx->init_hash[hash]);

    return 0;
}

static inline int WmCmpPattern(WmPattern *p, u_int8_t *pat, u_int16_t patlen, char nocase);

static inline WmPattern *WmInitHashLookup(WmCtx *wm_ctx, u_int8_t *pat, u_int16_t patlen, char nocase) {
    u_int32_t hash = WmInitHashRaw(pat,patlen);

    //printf("WmInitHashLookup: %u, head %p\n", hash, wm_ctx->init_hash[hash]);

    if (wm_ctx->init_hash[hash] == NULL) {
        return NULL;
    }

    WmPattern *t = wm_ctx->init_hash[hash];
    for ( ; t != NULL; t = t->next) {
        if (WmCmpPattern(t,pat,patlen,nocase) == 1)
            return t;
    }

    return NULL;
}

static inline int WmCmpPattern(WmPattern *p, u_int8_t *pat, u_int16_t patlen, char nocase) {
    if (p->len != patlen)
        return 0;

    if (!((nocase && p->flags & WUMANBER_NOCASE) || (!nocase && !(p->flags & WUMANBER_NOCASE))))
        return 0;

    if (memcmp(p->cs, pat, patlen) != 0)
        return 0;

    return 1;
}

/*
 * INIT HASH END
 */

void WmFreePattern(MpmCtx *mpm_ctx, WmPattern *p) {
    if (p && p->em) {
        MpmEndMatchFreeAll(mpm_ctx, p->em);
    }

    if (p && p->cs && p->cs != p->ci) {
        free(p->cs);
        mpm_ctx->memory_cnt--;
        mpm_ctx->memory_size -= p->len;
    }

    if (p && p->ci) {
        free(p->ci);
        mpm_ctx->memory_cnt--;
        mpm_ctx->memory_size -= p->len;
    }

    if (p) {
        free(p);
        mpm_ctx->memory_cnt--;
        mpm_ctx->memory_size -= sizeof(WmPattern); 
    }
}

/* WmAddPattern
 *
 * pat: ptr to the pattern
 * patlen: length of the pattern
 * nocase: nocase flag: 1 enabled, 0 disable
 * pid: pattern id
 * sid: signature id (internal id)
 */
static inline int WmAddPattern(MpmCtx *mpm_ctx, u_int8_t *pat, u_int16_t patlen, u_int16_t offset, u_int16_t depth, char nocase, char scan, u_int32_t pid, u_int32_t sid) {
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx->ctx;

//    printf("WmAddPattern: ctx %p \"", mpm_ctx); prt(pat, patlen);
//    printf("\" id %u, nocase %s\n", id, nocase ? "true" : "false");

    if (patlen == 0)
        return 0;

    /* get a memory piece */
    WmPattern *p = WmInitHashLookup(wm_ctx, pat, patlen, nocase);
    if (p == NULL) {
//        printf("WmAddPattern: allocing new pattern\n");
        p = WmAllocPattern(mpm_ctx);
        if (p == NULL)
            goto error;

        p->len = patlen;

        if (nocase) p->flags |= WUMANBER_NOCASE;

        /* setup the case insensitive part of the pattern */
        p->ci = malloc(patlen);
        if (p->ci == NULL) goto error;
        mpm_ctx->memory_cnt++;
        mpm_ctx->memory_size += patlen;
        memcpy_tolower(p->ci, pat, patlen);

        /* setup the case sensitive part of the pattern */
        if (p->flags & WUMANBER_NOCASE) {
            /* nocase means no difference between cs and ci */
            p->cs = p->ci;
        } else {
            if (memcmp(p->ci,pat,p->len) == 0) {
                /* no diff between cs and ci: pat is lowercase */
                p->cs = p->ci;
            } else {
                p->cs = malloc(patlen);
                if (p->cs == NULL) goto error;
                mpm_ctx->memory_cnt++;
                mpm_ctx->memory_size += patlen;
                memcpy(p->cs, pat, patlen);
            }
        }

        if (p->len > 1) {
            p->prefix_cs = (u_int16_t)(*(p->cs)+*(p->cs+1));
            p->prefix_ci = (u_int16_t)(*(p->ci)+*(p->ci+1));
        }

        //printf("WmAddPattern: ci \""); prt(p->ci,p->len);
        //printf("\" cs \""); prt(p->cs,p->len);
        //printf("\" prefix_ci %u, prefix_cs %u\n", p->prefix_ci, p->prefix_cs);

        /* put in the pattern hash */
        WmInitHashAdd(wm_ctx, p);

        if (mpm_ctx->pattern_cnt == 65535) {
            printf("Max search words reached\n");
            exit(1);
        }
        mpm_ctx->pattern_cnt++;

        if (scan) { /* SCAN */
            if (mpm_ctx->scan_maxlen < patlen) mpm_ctx->scan_maxlen = patlen;
            if (mpm_ctx->pattern_cnt == 1) mpm_ctx->scan_minlen = patlen;
            else if (mpm_ctx->scan_minlen > patlen) mpm_ctx->scan_minlen = patlen;
            p->flags |= WUMANBER_SCAN;
        } else { /* SEARCH */
            if (mpm_ctx->search_maxlen < patlen) mpm_ctx->search_maxlen = patlen;
            if (mpm_ctx->pattern_cnt == 1) mpm_ctx->search_minlen = patlen;
            else if (mpm_ctx->search_minlen > patlen) mpm_ctx->search_minlen = patlen;
        }
    }

    /* we need a match */
    WmEndMatchAppend(mpm_ctx, p, offset, depth, pid, sid);

    /* keep track of highest pattern id XXX still used? */
    if (pid > mpm_ctx->max_pattern_id)
        mpm_ctx->max_pattern_id = pid;

    mpm_ctx->total_pattern_cnt++;

    return 0;

error:
    WmFreePattern(mpm_ctx, p);
    return -1;
}

int WmAddScanPatternCI(MpmCtx *mpm_ctx, u_int8_t *pat, u_int16_t patlen,
    u_int16_t offset, u_int16_t depth, u_int32_t pid, u_int32_t sid)
{
    return WmAddPattern(mpm_ctx, pat, patlen, offset, depth, /* nocase */1, /* scan */1, pid, sid);
}

int WmAddScanPatternCS(MpmCtx *mpm_ctx, u_int8_t *pat, u_int16_t patlen,
    u_int16_t offset, u_int16_t depth, u_int32_t pid, u_int32_t sid)
{
    return WmAddPattern(mpm_ctx, pat, patlen, offset, depth, /* nocase */0, /* scan */1, pid, sid);
}

int WmAddPatternCI(MpmCtx *mpm_ctx, u_int8_t *pat, u_int16_t patlen,
    u_int16_t offset, u_int16_t depth, u_int32_t pid, u_int32_t sid)
{
    return WmAddPattern(mpm_ctx, pat, patlen, offset, depth, /* nocase */1, /* scan */0, pid, sid);
}

int WmAddPatternCS(MpmCtx *mpm_ctx, u_int8_t *pat, u_int16_t patlen,
    u_int16_t offset, u_int16_t depth, u_int32_t pid, u_int32_t sid)
{
    return WmAddPattern(mpm_ctx, pat, patlen, offset, depth, /* nocase */0, /* scan */0, pid, sid);
}

static void WmScanPrepareHash(MpmCtx *mpm_ctx) {
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx->ctx;
    u_int16_t i;
    u_int16_t idx = 0;
    u_int8_t idx8 = 0;

    wm_ctx->scan_hash = (WmHashItem **)malloc(sizeof(WmHashItem *) * wm_ctx->scan_hash_size);
    if (wm_ctx->scan_hash == NULL) goto error;
    memset(wm_ctx->scan_hash, 0, sizeof(WmHashItem *) * wm_ctx->scan_hash_size);

    mpm_ctx->memory_cnt++;
    mpm_ctx->memory_size += (sizeof(WmHashItem *) * wm_ctx->scan_hash_size);

    for (i = 0; i < mpm_ctx->pattern_cnt; i++)
    {
        /* ignore patterns that don't have the scan flag set */
        if (!(wm_ctx->parray[i]->flags & WUMANBER_SCAN))
            continue;

        if(wm_ctx->parray[i]->len == 1) {
            idx8 = (u_int8_t)wm_ctx->parray[i]->ci[0];
            if (wm_ctx->scan_hash1[idx8].flags == 0) {
                wm_ctx->scan_hash1[idx8].idx = i;
                wm_ctx->scan_hash1[idx8].flags |= 0x01;
            } else {
                WmHashItem *hi = WmAllocHashItem(mpm_ctx);
                hi->idx = i;
                hi->flags |= 0x01;

                /* Append this HashItem to the list */
                WmHashItem *thi = &wm_ctx->scan_hash1[idx8];
                while (thi->nxt) thi = thi->nxt;
                thi->nxt = hi;
            }
        } else {
            u_int16_t patlen = wm_ctx->scan_shiftlen;

            if (wm_ctx->scan_hash_size == HASH9_SIZE)
                idx = HASH9(wm_ctx->parray[i]->ci[patlen-1], wm_ctx->parray[i]->ci[patlen-2]);
            else if (wm_ctx->scan_hash_size == HASH12_SIZE)
                idx = HASH12(wm_ctx->parray[i]->ci[patlen-1], wm_ctx->parray[i]->ci[patlen-2]);
            else if (wm_ctx->scan_hash_size == HASH14_SIZE)
                idx = HASH14(wm_ctx->parray[i]->ci[patlen-1], wm_ctx->parray[i]->ci[patlen-2]);
            else if (wm_ctx->scan_hash_size == HASH15_SIZE)
                idx = HASH15(wm_ctx->parray[i]->ci[patlen-1], wm_ctx->parray[i]->ci[patlen-2]);
            else
                idx = HASH16(wm_ctx->parray[i]->ci[patlen-1], wm_ctx->parray[i]->ci[patlen-2]);

            if (wm_ctx->scan_hash[idx] == NULL) {
                WmHashItem *hi = WmAllocHashItem(mpm_ctx);
                hi->idx = i;
                hi->flags |= 0x01;

                wm_ctx->scan_hash[idx] = hi;
            } else {
                WmHashItem *hi = WmAllocHashItem(mpm_ctx);
                hi->idx = i;
                hi->flags |= 0x01;

                /* Append this HashItem to the list */
                WmHashItem *thi = wm_ctx->scan_hash[idx];
                while (thi->nxt) thi = thi->nxt;
                thi->nxt = hi;
            }
        }
    }
    return;
error:
    return;
}
static void WmPrepareHash(MpmCtx *mpm_ctx) {
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx->ctx;
    u_int16_t i;
    u_int16_t idx = 0;
    u_int8_t idx8 = 0;

    wm_ctx->search_hash = (WmHashItem **)malloc(sizeof(WmHashItem *) * wm_ctx->search_hash_size);
    if (wm_ctx->search_hash == NULL) goto error;
    memset(wm_ctx->search_hash, 0, sizeof(WmHashItem *) * wm_ctx->search_hash_size);

    mpm_ctx->memory_cnt++;
    mpm_ctx->memory_size += (sizeof(WmHashItem *) * wm_ctx->search_hash_size);

    for (i = 0; i < mpm_ctx->pattern_cnt; i++)
    {
        /* ignore patterns that have the scan flag set */
        if (wm_ctx->parray[i]->flags & WUMANBER_SCAN)
            continue;

        if(wm_ctx->parray[i]->len == 1) {
            idx8 = (u_int8_t)wm_ctx->parray[i]->ci[0];
            if (wm_ctx->search_hash1[idx8].flags == 0) {
                wm_ctx->search_hash1[idx8].idx = i;
                wm_ctx->search_hash1[idx8].flags |= 0x01;
            } else {
                WmHashItem *hi = WmAllocHashItem(mpm_ctx);
                hi->idx = i;
                hi->flags |= 0x01;

                /* Append this HashItem to the list */
                WmHashItem *thi = &wm_ctx->search_hash1[idx8];
                while (thi->nxt) thi = thi->nxt;
                thi->nxt = hi;
            }
        } else {
            u_int16_t patlen = wm_ctx->search_shiftlen;

            if (wm_ctx->search_hash_size == HASH9_SIZE)
                idx = HASH9(wm_ctx->parray[i]->ci[patlen-1], wm_ctx->parray[i]->ci[patlen-2]);
            else if (wm_ctx->search_hash_size == HASH12_SIZE)
                idx = HASH12(wm_ctx->parray[i]->ci[patlen-1], wm_ctx->parray[i]->ci[patlen-2]);
            else if (wm_ctx->search_hash_size == HASH14_SIZE)
                idx = HASH14(wm_ctx->parray[i]->ci[patlen-1], wm_ctx->parray[i]->ci[patlen-2]);
            else if (wm_ctx->search_hash_size == HASH15_SIZE)
                idx = HASH15(wm_ctx->parray[i]->ci[patlen-1], wm_ctx->parray[i]->ci[patlen-2]);
            else
                idx = HASH16(wm_ctx->parray[i]->ci[patlen-1], wm_ctx->parray[i]->ci[patlen-2]);

            if (wm_ctx->search_hash[idx] == NULL) {
                WmHashItem *hi = WmAllocHashItem(mpm_ctx);
                hi->idx = i;
                hi->flags |= 0x01;

                wm_ctx->search_hash[idx] = hi;
            } else {
                WmHashItem *hi = WmAllocHashItem(mpm_ctx);
                hi->idx = i;
                hi->flags |= 0x01;

                /* Append this HashItem to the list */
                WmHashItem *thi = wm_ctx->search_hash[idx];
                while (thi->nxt) thi = thi->nxt;
                thi->nxt = hi;
            }
        }
    }
    return;
error:
    return;
}

static void WmScanPrepareShiftTable(MpmCtx *mpm_ctx)
{
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx->ctx;

    u_int16_t shift = 0, k = 0, idx = 0;
    u_int32_t i = 0;

    u_int16_t smallest = mpm_ctx->scan_minlen;
    if (smallest > 255) smallest = 255;
    if (smallest < 2) smallest = 2;

    wm_ctx->scan_shiftlen = smallest;

    wm_ctx->scan_shifttable = malloc(sizeof(u_int16_t) * wm_ctx->scan_hash_size);
    if (wm_ctx->scan_shifttable == NULL)
        return;

    mpm_ctx->memory_cnt++;
    mpm_ctx->memory_size += (sizeof(u_int16_t) * wm_ctx->scan_hash_size);

    /* default shift table is set to minimal shift */
    for (i = 0; i < wm_ctx->scan_hash_size; i++)
        wm_ctx->scan_shifttable[i] = wm_ctx->scan_shiftlen;

    for (i = 0; i < mpm_ctx->pattern_cnt; i++)
    {
        /* ignore one byte patterns */
        if (wm_ctx->parray[i]->len == 1)
            continue;

        /* ignore patterns that don't have the scan flag set */
        if (!(wm_ctx->parray[i]->flags & WUMANBER_SCAN))
            continue;

        //printf("WmPrepareShiftTable: i = %u ", i);
        //prt(wm_ctx->parray[i].ci, wm_ctx->parray[i].len);

        /* add the first character of the pattern preceeded by
         * every possible other character. */
        for (k = 0; k < 256; k++) {
            shift = wm_ctx->scan_shiftlen - 1;
            if (shift > 255) shift = 255;

            if (wm_ctx->scan_hash_size == HASH9_SIZE) {
                idx = HASH9(wm_ctx->parray[i]->ci[0], (u_int8_t)k);
                //printf("HASH9 idx %u\n", idx);
            } else if (wm_ctx->scan_hash_size == HASH12_SIZE) {
                idx = HASH12(wm_ctx->parray[i]->ci[0], (u_int8_t)k);
                //printf("HASH12 idx %u\n", idx);
            } else if (wm_ctx->scan_hash_size == HASH14_SIZE) {
                idx = HASH14(wm_ctx->parray[i]->ci[0], (u_int8_t)k);
                //printf("HASH14 idx %u\n", idx);
            } else if (wm_ctx->scan_hash_size == HASH15_SIZE) {
                idx = HASH15(wm_ctx->parray[i]->ci[0], (u_int8_t)k);
                //printf("HASH15 idx %u\n", idx);
            } else {
                idx = HASH16(wm_ctx->parray[i]->ci[0], (u_int8_t)k);
                //printf("HASH15 idx %u\n", idx);
            }
            if (shift < wm_ctx->scan_shifttable[idx]) {
                wm_ctx->scan_shifttable[idx] = shift;
            }
        }

        for (k = 0; k < wm_ctx->scan_shiftlen-1; k++)
        {
            shift = (wm_ctx->scan_shiftlen - 2 - k);
            if (shift > 255) shift = 255;

            if (wm_ctx->scan_hash_size == HASH9_SIZE) {
                idx = HASH9(wm_ctx->parray[i]->ci[k+1], wm_ctx->parray[i]->ci[k]);
                //printf("HASH9 idx %u\n", idx);
            } else if (wm_ctx->scan_hash_size == HASH12_SIZE) {
                idx = HASH12(wm_ctx->parray[i]->ci[k+1], wm_ctx->parray[i]->ci[k]);
                //printf("HASH12 idx %u\n", idx);
            } else if (wm_ctx->scan_hash_size == HASH14_SIZE) {
                idx = HASH14(wm_ctx->parray[i]->ci[k+1], wm_ctx->parray[i]->ci[k]);
                //printf("HASH14 idx %u\n", idx);
            } else if (wm_ctx->scan_hash_size == HASH15_SIZE) {
                idx = HASH15(wm_ctx->parray[i]->ci[k+1], wm_ctx->parray[i]->ci[k]);
                //printf("HASH15 idx %u\n", idx);
            } else {
                idx = HASH16(wm_ctx->parray[i]->ci[k+1], wm_ctx->parray[i]->ci[k]);
                //printf("HASH15 idx %u\n", idx);
            }
            if (shift < wm_ctx->scan_shifttable[idx]) {
                wm_ctx->scan_shifttable[idx] = shift;
            }
            //printf("WmPrepareShiftTable: i %u, k %u, idx %u, shift set to %u: \"%c%c\"\n",
            //    i, k, idx, shift, wm_ctx->parray[i]->ci[k], wm_ctx->parray[i]->ci[k+1]);
        }
    }
}

static void WmPrepareShiftTable(MpmCtx *mpm_ctx)
{
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx->ctx;

    u_int16_t shift = 0, k = 0, idx = 0;
    u_int32_t i = 0;

    u_int16_t smallest = mpm_ctx->search_minlen;
    if (smallest > 255) smallest = 255;
    if (smallest < 2) smallest = 2;

    wm_ctx->search_shiftlen = smallest;

    wm_ctx->search_shifttable = malloc(sizeof(u_int16_t) * wm_ctx->search_hash_size);
    if (wm_ctx->search_shifttable == NULL)
        return;

    mpm_ctx->memory_cnt++;
    mpm_ctx->memory_size += (sizeof(u_int16_t) * wm_ctx->search_hash_size);

    /* default shift table is set to minimal shift */
    for (i = 0; i < wm_ctx->search_hash_size; i++)
        wm_ctx->search_shifttable[i] = wm_ctx->search_shiftlen;

    for (i = 0; i < mpm_ctx->pattern_cnt; i++)
    {
        /* ignore one byte patterns */
        if (wm_ctx->parray[i]->len == 1)
            continue;

        /* ignore patterns that have the scan flag set */
        if (wm_ctx->parray[i]->flags & WUMANBER_SCAN)
            continue;

        //printf("WmPrepareShiftTable: i = %u ", i);
        //prt(wm_ctx->parray[i].ci, wm_ctx->parray[i].len);

        /* add the first character of the pattern preceeded by
         * every possible other character. */
        for (k = 0; k < 256; k++) {
            shift = wm_ctx->search_shiftlen - 1;
            if (shift > 255) shift = 255;

            if (wm_ctx->search_hash_size == HASH9_SIZE) {
                idx = HASH9(wm_ctx->parray[i]->ci[0], (u_int8_t)k);
                //printf("HASH9 idx %u\n", idx);
            } else if (wm_ctx->search_hash_size == HASH12_SIZE) {
                idx = HASH12(wm_ctx->parray[i]->ci[0], (u_int8_t)k);
                //printf("HASH12 idx %u\n", idx);
            } else if (wm_ctx->search_hash_size == HASH14_SIZE) {
                idx = HASH14(wm_ctx->parray[i]->ci[0], (u_int8_t)k);
                //printf("HASH14 idx %u\n", idx);
            } else if (wm_ctx->search_hash_size == HASH15_SIZE) {
                idx = HASH15(wm_ctx->parray[i]->ci[0], (u_int8_t)k);
                //printf("HASH15 idx %u\n", idx);
            } else {
                idx = HASH16(wm_ctx->parray[i]->ci[0], (u_int8_t)k);
                //printf("HASH15 idx %u\n", idx);
            }
            if (shift < wm_ctx->search_shifttable[idx]) {
                wm_ctx->search_shifttable[idx] = shift;
            }
        }

        for (k = 0; k < wm_ctx->search_shiftlen - 1; k++)
        {
            shift = (wm_ctx->search_shiftlen - 2 - k);
            if (shift > 255) shift = 255;

            if (wm_ctx->search_hash_size == HASH9_SIZE) {
                idx = HASH9(wm_ctx->parray[i]->ci[k+1], wm_ctx->parray[i]->ci[k]);
                //printf("HASH9 idx %u\n", idx);
            } else if (wm_ctx->search_hash_size == HASH12_SIZE) {
                idx = HASH12(wm_ctx->parray[i]->ci[k+1], wm_ctx->parray[i]->ci[k]);
                //printf("HASH12 idx %u\n", idx);
            } else if (wm_ctx->search_hash_size == HASH14_SIZE) {
                idx = HASH14(wm_ctx->parray[i]->ci[k+1], wm_ctx->parray[i]->ci[k]);
                //printf("HASH14 idx %u\n", idx);
            } else if (wm_ctx->search_hash_size == HASH15_SIZE) {
                idx = HASH15(wm_ctx->parray[i]->ci[k+1], wm_ctx->parray[i]->ci[k]);
                //printf("HASH15 idx %u\n", idx);
            } else {
                idx = HASH16(wm_ctx->parray[i]->ci[k+1], wm_ctx->parray[i]->ci[k]);
                //printf("HASH15 idx %u\n", idx);
            }
            if (shift < wm_ctx->search_shifttable[idx]) {
                wm_ctx->search_shifttable[idx] = shift;
            }
            //printf("WmPrepareShiftTable: i %u, k %u, idx %u, shift set to %u: \"%c%c\"\n",
            //    i, k, idx, shift, wm_ctx->parray[i]->ci[k], wm_ctx->parray[i]->ci[k+1]);
        }
    }
}

int WmPreparePatterns(MpmCtx *mpm_ctx) {
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx->ctx;

    /* alloc the pattern array */
    wm_ctx->parray = (WmPattern **)malloc(mpm_ctx->pattern_cnt * sizeof(WmPattern *));
    if (wm_ctx->parray == NULL) goto error;
    memset(wm_ctx->parray, 0, mpm_ctx->pattern_cnt * sizeof(WmPattern *));
    //printf("mpm_ctx %p, parray %p\n", mpm_ctx,wm_ctx->parray);
    mpm_ctx->memory_cnt++;
    mpm_ctx->memory_size += (mpm_ctx->pattern_cnt * sizeof(WmPattern *));

    /* populate it with the patterns in the hash */
    u_int32_t i = 0, p = 0;
    for (i = 0; i < INIT_HASH_SIZE; i++) {
        WmPattern *node = wm_ctx->init_hash[i], *nnode = NULL;
        for ( ; node != NULL; ) {
            nnode = node->next;
            node->next = NULL;

            wm_ctx->parray[p] = node;

            p++;
            node = nnode;
        }
    }
    /* we no longer need the hash, so free it's memory */
    free(wm_ctx->init_hash);
    wm_ctx->init_hash = NULL;

    /* TODO VJ these values are chosen pretty much randomly, so
     * we should do some performance testing
     * */

    /* scan */
    if (wm_ctx->scan_hash_size == 0) {
        if (mpm_ctx->scan_pattern_cnt < 50) {
            wm_ctx->scan_hash_size = HASH9_SIZE;
        } else if(mpm_ctx->scan_pattern_cnt < 300) {
            wm_ctx->scan_hash_size = HASH12_SIZE;
        } else if(mpm_ctx->scan_pattern_cnt < 1200) {
            wm_ctx->scan_hash_size = HASH14_SIZE;
        } else if(mpm_ctx->scan_pattern_cnt < 2400) {
            wm_ctx->scan_hash_size = HASH15_SIZE;
        } else {
            wm_ctx->scan_hash_size = HASH16_SIZE;
        }
    }

    WmScanPrepareShiftTable(mpm_ctx);
    WmScanPrepareHash(mpm_ctx);

    if (wm_ctx->scan_hash_size == HASH9_SIZE) {
        wm_ctx->MBScan = WmScan2Hash9;
        mpm_ctx->Scan = WmScan2Hash9;
    } else if (wm_ctx->scan_hash_size == HASH12_SIZE) {
        wm_ctx->MBScan = WmScan2Hash12;
        mpm_ctx->Scan = WmScan2Hash12;
    } else if (wm_ctx->scan_hash_size == HASH14_SIZE) {
        wm_ctx->MBScan = WmScan2Hash14;
        mpm_ctx->Scan = WmScan2Hash14;
    } else if (wm_ctx->scan_hash_size == HASH15_SIZE) {
        wm_ctx->MBScan = WmScan2Hash15;
        mpm_ctx->Scan = WmScan2Hash15;
    } else {
        wm_ctx->MBScan = WmScan2Hash16;
        mpm_ctx->Scan = WmScan2Hash16;
    }

    if (mpm_ctx->scan_minlen == 1) {
        mpm_ctx->Scan = WmScan1;
    }

    /* search XXX use search only pat cnt*/
    if (wm_ctx->search_hash_size == 0) {
        if (mpm_ctx->pattern_cnt < 50) {
            wm_ctx->search_hash_size = HASH9_SIZE;
        } else if(mpm_ctx->pattern_cnt < 300) {
            wm_ctx->search_hash_size = HASH12_SIZE;
        } else if(mpm_ctx->pattern_cnt < 1200) {
            wm_ctx->search_hash_size = HASH14_SIZE;
        } else if(mpm_ctx->pattern_cnt < 2400) {
            wm_ctx->search_hash_size = HASH15_SIZE;
        } else {
            wm_ctx->search_hash_size = HASH16_SIZE;
        }
    }

    WmPrepareShiftTable(mpm_ctx);
    WmPrepareHash(mpm_ctx);

    if (wm_ctx->search_hash_size == HASH9_SIZE) {
        wm_ctx->MBSearch = WmSearch2Hash9;
        mpm_ctx->Search = WmSearch2Hash9;
    } else if (wm_ctx->search_hash_size == HASH12_SIZE) {
        wm_ctx->MBSearch = WmSearch2Hash12;
        mpm_ctx->Search = WmSearch2Hash12;
    } else if (wm_ctx->search_hash_size == HASH14_SIZE) {
        wm_ctx->MBSearch = WmSearch2Hash14;
        mpm_ctx->Search = WmSearch2Hash14;
    } else if (wm_ctx->search_hash_size == HASH15_SIZE) {
        wm_ctx->MBSearch = WmSearch2Hash15;
        mpm_ctx->Search = WmSearch2Hash15;
    } else {
        wm_ctx->MBSearch = WmSearch2Hash16;
        mpm_ctx->Search = WmSearch2Hash16;
    }

    if (mpm_ctx->search_minlen == 1) {
        mpm_ctx->Search = WmSearch1;
    }

    return 0;
error:
    return -1;
}

void WmPrintSearchStats(MpmThreadCtx *mpm_thread_ctx) {
#ifdef WUMANBER_COUNTERS
    WmThreadCtx *wm_thread_ctx = (WmThreadCtx *)mpm_thread_ctx->ctx;

    printf("Shift 0: %u\n", wm_thread_ctx->scan_stat_shift_null);
    printf("Loop match: %u\n", wm_thread_ctx->scan_stat_loop_match);
    printf("Loop no match: %u\n", wm_thread_ctx->scan_stat_loop_no_match);
    printf("Num shifts: %u\n", wm_thread_ctx->scan_stat_num_shift);
    printf("Total shifts: %u\n", wm_thread_ctx->scan_stat_total_shift);

    printf("Shift 0: %u\n", wm_thread_ctx->search_stat_shift_null);
    printf("Loop match: %u\n", wm_thread_ctx->search_stat_loop_match);
    printf("Loop no match: %u\n", wm_thread_ctx->search_stat_loop_no_match);
    printf("Num shifts: %u\n", wm_thread_ctx->search_stat_num_shift);
    printf("Total shifts: %u\n", wm_thread_ctx->search_stat_total_shift);
#endif /* WUMANBER_COUNTERS */
}

static inline int
memcmp_lowercase(u_int8_t *s1, u_int8_t *s2, u_int16_t n) {
    size_t i;

    /* check backwards because we already tested the first
     * 2 to 4 chars. This way we are more likely to detect
     * a miss and thus speed up a little... */
    for (i = n - 1; i; i--) {
        if (wm_tolower(*(s2+i)) != s1[i])
            return 1;
    }

    return 0;
}

/* SCAN FUNCTIONS */
u_int32_t WmScan2Hash9(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *pmq, u_int8_t *buf, u_int16_t buflen) {
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx->ctx;
#ifdef WUMANBER_COUNTERS
    WmThreadCtx *wm_thread_ctx = (WmThreadCtx *)mpm_thread_ctx->ctx;
#endif /* WUMANBER_COUNTERS */
    u_int32_t cnt = 0;
    u_int8_t *bufmin = buf;
    u_int8_t *bufend = buf + buflen - 1;
    u_int16_t sl = wm_ctx->scan_shiftlen;
    u_int16_t h;
    u_int8_t shift;
    WmHashItem *thi, *hi;
    WmPattern *p;
    u_int16_t prefixci_buf;
    u_int16_t prefixcs_buf;

    if (buflen == 0)
        return 0;

    //printf("BUF(%u) ", buflen); prt(buf,buflen); printf(" (sl %u)\n", sl);

    buf+=(sl-1);

    while (buf <= bufend) {
        h = HASH9(wm_tolower(*buf),(wm_tolower(*(buf-1))));
        shift = wm_ctx->scan_shifttable[h];
        //printf("%p %u search: h %u, shift %u\n", buf, buf - bufmin, h, shift);

        if (shift == 0) {
            COUNT(wm_thread_ctx->scan_stat_shift_null++);
            /* get our hash item */
            hi = wm_ctx->scan_hash[h];
            if (hi != NULL) {
                //printf("buf-sl+1 %p, buf-sl+2 %p\n", buf-sl+1, buf-sl+2);
                prefixci_buf = (u_int16_t)(wm_tolower(*(buf-sl+1)) + wm_tolower(*(buf-sl+2)));
                prefixcs_buf = (u_int16_t)(*(buf-sl+1) + *(buf-sl+2));
                //printf("WmSearch2: prefixci_buf %u, prefixcs_buf %u\n", prefixci_buf, prefixcs_buf);
                for (thi = hi; thi != NULL; thi = thi->nxt) {
                    p = wm_ctx->parray[thi->idx];

                    //printf("WmSearch2: p->prefix_ci %u, p->prefix_cs %u\n",
                    //    p->prefix_ci, p->prefix_cs);

                    if (p->flags & WUMANBER_NOCASE) {
                        if (p->prefix_ci != prefixci_buf || p->len > (bufend-(buf-sl)))
                            continue;

                        if (memcmp_lowercase(p->ci, buf-sl+1, p->len) == 0) {
                            //printf("CI Exact match: "); prt(p->ci, p->len); printf("\n");
                            COUNT(wm_thread_ctx->scan_stat_loop_match++);

                            MpmEndMatch *em; 
                            for (em = p->em; em; em = em->next) {
                                //printf("em %p id %u\n", em, em->id);
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf-sl+1 - bufmin), p->len))
                                    cnt++;
                            }

                        } else {
                            COUNT(wm_thread_ctx->scan_stat_loop_no_match++);
                        }
                    } else {
                        if (p->prefix_cs != prefixcs_buf || p->len > (bufend-(buf-sl)))
                            continue;
                        if (memcmp(p->cs, buf-sl+1, p->len) == 0) {
                            //printf("CS Exact match: "); prt(p->cs, p->len); printf("\n");
                            COUNT(wm_thread_ctx->scan_stat_loop_match++);

                            MpmEndMatch *em; 
                            for (em = p->em; em; em = em->next) {
                                //printf("em %p id %u\n", em, em->id);
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf-sl+1 - bufmin), p->len))
                                    cnt++;
                            }

                        } else {
                            COUNT(wm_thread_ctx->scan_stat_loop_no_match++);
                        }
                    }
                }
            }
            shift = 1;
        } else {
            COUNT(wm_thread_ctx->scan_stat_total_shift += shift);
            COUNT(wm_thread_ctx->scan_stat_num_shift++);
        }
        buf += shift;
    }

    //printf("cnt %u\n", cnt);
    return cnt;
}

u_int32_t WmScan2Hash12(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *pmq, u_int8_t *buf, u_int16_t buflen) {
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx->ctx;
#ifdef WUMANBER_COUNTERS
    WmThreadCtx *wm_thread_ctx = (WmThreadCtx *)mpm_thread_ctx->ctx;
#endif /* WUMANBER_COUNTERS */
    u_int32_t cnt = 0;
    u_int8_t *bufmin = buf;
    u_int8_t *bufend = buf + buflen - 1;
    u_int16_t sl = wm_ctx->scan_shiftlen;
    u_int16_t h;
    u_int8_t shift;
    WmHashItem *thi, *hi;
    WmPattern *p;
    u_int16_t prefixci_buf;
    u_int16_t prefixcs_buf;

    if (buflen == 0)
        return 0;

    //printf("BUF(%u) ", buflen); prt(buf,buflen); printf("\n");

    buf+=(sl-1);
    //buf++;

    while (buf <= bufend) {
        //h = (wm_tolower(*buf)<<8)+(wm_tolower(*(buf-1)));
        h = HASH12(wm_tolower(*buf),(wm_tolower(*(buf-1))));
        shift = wm_ctx->scan_shifttable[h];
        //printf("search: h %u, shift %u\n", h, shift);

        if (shift == 0) {
            COUNT(wm_thread_ctx->scan_stat_shift_null++);
            /* get our hash item */
            hi = wm_ctx->scan_hash[h];
            //printf("search: hi %p\n", hi);
            if (hi != NULL) {
                prefixci_buf = (u_int16_t)(wm_tolower(*(buf-sl+1)) + wm_tolower(*(buf-sl+2)));
                prefixcs_buf = (u_int16_t)(*(buf-sl+1) + *(buf-sl+2));
                //printf("WmSearch2: prefixci_buf %u, prefixcs_buf %u\n", prefixci_buf, prefixcs_buf);
                for (thi = hi; thi != NULL; thi = thi->nxt) {
                    p = wm_ctx->parray[thi->idx];

                    //printf("WmSearch2: p->prefix_ci %u, p->prefix_cs %u\n",
                    //    p->prefix_ci, p->prefix_cs);

                    if (p->flags & WUMANBER_NOCASE) {
                        if (p->prefix_ci != prefixci_buf || p->len > (bufend-(buf-sl)))
                            continue;

                        if (memcmp_lowercase(p->ci, buf-sl+1, p->len) == 0) {
                            //printf("CI Exact match: "); prt(p->ci, p->len); printf("\n");
                            COUNT(wm_thread_ctx->scan_stat_loop_match++);

                            MpmEndMatch *em; 
                            for (em = p->em; em; em = em->next) {
                                //printf("em %p id %u\n", em, em->id);
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf-sl+1 - bufmin), p->len))
                                    cnt++;
                            }

                        } else {
                            COUNT(wm_thread_ctx->scan_stat_loop_no_match++);
                        }
                    } else {
                        if (p->prefix_cs != prefixcs_buf || p->len > (bufend-(buf-sl)))
                            continue;
                        if (memcmp(p->cs, buf-sl+1, p->len) == 0) {
                            //printf("CS Exact match: "); prt(p->cs, p->len); printf("\n");
                            COUNT(wm_thread_ctx->scan_stat_loop_match++);

                            MpmEndMatch *em; 
                            for (em = p->em; em; em = em->next) {
                                //printf("em %p id %u\n", em, em->id);
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf-sl+1 - bufmin), p->len))
                                    cnt++;
                            }

                        } else {
                            COUNT(wm_thread_ctx->scan_stat_loop_no_match++);
                        }
                    }
                }
            }
            shift = 1;
        } else {
            COUNT(wm_thread_ctx->scan_stat_total_shift += shift);
            COUNT(wm_thread_ctx->scan_stat_num_shift++);
        }
        buf += shift;
    }

    return cnt;
}

u_int32_t WmScan2Hash14(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *pmq, u_int8_t *buf, u_int16_t buflen) {
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx->ctx;
#ifdef WUMANBER_COUNTERS
    WmThreadCtx *wm_thread_ctx = (WmThreadCtx *)mpm_thread_ctx->ctx;
#endif /* WUMANBER_COUNTERS */
    u_int32_t cnt = 0;
    u_int8_t *bufmin = buf;
    u_int8_t *bufend = buf + buflen - 1;
    u_int16_t sl = wm_ctx->scan_shiftlen;
    u_int16_t h;
    u_int8_t shift;
    WmHashItem *thi, *hi;
    WmPattern *p;
    u_int16_t prefixci_buf;
    u_int16_t prefixcs_buf;

    if (buflen == 0)
        return 0;

    //printf("BUF(%u) ", buflen); prt(buf,buflen); printf("\n");

    buf+=(sl-1);
    //buf++;

    while (buf <= bufend) {
        //h = (wm_tolower(*buf)<<8)+(wm_tolower(*(buf-1)));
        h = HASH14(wm_tolower(*buf),(wm_tolower(*(buf-1))));
        shift = wm_ctx->scan_shifttable[h];
        //printf("search: h %u, shift %u\n", h, shift);

        if (shift == 0) {
            COUNT(wm_thread_ctx->scan_stat_shift_null++);
            /* get our hash item */
            hi = wm_ctx->scan_hash[h];
            //printf("search: hi %p\n", hi);
            if (hi != NULL) {
                prefixci_buf = (u_int16_t)(wm_tolower(*(buf-sl+1)) + wm_tolower(*(buf-sl+2)));
                prefixcs_buf = (u_int16_t)(*(buf-sl+1) + *(buf-sl+2));
                //printf("WmSearch2: prefixci_buf %u, prefixcs_buf %u\n", prefixci_buf, prefixcs_buf);
                for (thi = hi; thi != NULL; thi = thi->nxt) {
                    p = wm_ctx->parray[thi->idx];

                    //printf("WmSearch2: p->prefix_ci %u, p->prefix_cs %u\n",
                    //    p->prefix_ci, p->prefix_cs);

                    if (p->flags & WUMANBER_NOCASE) {
                        if (p->prefix_ci != prefixci_buf || p->len > (bufend-(buf-sl)))
                            continue;

                        if (memcmp_lowercase(p->ci, buf-sl+1, p->len) == 0) {
                            //printf("CI Exact match: "); prt(p->ci, p->len); printf("\n");
                            COUNT(wm_thread_ctx->scan_stat_loop_match++);

                            MpmEndMatch *em; 
                            for (em = p->em; em; em = em->next) {
                                //printf("em %p id %u\n", em, em->id);
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf-sl+1 - bufmin), p->len))
                                    cnt++;
                            }

                        } else {
                            COUNT(wm_thread_ctx->scan_stat_loop_no_match++);
                        }
                    } else {
                        if (p->prefix_cs != prefixcs_buf || p->len > (bufend-(buf-sl)))
                            continue;
                        if (memcmp(p->cs, buf-sl+1, p->len) == 0) {
                            //printf("CS Exact match: "); prt(p->cs, p->len); printf("\n");
                            COUNT(wm_thread_ctx->scan_stat_loop_match++);

                            MpmEndMatch *em; 
                            for (em = p->em; em; em = em->next) {
                                //printf("em %p id %u\n", em, em->id);
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf-sl+1 - bufmin), p->len))
                                    cnt++;
                            }

                        } else {
                            COUNT(wm_thread_ctx->scan_stat_loop_no_match++);
                        }
                    }
                }
            }
            shift = 1;
        } else {
            COUNT(wm_thread_ctx->scan_stat_total_shift += shift);
            COUNT(wm_thread_ctx->scan_stat_num_shift++);
        }
        buf += shift;
    }

    return cnt;
}

u_int32_t WmScan2Hash15(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *pmq, u_int8_t *buf, u_int16_t buflen) {
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx->ctx;
#ifdef WUMANBER_COUNTERS
    WmThreadCtx *wm_thread_ctx = (WmThreadCtx *)mpm_thread_ctx->ctx;
#endif /* WUMANBER_COUNTERS */
    u_int32_t cnt = 0;
    u_int8_t *bufmin = buf;
    u_int8_t *bufend = buf + buflen - 1;
    u_int16_t sl = wm_ctx->scan_shiftlen;
    u_int16_t h;
    u_int8_t shift;
    WmHashItem *thi, *hi;
    WmPattern *p;
    u_int16_t prefixci_buf;
    u_int16_t prefixcs_buf;

    if (buflen == 0)
        return 0;

    //printf("BUF(%u) ", buflen); prt(buf,buflen); printf("\n");

    buf+=(sl-1);
    //buf++;

    while (buf <= bufend) {
        //h = (wm_tolower(*buf)<<8)+(wm_tolower(*(buf-1)));
        h = HASH15(wm_tolower(*buf),(wm_tolower(*(buf-1))));
        shift = wm_ctx->scan_shifttable[h];
        //printf("search: h %u, shift %u\n", h, shift);

        if (shift == 0) {
            COUNT(wm_thread_ctx->scan_stat_shift_null++);
            /* get our hash item */
            hi = wm_ctx->scan_hash[h];
            //printf("search: hi %p\n", hi);
            if (hi != NULL) {
                prefixci_buf = (u_int16_t)(wm_tolower(*(buf-sl+1)) + wm_tolower(*(buf-sl+2)));
                prefixcs_buf = (u_int16_t)(*(buf-sl+1) + *(buf-sl+2));
                //printf("WmSearch2: prefixci_buf %u, prefixcs_buf %u\n", prefixci_buf, prefixcs_buf);
                for (thi = hi; thi != NULL; thi = thi->nxt) {
                    p = wm_ctx->parray[thi->idx];

                    //printf("WmSearch2: p->prefix_ci %u, p->prefix_cs %u\n",
                    //    p->prefix_ci, p->prefix_cs);

                    if (p->flags & WUMANBER_NOCASE) {
                        if (p->prefix_ci != prefixci_buf || p->len > (bufend-(buf-sl)))
                            continue;

                        if (memcmp_lowercase(p->ci, buf-sl+1, p->len) == 0) {
                            //printf("CI Exact match: "); prt(p->ci, p->len); printf("\n");
                            COUNT(wm_thread_ctx->scan_stat_loop_match++);

                            MpmEndMatch *em; 
                            for (em = p->em; em; em = em->next) {
                                //printf("em %p id %u\n", em, em->id);
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf-sl+1 - bufmin), p->len))
                                    cnt++;
                            }

                        } else {
                            COUNT(wm_thread_ctx->scan_stat_loop_no_match++);
                        }
                    } else {
                        if (p->prefix_cs != prefixcs_buf || p->len > (bufend-(buf-sl)))
                            continue;
                        if (memcmp(p->cs, buf-sl+1, p->len) == 0) {
                            //printf("CS Exact match: "); prt(p->cs, p->len); printf("\n");
                            COUNT(wm_thread_ctx->scan_stat_loop_match++);

                            MpmEndMatch *em; 
                            for (em = p->em; em; em = em->next) {
                                //printf("em %p id %u\n", em, em->id);
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf-sl+1 - bufmin), p->len))
                                    cnt++;
                            }

                        } else {
                            COUNT(wm_thread_ctx->scan_stat_loop_no_match++);
                        }
                    }
                }
            }
            shift = 1;
        } else {
            COUNT(wm_thread_ctx->scan_stat_total_shift += shift);
            COUNT(wm_thread_ctx->scan_stat_num_shift++);
        }
        buf += shift;
    }

    return cnt;
}

u_int32_t WmScan2Hash16(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *pmq, u_int8_t *buf, u_int16_t buflen) {
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx->ctx;
#ifdef WUMANBER_COUNTERS
    WmThreadCtx *wm_thread_ctx = (WmThreadCtx *)mpm_thread_ctx->ctx;
#endif /* WUMANBER_COUNTERS */
    u_int32_t cnt = 0;
    u_int8_t *bufmin = buf;
    u_int8_t *bufend = buf + buflen - 1;
    u_int16_t sl = wm_ctx->scan_shiftlen;
    u_int16_t h;
    u_int8_t shift;
    WmHashItem *thi, *hi;
    WmPattern *p;
    u_int16_t prefixci_buf;
    u_int16_t prefixcs_buf;

    if (buflen == 0)
        return 0;

    //printf("BUF(%u) ", buflen); prt(buf,buflen); printf("\n");

    buf+=(sl-1);
    //buf++;

    while (buf <= bufend) {
        //h = (wm_tolower(*buf)<<8)+(wm_tolower(*(buf-1)));
        h = HASH16(wm_tolower(*buf),(wm_tolower(*(buf-1))));
        shift = wm_ctx->scan_shifttable[h];
        //printf("search: h %u, shift %u\n", h, shift);

        if (shift == 0) {
            COUNT(wm_thread_ctx->scan_stat_shift_null++);
            /* get our hash item */
            hi = wm_ctx->scan_hash[h];
            //printf("search: hi %p\n", hi);
            if (hi != NULL) {
                prefixci_buf = (u_int16_t)(wm_tolower(*(buf-sl+1)) + wm_tolower(*(buf-sl+2)));
                prefixcs_buf = (u_int16_t)(*(buf-sl+1) + *(buf-sl+2));
                //printf("WmSearch2: prefixci_buf %u, prefixcs_buf %u\n", prefixci_buf, prefixcs_buf);
                for (thi = hi; thi != NULL; thi = thi->nxt) {
                    p = wm_ctx->parray[thi->idx];

                    //printf("WmSearch2: p->prefix_ci %u, p->prefix_cs %u\n",
                    //    p->prefix_ci, p->prefix_cs);

                    if (p->flags & WUMANBER_NOCASE) {
                        if (p->prefix_ci != prefixci_buf || p->len > (bufend-(buf-sl)))
                            continue;

                        if (memcmp_lowercase(p->ci, buf-sl+1, p->len) == 0) {
                            //printf("CI Exact match: "); prt(p->ci, p->len); printf("\n");
                            COUNT(wm_thread_ctx->scan_stat_loop_match++);

                            MpmEndMatch *em; 
                            for (em = p->em; em; em = em->next) {
                                //printf("em %p id %u\n", em, em->id);
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf-sl+1 - bufmin), p->len))
                                    cnt++;
                            }

                        } else {
                            COUNT(wm_thread_ctx->scan_stat_loop_no_match++);
                        }
                    } else {
                        if (p->prefix_cs != prefixcs_buf || p->len > (bufend-(buf-sl)))
                            continue;
                        if (memcmp(p->cs, buf-sl+1, p->len) == 0) {
                            //printf("CS Exact match: "); prt(p->cs, p->len); printf("\n");
                            COUNT(wm_thread_ctx->scan_stat_loop_match++);

                            MpmEndMatch *em; 
                            for (em = p->em; em; em = em->next) {
                                //printf("em %p id %u\n", em, em->id);
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf-sl+1 - bufmin), p->len))
                                    cnt++;
                            }

                        } else {
                            COUNT(wm_thread_ctx->scan_stat_loop_no_match++);
                        }
                    }
                }
            }
            shift = 1;
        } else {
            COUNT(wm_thread_ctx->scan_stat_total_shift += shift);
            COUNT(wm_thread_ctx->scan_stat_num_shift++);
        }
        buf += shift;
    }

    return cnt;
}

u_int32_t WmScan1(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *pmq, u_int8_t *buf, u_int16_t buflen) {
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx->ctx;
    u_int8_t *bufmin = buf;
    u_int8_t *bufend = buf + buflen - 1;
    u_int32_t cnt = 0;
    WmPattern *p;
    MpmEndMatch *em; 
    WmHashItem *thi, *hi;

    if (buflen == 0)
        return 0;

    //printf("BUF "); prt(buf,buflen); printf("\n");

    if (mpm_ctx->scan_minlen == 1) {
        while (buf <= bufend) {
            u_int8_t h = wm_tolower(*buf);
            hi = &wm_ctx->scan_hash1[h];

            if (hi->flags & 0x01) {
                for (thi = hi; thi != NULL; thi = thi->nxt) {
                    p = wm_ctx->parray[thi->idx];

                    if (p->len != 1)
                        continue;

                    if (p->flags & WUMANBER_NOCASE) {
                        if (wm_tolower(*buf) == p->ci[0]) {
                            //printf("CI Exact match: "); prt(p->ci, p->len); printf(" in buf "); prt(buf, p->len);printf(" (WmScan1)\n");
                            for (em = p->em; em; em = em->next) {
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf+1 - bufmin), p->len))
                                    cnt++;
                            }
                        }
                    } else {
                        if (*buf == p->cs[0]) {
                            //printf("CS Exact match: "); prt(p->cs, p->len); printf(" in buf "); prt(buf, p->len);printf(" (WmScan1)\n");
                            for (em = p->em; em; em = em->next) {
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf+1 - bufmin), p->len))
                                    cnt++;
                            }
                        }
                    }
                }
            }
            buf += 1;
        }
    }
    //printf("WmScan1: after 1byte cnt %u\n", cnt);
    if (mpm_ctx->scan_maxlen > 1) {
        /* Pass bufmin on because buf no longer points to the
         * start of the buffer. */
        cnt += wm_ctx->MBScan(mpm_ctx, mpm_thread_ctx, pmq, bufmin, buflen);
        //printf("WmScan1: after 2+byte cnt %u\n", cnt);
    }
    return cnt;
}


/* SEARCH FUNCTIONS */
u_int32_t WmSearch2Hash9(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *pmq, u_int8_t *buf, u_int16_t buflen) {
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx->ctx;
#ifdef WUMANBER_COUNTERS
    WmThreadCtx *wm_thread_ctx = (WmThreadCtx *)mpm_thread_ctx->ctx;
#endif /* WUMANBER_COUNTERS */
    u_int32_t cnt = 0;
    u_int8_t *bufmin = buf;
    u_int8_t *bufend = buf + buflen - 1;
    u_int16_t sl = wm_ctx->search_shiftlen;
    u_int16_t h;
    u_int8_t shift;
    WmHashItem *thi, *hi;
    WmPattern *p;
    u_int16_t prefixci_buf;
    u_int16_t prefixcs_buf;

    if (buflen == 0)
        return 0;

    //printf("BUF(%u) ", buflen); prt(buf,buflen); printf(" (sl %u)\n", sl);

    buf+=(sl-1);
    //buf++;

    while (buf <= bufend) {
    //while (buf < bufend) {
        h = HASH9(wm_tolower(*buf),(wm_tolower(*(buf-1))));
        shift = wm_ctx->search_shifttable[h];
        //printf("%p %u search: h %u, shift %u\n", buf, buf - bufmin, h, shift);

        if (shift == 0) {
            COUNT(wm_thread_ctx->search_stat_shift_null++);
            /* get our hash item */
            hi = wm_ctx->search_hash[h];
            if (hi != NULL) {
                //printf("buf-sl+1 %p, buf-sl+2 %p\n", buf-sl+1, buf-sl+2);
                prefixci_buf = (u_int16_t)(wm_tolower(*(buf-sl+1)) + wm_tolower(*(buf-sl+2)));
                prefixcs_buf = (u_int16_t)(*(buf-sl+1) + *(buf-sl+2));
                //printf("WmSearch2: prefixci_buf %u, prefixcs_buf %u\n", prefixci_buf, prefixcs_buf);
                for (thi = hi; thi != NULL; thi = thi->nxt) {
                    p = wm_ctx->parray[thi->idx];

                    //printf("WmSearch2: p->prefix_ci %u, p->prefix_cs %u\n",
                    //    p->prefix_ci, p->prefix_cs);

                    if (p->flags & WUMANBER_NOCASE) {
                        if (p->prefix_ci != prefixci_buf || p->len > (bufend-(buf-sl)))
                            continue;

                        if (memcmp_lowercase(p->ci, buf-sl+1, p->len) == 0) {
                            //printf("CI Exact match: "); prt(p->ci, p->len); printf("\n");
                            COUNT(wm_thread_ctx->search_stat_loop_match++);

                            MpmEndMatch *em; 
                            for (em = p->em; em; em = em->next) {
                                //printf("em %p id %u\n", em, em->id);
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf-sl+1 - bufmin), p->len))
                                    cnt++;
                            }

                        } else {
                            COUNT(wm_thread_ctx->search_stat_loop_no_match++);
                        }
                    } else {
                        if (p->prefix_cs != prefixcs_buf || p->len > (bufend-(buf-sl)))
                            continue;
                        if (memcmp(p->cs, buf-sl+1, p->len) == 0) {
                            //printf("CS Exact match: "); prt(p->cs, p->len); printf("\n");
                            COUNT(wm_thread_ctx->search_stat_loop_match++);

                            MpmEndMatch *em; 
                            for (em = p->em; em; em = em->next) {
                                //printf("em %p id %u\n", em, em->id);
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf-sl+1 - bufmin), p->len))
                                    cnt++;
                            }

                        } else {
                            COUNT(wm_thread_ctx->search_stat_loop_no_match++);
                        }
                    }
                }
            }
            shift = 1;
        } else {
            COUNT(wm_thread_ctx->search_stat_total_shift += shift);
            COUNT(wm_thread_ctx->search_stat_num_shift++);
        }
        buf += shift;
    }

    //printf("cnt %u\n", cnt);
    return cnt;
}

u_int32_t WmSearch2Hash12(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *pmq, u_int8_t *buf, u_int16_t buflen) {
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx->ctx;
#ifdef WUMANBER_COUNTERS
    WmThreadCtx *wm_thread_ctx = (WmThreadCtx *)mpm_thread_ctx->ctx;
#endif /* WUMANBER_COUNTERS */
    u_int32_t cnt = 0;
    u_int8_t *bufmin = buf;
    u_int8_t *bufend = buf + buflen - 1;
    u_int16_t sl = wm_ctx->search_shiftlen;
    u_int16_t h;
    u_int8_t shift;
    WmHashItem *thi, *hi;
    WmPattern *p;
    u_int16_t prefixci_buf;
    u_int16_t prefixcs_buf;

    if (buflen == 0)
        return 0;

    //printf("BUF(%u) ", buflen); prt(buf,buflen); printf("\n");

    buf+=(sl-1);
    //buf++;

    while (buf <= bufend) {
        //h = (wm_tolower(*buf)<<8)+(wm_tolower(*(buf-1)));
        h = HASH12(wm_tolower(*buf),(wm_tolower(*(buf-1))));
        shift = wm_ctx->search_shifttable[h];
        //printf("search: h %u, shift %u\n", h, shift);

        if (shift == 0) {
            COUNT(wm_thread_ctx->search_stat_shift_null++);
            /* get our hash item */
            hi = wm_ctx->search_hash[h];
            //printf("search: hi %p\n", hi);
            if (hi != NULL) {
                prefixci_buf = (u_int16_t)(wm_tolower(*(buf-sl+1)) + wm_tolower(*(buf-sl+2)));
                prefixcs_buf = (u_int16_t)(*(buf-sl+1) + *(buf-sl+2));
                //printf("WmSearch2: prefixci_buf %u, prefixcs_buf %u\n", prefixci_buf, prefixcs_buf);
                for (thi = hi; thi != NULL; thi = thi->nxt) {
                    p = wm_ctx->parray[thi->idx];

                    //printf("WmSearch2: p->prefix_ci %u, p->prefix_cs %u\n",
                    //    p->prefix_ci, p->prefix_cs);

                    if (p->flags & WUMANBER_NOCASE) {
                        if (p->prefix_ci != prefixci_buf || p->len > (bufend-(buf-sl)))
                            continue;

                        if (memcmp_lowercase(p->ci, buf-sl+1, p->len) == 0) {
                            //printf("CI Exact match: "); prt(p->ci, p->len); printf("\n");
                            COUNT(wm_thread_ctx->search_stat_loop_match++);

                            MpmEndMatch *em; 
                            for (em = p->em; em; em = em->next) {
                                //printf("em %p id %u\n", em, em->id);
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf-sl+1 - bufmin), p->len))
                                    cnt++;
                            }

                        } else {
                            COUNT(wm_thread_ctx->search_stat_loop_no_match++);
                        }
                    } else {
                        if (p->prefix_cs != prefixcs_buf || p->len > (bufend-(buf-sl)))
                            continue;
                        if (memcmp(p->cs, buf-sl+1, p->len) == 0) {
                            //printf("CS Exact match: "); prt(p->cs, p->len); printf("\n");
                            COUNT(wm_thread_ctx->search_stat_loop_match++);

                            MpmEndMatch *em; 
                            for (em = p->em; em; em = em->next) {
                                //printf("em %p id %u\n", em, em->id);
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf-sl+1 - bufmin), p->len))
                                    cnt++;
                            }

                        } else {
                            COUNT(wm_thread_ctx->search_stat_loop_no_match++);
                        }
                    }
                }
            }
            shift = 1;
        } else {
            COUNT(wm_thread_ctx->search_stat_total_shift += shift);
            COUNT(wm_thread_ctx->search_stat_num_shift++);
        }
        buf += shift;
    }

    return cnt;
}

u_int32_t WmSearch2Hash14(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *pmq, u_int8_t *buf, u_int16_t buflen) {
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx->ctx;
#ifdef WUMANBER_COUNTERS
    WmThreadCtx *wm_thread_ctx = (WmThreadCtx *)mpm_thread_ctx->ctx;
#endif /* WUMANBER_COUNTERS */
    u_int32_t cnt = 0;
    u_int8_t *bufmin = buf;
    u_int8_t *bufend = buf + buflen - 1;
    u_int16_t sl = wm_ctx->search_shiftlen;
    u_int16_t h;
    u_int8_t shift;
    WmHashItem *thi, *hi;
    WmPattern *p;
    u_int16_t prefixci_buf;
    u_int16_t prefixcs_buf;

    if (buflen == 0)
        return 0;

    //printf("BUF(%u) ", buflen); prt(buf,buflen); printf("\n");

    buf+=(sl-1);
    //buf++;

    while (buf <= bufend) {
        //h = (wm_tolower(*buf)<<8)+(wm_tolower(*(buf-1)));
        h = HASH14(wm_tolower(*buf),(wm_tolower(*(buf-1))));
        shift = wm_ctx->search_shifttable[h];
        //printf("search: h %u, shift %u\n", h, shift);

        if (shift == 0) {
            COUNT(wm_thread_ctx->search_stat_shift_null++);
            /* get our hash item */
            hi = wm_ctx->search_hash[h];
            //printf("search: hi %p\n", hi);
            if (hi != NULL) {
                prefixci_buf = (u_int16_t)(wm_tolower(*(buf-sl+1)) + wm_tolower(*(buf-sl+2)));
                prefixcs_buf = (u_int16_t)(*(buf-sl+1) + *(buf-sl+2));
                //printf("WmSearch2: prefixci_buf %u, prefixcs_buf %u\n", prefixci_buf, prefixcs_buf);
                for (thi = hi; thi != NULL; thi = thi->nxt) {
                    p = wm_ctx->parray[thi->idx];

                    //printf("WmSearch2: p->prefix_ci %u, p->prefix_cs %u\n",
                    //    p->prefix_ci, p->prefix_cs);

                    if (p->flags & WUMANBER_NOCASE) {
                        if (p->prefix_ci != prefixci_buf || p->len > (bufend-(buf-sl)))
                            continue;

                        if (memcmp_lowercase(p->ci, buf-sl+1, p->len) == 0) {
                            //printf("CI Exact match: "); prt(p->ci, p->len); printf("\n");
                            COUNT(wm_thread_ctx->search_stat_loop_match++);

                            MpmEndMatch *em; 
                            for (em = p->em; em; em = em->next) {
                                //printf("em %p id %u\n", em, em->id);
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf-sl+1 - bufmin), p->len))
                                    cnt++;
                            }

                        } else {
                            COUNT(wm_thread_ctx->search_stat_loop_no_match++);
                        }
                    } else {
                        if (p->prefix_cs != prefixcs_buf || p->len > (bufend-(buf-sl)))
                            continue;
                        if (memcmp(p->cs, buf-sl+1, p->len) == 0) {
                            //printf("CS Exact match: "); prt(p->cs, p->len); printf("\n");
                            COUNT(wm_thread_ctx->search_stat_loop_match++);

                            MpmEndMatch *em; 
                            for (em = p->em; em; em = em->next) {
                                //printf("em %p id %u\n", em, em->id);
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf-sl+1 - bufmin), p->len))
                                    cnt++;
                            }

                        } else {
                            COUNT(wm_thread_ctx->search_stat_loop_no_match++);
                        }
                    }
                }
            }
            shift = 1;
        } else {
            COUNT(wm_thread_ctx->search_stat_total_shift += shift);
            COUNT(wm_thread_ctx->search_stat_num_shift++);
        }
        buf += shift;
    }

    return cnt;
}

u_int32_t WmSearch2Hash15(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *pmq, u_int8_t *buf, u_int16_t buflen) {
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx->ctx;
#ifdef WUMANBER_COUNTERS
    WmThreadCtx *wm_thread_ctx = (WmThreadCtx *)mpm_thread_ctx->ctx;
#endif /* WUMANBER_COUNTERS */
    u_int32_t cnt = 0;
    u_int8_t *bufmin = buf;
    u_int8_t *bufend = buf + buflen - 1;
    u_int16_t sl = wm_ctx->search_shiftlen;
    u_int16_t h;
    u_int8_t shift;
    WmHashItem *thi, *hi;
    WmPattern *p;
    u_int16_t prefixci_buf;
    u_int16_t prefixcs_buf;

    if (buflen == 0)
        return 0;

    //printf("BUF(%u) ", buflen); prt(buf,buflen); printf("\n");

    buf+=(sl-1);

    while (buf <= bufend) {
        h = HASH15(wm_tolower(*buf),(wm_tolower(*(buf-1))));
        shift = wm_ctx->search_shifttable[h];
        //printf("search: h %u, shift %u\n", h, shift);

        if (shift == 0) {
            COUNT(wm_thread_ctx->search_stat_shift_null++);
            /* get our hash item */
            hi = wm_ctx->search_hash[h];
            //printf("search: hi %p\n", hi);
            if (hi != NULL) {
                prefixci_buf = (u_int16_t)(wm_tolower(*(buf-sl+1)) + wm_tolower(*(buf-sl+2)));
                prefixcs_buf = (u_int16_t)(*(buf-sl+1) + *(buf-sl+2));
                //printf("WmSearch2: prefixci_buf %u, prefixcs_buf %u\n", prefixci_buf, prefixcs_buf);
                for (thi = hi; thi != NULL; thi = thi->nxt) {
                    p = wm_ctx->parray[thi->idx];

                    //printf("WmSearch2: p->prefix_ci %u, p->prefix_cs %u\n",
                    //    p->prefix_ci, p->prefix_cs);

                    if (p->flags & WUMANBER_NOCASE) {
                        if (p->prefix_ci != prefixci_buf || p->len > (bufend-(buf-sl)))
                            continue;

                        if (memcmp_lowercase(p->ci, buf-sl+1, p->len) == 0) {
                            //printf("CI Exact match: "); prt(p->ci, p->len); printf("\n");
                            COUNT(wm_thread_ctx->search_stat_loop_match++);

                            MpmEndMatch *em; 
                            for (em = p->em; em; em = em->next) {
                                //printf("em %p id %u\n", em, em->id);
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf-sl+1 - bufmin), p->len))
                                    cnt++;
                            }

                        } else {
                            COUNT(wm_thread_ctx->search_stat_loop_no_match++);
                        }
                    } else {
                        if (p->prefix_cs != prefixcs_buf || p->len > (bufend-(buf-sl)))
                            continue;
                        if (memcmp(p->cs, buf-sl+1, p->len) == 0) {
                            //printf("CS Exact match: "); prt(p->cs, p->len); printf("\n");
                            COUNT(wm_thread_ctx->search_stat_loop_match++);

                            MpmEndMatch *em; 
                            for (em = p->em; em; em = em->next) {
                                //printf("em %p id %u\n", em, em->id);
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf-sl+1 - bufmin), p->len))
                                    cnt++;
                            }

                        } else {
                            COUNT(wm_thread_ctx->search_stat_loop_no_match++);
                        }
                    }
                }
            }
            shift = 1;
        } else {
            COUNT(wm_thread_ctx->search_stat_total_shift += shift);
            COUNT(wm_thread_ctx->search_stat_num_shift++);
        }
        buf += shift;
    }

    return cnt;
}

u_int32_t WmSearch2Hash16(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *pmq, u_int8_t *buf, u_int16_t buflen) {
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx->ctx;
#ifdef WUMANBER_COUNTERS
    WmThreadCtx *wm_thread_ctx = (WmThreadCtx *)mpm_thread_ctx->ctx;
#endif /* WUMANBER_COUNTERS */
    u_int32_t cnt = 0;
    u_int8_t *bufmin = buf;
    u_int8_t *bufend = buf + buflen - 1;
    u_int16_t sl = wm_ctx->search_shiftlen;
    u_int16_t h;
    u_int8_t shift;
    WmHashItem *thi, *hi;
    WmPattern *p;
    u_int16_t prefixci_buf;
    u_int16_t prefixcs_buf;

    if (buflen == 0)
        return 0;

    //printf("BUF(%u) ", buflen); prt(buf,buflen); printf("\n");

    buf+=(sl-1);

    while (buf <= bufend) {
        h = HASH16(wm_tolower(*buf),(wm_tolower(*(buf-1))));
        shift = wm_ctx->search_shifttable[h];
        //printf("search: h %u, shift %u\n", h, shift);

        if (shift == 0) {
            COUNT(wm_thread_ctx->search_stat_shift_null++);
            /* get our hash item */
            hi = wm_ctx->search_hash[h];
            //printf("search: hi %p\n", hi);
            if (hi != NULL) {
                prefixci_buf = (u_int16_t)(wm_tolower(*(buf-sl+1)) + wm_tolower(*(buf-sl+2)));
                prefixcs_buf = (u_int16_t)(*(buf-sl+1) + *(buf-sl+2));
                //printf("WmSearch2: prefixci_buf %u, prefixcs_buf %u\n", prefixci_buf, prefixcs_buf);
                for (thi = hi; thi != NULL; thi = thi->nxt) {
                    p = wm_ctx->parray[thi->idx];

                    //printf("WmSearch2: p->prefix_ci %u, p->prefix_cs %u\n",
                    //    p->prefix_ci, p->prefix_cs);

                    if (p->flags & WUMANBER_NOCASE) {
                        if (p->prefix_ci != prefixci_buf || p->len > (bufend-(buf-sl)))
                            continue;

                        if (memcmp_lowercase(p->ci, buf-sl+1, p->len) == 0) {
                            //printf("CI Exact match: "); prt(p->ci, p->len); printf("\n");
                            COUNT(wm_thread_ctx->search_stat_loop_match++);

                            MpmEndMatch *em; 
                            for (em = p->em; em; em = em->next) {
                                //printf("em %p id %u\n", em, em->id);
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf-sl+1 - bufmin), p->len))
                                    cnt++;
                            }

                        } else {
                            COUNT(wm_thread_ctx->search_stat_loop_no_match++);
                        }
                    } else {
                        if (p->prefix_cs != prefixcs_buf || p->len > (bufend-(buf-sl)))
                            continue;
                        if (memcmp(p->cs, buf-sl+1, p->len) == 0) {
                            //printf("CS Exact match: "); prt(p->cs, p->len); printf("\n");
                            COUNT(wm_thread_ctx->search_stat_loop_match++);

                            MpmEndMatch *em; 
                            for (em = p->em; em; em = em->next) {
                                //printf("em %p id %u\n", em, em->id);
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf-sl+1 - bufmin), p->len))
                                    cnt++;
                            }

                        } else {
                            COUNT(wm_thread_ctx->search_stat_loop_no_match++);
                        }
                    }
                }
            }
            shift = 1;
        } else {
            COUNT(wm_thread_ctx->search_stat_total_shift += shift);
            COUNT(wm_thread_ctx->search_stat_num_shift++);
        }
        buf += shift;
    }

    return cnt;
}

u_int32_t WmSearch1(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, PatternMatcherQueue *pmq, u_int8_t *buf, u_int16_t buflen) {
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx->ctx;
    u_int8_t *bufmin = buf;
    u_int8_t *bufend = buf + buflen - 1;
    u_int32_t cnt = 0;
    WmPattern *p;
    MpmEndMatch *em; 
    WmHashItem *thi, *hi;

    if (buflen == 0)
        return 0;

    //printf("BUF "); prt(buf,buflen); printf("\n");

    if (mpm_ctx->search_minlen == 1) {
        while (buf <= bufend) {
            u_int8_t h = wm_tolower(*buf);
            hi = &wm_ctx->search_hash1[h];

            if (hi->flags & 0x01) {
                for (thi = hi; thi != NULL; thi = thi->nxt) {
                    p = wm_ctx->parray[thi->idx];

                    if (p->len != 1)
                        continue;

                    if (p->flags & WUMANBER_NOCASE) {
                        if (wm_tolower(*buf) == p->ci[0]) {
                            //printf("CI Exact match: "); prt(p->ci, p->len); printf(" in buf "); prt(buf, p->len);printf(" (WmSearch1)\n");
                            for (em = p->em; em; em = em->next) {
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf+1 - bufmin), p->len))
                                    cnt++;
                            }
                        }
                    } else {
                        if (*buf == p->cs[0]) {
                            //printf("CS Exact match: "); prt(p->cs, p->len); printf(" in buf "); prt(buf, p->len);printf(" (WmSearch1)\n");
                            for (em = p->em; em; em = em->next) {
                                if (MpmMatchAppend(mpm_thread_ctx, pmq, em, &mpm_thread_ctx->match[em->id],(buf+1 - bufmin), p->len))
                                    cnt++;
                            }
                        }
                    }
                }
            }
            buf += 1;
        }
    }
    //printf("WmSearch1: after 1byte cnt %u\n", cnt);
    if (mpm_ctx->search_maxlen > 1) {
        /* Pass bufmin on because buf no longer points to the
         * start of the buffer. */
        cnt += wm_ctx->MBSearch(mpm_ctx, mpm_thread_ctx, pmq, bufmin, buflen);
        //printf("WmSearch1: after 2+byte cnt %u\n", cnt);
    }
    return cnt;
}

void WmInitCtx (MpmCtx *mpm_ctx) {
    //printf("WmInitCtx: mpm_ctx %p\n", mpm_ctx);

    memset(mpm_ctx, 0, sizeof(MpmCtx));

    mpm_ctx->ctx = malloc(sizeof(WmCtx));
    if (mpm_ctx->ctx == NULL)
        return;

    memset(mpm_ctx->ctx, 0, sizeof(WmCtx));

    mpm_ctx->memory_cnt++;
    mpm_ctx->memory_size += sizeof(WmCtx);

    /* initialize the hash we use to speed up pattern insertions */
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx->ctx;
    wm_ctx->init_hash = malloc(sizeof(WmPattern *) * INIT_HASH_SIZE);
    if (wm_ctx->init_hash == NULL)
        return;

    memset(wm_ctx->init_hash, 0, sizeof(WmPattern *) * INIT_HASH_SIZE);
}

void WmDestroyCtx(MpmCtx *mpm_ctx) {
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx->ctx;
    if (wm_ctx == NULL)
        return;

    if (wm_ctx->init_hash) {
        free(wm_ctx->init_hash);
        mpm_ctx->memory_cnt--;
        mpm_ctx->memory_size -= (INIT_HASH_SIZE * sizeof(WmPattern *));
    }

    if (wm_ctx->parray) {
        u_int32_t i;
        for (i = 0; i < mpm_ctx->pattern_cnt; i++) {
            if (wm_ctx->parray[i] != NULL) {
                WmFreePattern(mpm_ctx, wm_ctx->parray[i]);
            }
        }

        free(wm_ctx->parray);
        mpm_ctx->memory_cnt--;
        mpm_ctx->memory_size -= (mpm_ctx->pattern_cnt * sizeof(WmPattern));
    }

    if (wm_ctx->scan_hash) {
        free(wm_ctx->scan_hash);
        mpm_ctx->memory_cnt--;
        mpm_ctx->memory_size -= (sizeof(WmHashItem) * wm_ctx->scan_hash_size);
    }

    if (wm_ctx->scan_shifttable) {
        free(wm_ctx->scan_shifttable);
        mpm_ctx->memory_cnt--;
        mpm_ctx->memory_size -= (sizeof(u_int16_t) * wm_ctx->scan_hash_size);
    }

    if (wm_ctx->search_hash) {
        free(wm_ctx->search_hash);
        mpm_ctx->memory_cnt--;
        mpm_ctx->memory_size -= (sizeof(WmHashItem) * wm_ctx->search_hash_size);
    }

    if (wm_ctx->search_shifttable) {
        free(wm_ctx->search_shifttable);
        mpm_ctx->memory_cnt--;
        mpm_ctx->memory_size -= (sizeof(u_int16_t) * wm_ctx->search_hash_size);
    }

    free(mpm_ctx->ctx);
    mpm_ctx->memory_cnt--;
    mpm_ctx->memory_size -= sizeof(WmCtx);
}

void WmThreadInitCtx(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx, u_int32_t matchsize) {
    memset(mpm_thread_ctx, 0, sizeof(MpmThreadCtx));

    mpm_thread_ctx->ctx = malloc(sizeof(WmThreadCtx));
    if (mpm_thread_ctx->ctx == NULL)
        return;

    memset(mpm_thread_ctx->ctx, 0, sizeof(WmThreadCtx));

    mpm_thread_ctx->memory_cnt++;
    mpm_thread_ctx->memory_size += sizeof(WmThreadCtx);

    /* alloc an array with the size of _all_ keys in all instances.
     * this is done so the detect engine won't have to care about
     * what instance it's looking up in. The matches all have a
     * unique id and is the array lookup key at the same time */
    //u_int32_t keys = mpm_ctx->max_pattern_id + 1;
    u_int32_t keys = matchsize + 1;
    if (keys) {
        mpm_thread_ctx->match = malloc(keys * sizeof(MpmMatchBucket));
        if (mpm_thread_ctx->match == NULL) {
            printf("ERROR: could not setup memory for pattern matcher: %s\n", strerror(errno));
            exit(1);
        }
        memset(mpm_thread_ctx->match, 0, keys * sizeof(MpmMatchBucket));

        mpm_thread_ctx->memory_cnt++;
        mpm_thread_ctx->memory_size += (keys * sizeof(MpmMatchBucket));
    }
}

void WmThreadDestroyCtx(MpmCtx *mpm_ctx, MpmThreadCtx *mpm_thread_ctx) {
    WmThreadCtx *wm_ctx = (WmThreadCtx *)mpm_thread_ctx->ctx;
    if (wm_ctx) {
        if (mpm_thread_ctx->match != NULL) {
            mpm_thread_ctx->memory_cnt--;
            mpm_thread_ctx->memory_size -= ((mpm_ctx->max_pattern_id + 1) * sizeof(MpmMatchBucket));
            free(mpm_thread_ctx->match);
        }

        mpm_thread_ctx->memory_cnt--;
        mpm_thread_ctx->memory_size -= sizeof(WmThreadCtx);
        free(mpm_thread_ctx->ctx);
    }

    MpmMatchFreeSpares(mpm_thread_ctx, mpm_thread_ctx->sparelist);
    MpmMatchFreeSpares(mpm_thread_ctx, mpm_thread_ctx->qlist);
}

/*
 * ONLY TESTS BELOW THIS COMMENT
 */


int WmTestInitCtx01 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    WmInitCtx(&mpm_ctx);

    if (mpm_ctx.ctx != NULL)
        result = 1;

    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestInitCtx02 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    WmInitCtx(&mpm_ctx);

    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx; 

    if (wm_ctx->parray == NULL)
        result = 1;

    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestInitCtx03 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    if (mpm_ctx.Search == WmSearch2Hash16)
        result = 1;

    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestThreadInitCtx01 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;

    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    if (mpm_thread_ctx.memory_cnt == 2)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestThreadInitCtx02 (void) {
#ifdef WUMANBER_COUNTERS
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;

    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    WmThreadCtx *wm_thread_ctx = (WmThreadCtx *)mpm_thread_ctx.ctx;

    if (wm_thread_ctx->search_stat_shift_null == 0)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
#else
    int result = 1;
#endif
    return result;
}

int WmTestInitAddPattern01 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;

    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    int ret = WmAddPattern(&mpm_ctx, (u_int8_t *)"abcd", 4, 0, 0, 1, 0, 1234, 0);
    if (ret == 0)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestInitAddPattern02 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;

    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPattern(&mpm_ctx, (u_int8_t *)"abcd", 4, 0, 0, 1, 0, 1234, 0);
    if (wm_ctx->init_hash != NULL)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestInitAddPattern03 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;

    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPattern(&mpm_ctx, (u_int8_t *)"abcd", 4, 0, 0, 1, 0, 1234, 0);
    WmPattern *pat = WmInitHashLookup(wm_ctx, (u_int8_t *)"abcd", 4, 1);
    if (pat != NULL) {
        if (pat->len == 4)
            result = 1;
    }

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestInitAddPattern04 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;

    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPattern(&mpm_ctx, (u_int8_t *)"abcd", 4, 0, 0, 1, 0, 1234, 0);
    WmPattern *pat = WmInitHashLookup(wm_ctx, (u_int8_t *)"abcd", 4, 1);
    if (pat != NULL) {
        if (pat->flags & WUMANBER_NOCASE)
            result = 1;
    }

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestInitAddPattern05 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;

    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPattern(&mpm_ctx, (u_int8_t *)"abcd", 4, 0, 0, 0, 0, 1234, 0);
    WmPattern *pat = WmInitHashLookup(wm_ctx, (u_int8_t *)"abcd", 4, 0);
    if (pat != NULL) {
        if (!(pat->flags & WUMANBER_NOCASE))
            result = 1;
    }

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestInitAddPattern06 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;

    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPattern(&mpm_ctx, (u_int8_t *)"abcd", 4, 0, 0, 1, 0, 1234, 0);
    WmPattern *pat = WmInitHashLookup(wm_ctx, (u_int8_t *)"abcd", 4, 1);
    if (pat != NULL) {
        if (memcmp(pat->cs, "abcd", 4) == 0)
            result = 1;
    }

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestInitAddPattern07 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;

    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"abcd", 4, 0, 0, 1, 0, 1234, 0);

    if (mpm_ctx.max_pattern_id == 1234)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestPrepare01 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"a", 1, 0, 0, 1, 0, 0, 0);
    WmPreparePatterns(&mpm_ctx);

    if (mpm_ctx.Search == WmSearch1)
        result = 1;

    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestPrepare02 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"abcd", 4, 0, 0, 1, 0, 0, 0);
    WmPreparePatterns(&mpm_ctx);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    if (wm_ctx->search_shiftlen == 4)
        result = 1;

    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestPrepare03 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"abcd", 4, 0, 0, 1, 0, 0, 0);
    WmPreparePatterns(&mpm_ctx);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    if (wm_ctx->search_shifttable[1] == 4)
        result = 1;
    else
        printf("4 != %u: ", wm_ctx->search_shifttable[1]);

    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestPrepare04 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"a", 1, 0, 0, 1, 1, 0, 0);
    WmPreparePatterns(&mpm_ctx);

    if (mpm_ctx.Scan == WmScan1)
        result = 1;

    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestPrepare05 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"abcd", 4, 0, 0, 1, 1, 0, 0);
    WmPreparePatterns(&mpm_ctx);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    if (wm_ctx->scan_shiftlen == 4)
        result = 1;

    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestPrepare06 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"abcd", 4, 0, 0, 1, 1, 0, 0);
    WmPreparePatterns(&mpm_ctx);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    if (wm_ctx->scan_shifttable[1] == 4)
        result = 1;
    else
        printf("4 != %u: ", wm_ctx->scan_shifttable[1]);

    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch01 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;

    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"abcd", 4, 0, 0, 1, 0, 0, 0);
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);
    //mpm_ctx.PrintCtx(&mpm_ctx);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"abcd", 4);
    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 1)
        result = 1;
    else
        printf("1 != %u ", cnt);

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch01Hash12 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;

    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPattern(&mpm_ctx, (u_int8_t *)"abcd", 4, 0, 0, 1, 0, 0, 0);

    wm_ctx->search_hash_size = HASH12_SIZE;
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);
    //mpm_ctx.PrintCtx(&mpm_ctx);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"abcd", 4);
    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 1)
        result = 1;
    else
        printf("1 != %u ", cnt);

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch01Hash14 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;

    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPattern(&mpm_ctx, (u_int8_t *)"abcd", 4, 0, 0, 1, 0, 0, 0);

    wm_ctx->search_hash_size = HASH14_SIZE;
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);
    //mpm_ctx.PrintCtx(&mpm_ctx);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"abcd", 4);
    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 1)
        result = 1;
    else
        printf("1 != %u ", cnt);

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch01Hash15 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;

    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPattern(&mpm_ctx, (u_int8_t *)"abcd", 4, 0, 0, 1, 0, 0, 0);

    wm_ctx->search_hash_size = HASH15_SIZE;
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);
    //mpm_ctx.PrintCtx(&mpm_ctx);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"abcd", 4);
    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 1)
        result = 1;
    else
        printf("1 != %u ", cnt);

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch01Hash16 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;

    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPattern(&mpm_ctx, (u_int8_t *)"abcd", 4, 0, 0, 1, 0, 0, 0);

    wm_ctx->search_hash_size = HASH16_SIZE;
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);
    //mpm_ctx.PrintCtx(&mpm_ctx);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"abcd", 4);
    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 1)
        result = 1;
    else
        printf("1 != %u ", cnt);

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch02 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"abcd", 4, 0, 0, 1, 0, 0, 0);
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"abce", 4);
    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 0)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch03 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"abcd", 4, 0, 0, 1, 0, 0, 0);
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"abcdefgh", 8);
    MpmMatchCleanup(&mpm_thread_ctx);
    if (cnt == 1)
        result = 1;
    else
        printf("1 != %u ", cnt);

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch04 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"bcde", 4, 0, 0, 1, 0, 0, 0);
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"abcdefgh", 8);
    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 1)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch05 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"efgh", 4, 0, 0, 1, 0, 0, 0);
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"abcdefgh", 8);
    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 1)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch06 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"eFgH", 4, 0, 0, 1, 0, 0, 0);
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"abcdEfGh", 8);
    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 1)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch07 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"abcd", 4, 0, 0, 0, 0, 0, 0);
    WmAddPattern(&mpm_ctx, (u_int8_t *)"eFgH", 4, 0, 0, 1, 0, 1, 0);
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 2);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"abcdEfGh", 8);
    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 2)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch08 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"abcde", 5, 0, 0, 1, 0, 0, 0);
    WmAddPattern(&mpm_ctx, (u_int8_t *)"bcde",  4, 0, 0, 1, 0, 1, 0);
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 2);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"abcdefgh", 8);
    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 2)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch09 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"ab", 2, 0, 0, 1, 0, 0, 0);
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"ab", 2);
    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 1)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch10 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"bc", 2, 0, 0, 1, 0, 0, 0);
    WmAddPattern(&mpm_ctx, (u_int8_t *)"gh", 2, 0, 0, 1, 0, 1, 0);
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 2);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"abcdefgh", 8);
    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 2)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch11 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"a", 1, 0, 0, 1, 0, 0, 0);
    WmAddPattern(&mpm_ctx, (u_int8_t *)"d", 1, 0, 0, 1, 0, 1, 0);
    WmAddPattern(&mpm_ctx, (u_int8_t *)"h", 1, 0, 0, 1, 0, 2, 0);
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 3);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"abcdefgh", 8);
    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 3)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch12 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"A", 1, 0, 0, 1, 0, 0, 0);
    WmAddPattern(&mpm_ctx, (u_int8_t *)"d", 1, 0, 0, 1, 0, 1, 0);
    WmAddPattern(&mpm_ctx, (u_int8_t *)"Z", 1, 0, 0, 1, 0, 2, 0);
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 3);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"abcdefgh", 8);
    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 2)
        result = 1;
    else
        printf("2 != %u: ", cnt);

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch13 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"a", 1, 0, 0, 1, 0, 0, 0);
    WmAddPattern(&mpm_ctx, (u_int8_t *)"de",2, 0, 0, 1, 0, 1, 0);
    WmAddPattern(&mpm_ctx, (u_int8_t *)"h", 1, 0, 0, 1, 0, 2, 0);
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 3);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"abcdefgh", 8);
    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 3)
        result = 1;
    else
        printf("3 != %u: ", cnt);

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch14 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"A", 1, 0, 0, 1, 0, 0, 0);
    WmAddPattern(&mpm_ctx, (u_int8_t *)"de",2, 0, 0, 1, 0, 1, 0);
    WmAddPattern(&mpm_ctx, (u_int8_t *)"Z", 1, 0, 0, 1, 0, 2, 0);
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 3);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"abcdefgh", 8);
    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 2)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch15 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"A", 1, 0, 0, 1, 0, 0, 0);
    WmAddPattern(&mpm_ctx, (u_int8_t *)"de",2, 0, 0, 1, 0, 1, 0);
    WmAddPattern(&mpm_ctx, (u_int8_t *)"Z", 1, 0, 0, 1, 0, 2, 0);
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 3);

    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"abcdefgh", 8);

    u_int32_t len = mpm_thread_ctx.match[1].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 1)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch16 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPattern(&mpm_ctx, (u_int8_t *)"A", 1, 0, 0, 1, 0, 0, 0);
    WmAddPattern(&mpm_ctx, (u_int8_t *)"de",2, 0, 0, 1, 0, 1, 0);
    WmAddPattern(&mpm_ctx, (u_int8_t *)"Z", 1, 0, 0, 1, 0, 2, 0);
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 3);

    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"abcdefgh", 8);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 1)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch17 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"/VideoAccessCodecInstall.exe", 28, 0, 0, 0, 0);
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"/VideoAccessCodecInstall.exe", 28);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 1)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch18Hash12 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"/VideoAccessCodecInstall.exe", 28, 0, 0, 0, 0);
    wm_ctx->search_hash_size = HASH12_SIZE;
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"/VideoAccessCodecInstaLL.exe", 28);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 0)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch18Hash14 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"/VideoAccessCodecInstall.exe", 28, 0, 0, 0, 0);
    wm_ctx->search_hash_size = HASH14_SIZE;
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"/VideoAccessCodecInstaLL.exe", 28);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 0)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch18Hash15 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"/VideoAccessCodecInstall.exe", 28, 0, 0, 0, 0);
    wm_ctx->search_hash_size = HASH15_SIZE;
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"/VideoAccessCodecInstaLL.exe", 28);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 0)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch18 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"/VideoAccessCodecInstall.exe", 28, 0, 0, 0, 0);
    wm_ctx->search_hash_size = HASH16_SIZE;
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"/VideoAccessCodecInstaLL.exe", 28);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 0)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch18Hash16 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"/VideoAccessCodecInstall.exe", 28, 0, 0, 0, 0);
    wm_ctx->search_hash_size = HASH16_SIZE;
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"/VideoAccessCodecInstaLL.exe", 28);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 0)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch19 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPatternCI(&mpm_ctx, (u_int8_t *)"/VideoAccessCodecInstall.exe", 28, 0, 0, 0, 0);
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"/VideoAccessCodecInstaLL.exe", 28);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 1)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch19Hash12 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCI(&mpm_ctx, (u_int8_t *)"/VideoAccessCodecInstall.exe", 28, 0, 0, 0, 0);
    wm_ctx->search_hash_size = HASH12_SIZE;
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"/VideoAccessCodecInstaLL.exe", 28);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 1)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch19Hash14 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCI(&mpm_ctx, (u_int8_t *)"/VideoAccessCodecInstall.exe", 28, 0, 0, 0, 0);
    wm_ctx->search_hash_size = HASH14_SIZE;
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"/VideoAccessCodecInstaLL.exe", 28);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 1)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch19Hash15 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCI(&mpm_ctx, (u_int8_t *)"/VideoAccessCodecInstall.exe", 28, 0, 0, 0, 0);
    wm_ctx->search_hash_size = HASH15_SIZE;
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"/VideoAccessCodecInstaLL.exe", 28);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 1)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch19Hash16 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCI(&mpm_ctx, (u_int8_t *)"/VideoAccessCodecInstall.exe", 28, 0, 0, 0, 0);
    wm_ctx->search_hash_size = HASH16_SIZE;
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"/VideoAccessCodecInstaLL.exe", 28);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 1)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch20 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"/videoaccesscodecinstall.exe", 28, 0, 0, 0, 0);
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"/VideoAccessCodecInstall.exe", 28);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 0)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch20Hash12 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"/videoaccesscodecinstall.exe", 28, 0, 0, 0, 0);
    wm_ctx->search_hash_size = HASH12_SIZE; /* force hash12 */
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"/VideoAccessCodecInstall.exe", 28);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 0)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch20Hash14 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"/videoaccesscodecinstall.exe", 28, 0, 0, 0, 0);
    wm_ctx->search_hash_size = HASH14_SIZE; /* force hash14 */
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"/VideoAccessCodecInstall.exe", 28);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 0)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch20Hash15 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"/videoaccesscodecinstall.exe", 28, 0, 0, 0, 0);
    wm_ctx->search_hash_size = HASH15_SIZE; /* force hash15 */
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"/VideoAccessCodecInstall.exe", 28);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 0)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch20Hash16 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"/videoaccesscodecinstall.exe", 28, 0, 0, 0, 0);
    wm_ctx->search_hash_size = HASH16_SIZE; /* force hash16 */
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"/VideoAccessCodecInstall.exe", 28);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 0)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

int WmTestSearch21 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);

    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"/videoaccesscodecinstall.exe", 28, 0, 0, 0, 0);
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);

    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"/videoaccesscodecinstall.exe", 28);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 1)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

static int WmTestSearch21Hash12 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"/videoaccesscodecinstall.exe", 28, 0, 0, 0, 0);
    wm_ctx->search_hash_size = HASH12_SIZE; /* force hash16 */
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);
    //WmPrintInfo(&mpm_ctx);
    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"/videoaccesscodecinstall.exe", 28);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 1)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

static int WmTestSearch21Hash14 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"/videoaccesscodecinstall.exe", 28, 0, 0, 0, 0);
    wm_ctx->search_hash_size = HASH14_SIZE; /* force hash16 */
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);
    //WmPrintInfo(&mpm_ctx);
    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"/videoaccesscodecinstall.exe", 28);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 1)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

static int WmTestSearch21Hash15 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"/videoaccesscodecinstall.exe", 28, 0, 0, 0, 0);
    wm_ctx->search_hash_size = HASH15_SIZE; /* force hash16 */
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);
    //WmPrintInfo(&mpm_ctx);
    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"/videoaccesscodecinstall.exe", 28);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 1)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

static int WmTestSearch21Hash16 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"/videoaccesscodecinstall.exe", 28, 0, 0, 0, 0);
    wm_ctx->search_hash_size = HASH16_SIZE; /* force hash16 */
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 1);
    //WmPrintInfo(&mpm_ctx);
    mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"/videoaccesscodecinstall.exe", 28);

    u_int32_t len = mpm_thread_ctx.match[0].len;

    MpmMatchCleanup(&mpm_thread_ctx);

    if (len == 1)
        result = 1;

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

static int WmTestSearch22Hash9 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"A", 1, 0, 0, 0, 0); /* should match 30 times */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AA", 2, 0, 0, 1, 0); /* should match 29 times */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AAA", 3, 0, 0, 2, 0); /* should match 28 times */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AAAAA", 5, 0, 0, 3, 0); /* 26 */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AAAAAAAAAA", 10, 0, 0, 4, 0); /* 21 */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", 30, 0, 0, 5, 0); /* 1 */
    /* total matches: 135 */

    wm_ctx->search_hash_size = HASH9_SIZE; /* force hash size */
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 6 /* 6 patterns */);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", 30);

    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 135)
        result = 1;
    else
        printf("135 != %u ",cnt);

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

static int WmTestSearch22Hash12 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"A", 1, 0, 0, 0, 0); /* should match 30 times */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AA", 2, 0, 0, 1, 0); /* should match 29 times */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AAA", 3, 0, 0, 2, 0); /* should match 28 times */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AAAAA", 5, 0, 0, 3, 0); /* 26 */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AAAAAAAAAA", 10, 0, 0, 4, 0); /* 21 */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", 30, 0, 0, 5, 0); /* 1 */
    /* total matches: 135 */

    wm_ctx->search_hash_size = HASH12_SIZE; /* force hash size */
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 6 /* 6 patterns */);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", 30);

    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 135)
        result = 1;
    else
        printf("135 != %u ",cnt);

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

static int WmTestSearch22Hash14 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"A", 1, 0, 0, 0, 0); /* should match 30 times */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AA", 2, 0, 0, 1, 0); /* should match 29 times */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AAA", 3, 0, 0, 2, 0); /* should match 28 times */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AAAAA", 5, 0, 0, 3, 0); /* 26 */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AAAAAAAAAA", 10, 0, 0, 4, 0); /* 21 */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", 30, 0, 0, 5, 0); /* 1 */
    /* total matches: 135 */

    wm_ctx->search_hash_size = HASH14_SIZE; /* force hash size */
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 6 /* 6 patterns */);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", 30);

    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 135)
        result = 1;
    else
        printf("135 != %u ",cnt);

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

static int WmTestSearch22Hash15 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"A", 1, 0, 0, 0, 0); /* should match 30 times */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AA", 2, 0, 0, 1, 0); /* should match 29 times */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AAA", 3, 0, 0, 2, 0); /* should match 28 times */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AAAAA", 5, 0, 0, 3, 0); /* 26 */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AAAAAAAAAA", 10, 0, 0, 4, 0); /* 21 */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", 30, 0, 0, 5, 0); /* 1 */
    /* total matches: 135 */

    wm_ctx->search_hash_size = HASH15_SIZE; /* force hash size */
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 6 /* 6 patterns */);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", 30);

    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 135)
        result = 1;
    else
        printf("135 != %u ",cnt);

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

static int WmTestSearch22Hash16 (void) {
    int result = 0;
    MpmCtx mpm_ctx;
    MpmThreadCtx mpm_thread_ctx;
    MpmInitCtx(&mpm_ctx, MPM_WUMANBER);
    WmCtx *wm_ctx = (WmCtx *)mpm_ctx.ctx;

    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"A", 1, 0, 0, 0, 0); /* should match 30 times */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AA", 2, 0, 0, 1, 0); /* should match 29 times */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AAA", 3, 0, 0, 2, 0); /* should match 28 times */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AAAAA", 5, 0, 0, 3, 0); /* 26 */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AAAAAAAAAA", 10, 0, 0, 4, 0); /* 21 */
    WmAddPatternCS(&mpm_ctx, (u_int8_t *)"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", 30, 0, 0, 5, 0); /* 1 */
    /* total matches: 135 */

    wm_ctx->search_hash_size = HASH16_SIZE; /* force hash size */
    WmPreparePatterns(&mpm_ctx);
    WmThreadInitCtx(&mpm_ctx, &mpm_thread_ctx, 6 /* 6 patterns */);

    u_int32_t cnt = mpm_ctx.Search(&mpm_ctx, &mpm_thread_ctx, NULL, (u_int8_t *)"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", 30);

    MpmMatchCleanup(&mpm_thread_ctx);

    if (cnt == 135)
        result = 1;
    else
        printf("135 != %u ",cnt);

    WmThreadDestroyCtx(&mpm_ctx, &mpm_thread_ctx);
    WmDestroyCtx(&mpm_ctx);
    return result;
}

void WmRegisterTests(void) {
    UtRegisterTest("WmTestInitCtx01", WmTestInitCtx01, 1);
    UtRegisterTest("WmTestInitCtx02", WmTestInitCtx02, 1);
    UtRegisterTest("WmTestInitCtx03", WmTestInitCtx03, 1);

    UtRegisterTest("WmTestThreadInitCtx01", WmTestThreadInitCtx01, 1);
    UtRegisterTest("WmTestThreadInitCtx02", WmTestThreadInitCtx02, 1);

    UtRegisterTest("WmTestInitAddPattern01", WmTestInitAddPattern01, 1);
    UtRegisterTest("WmTestInitAddPattern02", WmTestInitAddPattern02, 1);
    UtRegisterTest("WmTestInitAddPattern03", WmTestInitAddPattern03, 1);
    UtRegisterTest("WmTestInitAddPattern04", WmTestInitAddPattern04, 1);
    UtRegisterTest("WmTestInitAddPattern05", WmTestInitAddPattern05, 1);
    UtRegisterTest("WmTestInitAddPattern06", WmTestInitAddPattern06, 1);
    UtRegisterTest("WmTestInitAddPattern07", WmTestInitAddPattern07, 1);

    UtRegisterTest("WmTestPrepare01", WmTestPrepare01, 1);
    UtRegisterTest("WmTestPrepare02", WmTestPrepare02, 1);
    UtRegisterTest("WmTestPrepare03", WmTestPrepare03, 1);
    UtRegisterTest("WmTestPrepare04", WmTestPrepare01, 1);
    UtRegisterTest("WmTestPrepare05", WmTestPrepare02, 1);
    UtRegisterTest("WmTestPrepare06", WmTestPrepare03, 1);

    UtRegisterTest("WmTestSearch01", WmTestSearch01, 1);
    UtRegisterTest("WmTestSearch01Hash12", WmTestSearch01Hash12, 1);
    UtRegisterTest("WmTestSearch01Hash14", WmTestSearch01Hash14, 1);
    UtRegisterTest("WmTestSearch01Hash15", WmTestSearch01Hash15, 1);
    UtRegisterTest("WmTestSearch01Hash16", WmTestSearch01Hash16, 1);

    UtRegisterTest("WmTestSearch02", WmTestSearch02, 1);
    UtRegisterTest("WmTestSearch03", WmTestSearch03, 1);
    UtRegisterTest("WmTestSearch04", WmTestSearch04, 1);
    UtRegisterTest("WmTestSearch05", WmTestSearch05, 1);
    UtRegisterTest("WmTestSearch06", WmTestSearch06, 1);
    UtRegisterTest("WmTestSearch07", WmTestSearch07, 1);
    UtRegisterTest("WmTestSearch08", WmTestSearch08, 1);
    UtRegisterTest("WmTestSearch09", WmTestSearch09, 1);
    UtRegisterTest("WmTestSearch10", WmTestSearch10, 1);
    UtRegisterTest("WmTestSearch11", WmTestSearch11, 1);
    UtRegisterTest("WmTestSearch12", WmTestSearch12, 1);
    UtRegisterTest("WmTestSearch13", WmTestSearch13, 1);

    UtRegisterTest("WmTestSearch14", WmTestSearch14, 1);
    UtRegisterTest("WmTestSearch15", WmTestSearch15, 1);
    UtRegisterTest("WmTestSearch16", WmTestSearch16, 1);
    UtRegisterTest("WmTestSearch17", WmTestSearch17, 1);

    UtRegisterTest("WmTestSearch18", WmTestSearch18, 1);
    UtRegisterTest("WmTestSearch18Hash12", WmTestSearch18Hash12, 1);
    UtRegisterTest("WmTestSearch18Hash14", WmTestSearch18Hash14, 1);
    UtRegisterTest("WmTestSearch18Hash15", WmTestSearch18Hash15, 1);
    UtRegisterTest("WmTestSearch18Hash16", WmTestSearch18Hash16, 1);

    UtRegisterTest("WmTestSearch19", WmTestSearch19, 1);
    UtRegisterTest("WmTestSearch19Hash12", WmTestSearch19Hash12, 1);
    UtRegisterTest("WmTestSearch19Hash14", WmTestSearch19Hash14, 1);
    UtRegisterTest("WmTestSearch19Hash15", WmTestSearch19Hash15, 1);
    UtRegisterTest("WmTestSearch19Hash16", WmTestSearch19Hash16, 1);

    UtRegisterTest("WmTestSearch20", WmTestSearch20, 1);
    UtRegisterTest("WmTestSearch20Hash12", WmTestSearch20Hash12, 1);
    UtRegisterTest("WmTestSearch20Hash14", WmTestSearch20Hash14, 1);
    UtRegisterTest("WmTestSearch20Hash15", WmTestSearch20Hash15, 1);
    UtRegisterTest("WmTestSearch20Hash16", WmTestSearch20Hash16, 1);

    UtRegisterTest("WmTestSearch21", WmTestSearch21, 1);
    UtRegisterTest("WmTestSearch21Hash12", WmTestSearch21Hash12, 1);
    UtRegisterTest("WmTestSearch21Hash14", WmTestSearch21Hash14, 1);
    UtRegisterTest("WmTestSearch21Hash15", WmTestSearch21Hash15, 1);
    UtRegisterTest("WmTestSearch21Hash16", WmTestSearch21Hash16, 1);

    UtRegisterTest("WmTestSearch22Hash9", WmTestSearch22Hash9, 1);
    UtRegisterTest("WmTestSearch22Hash12", WmTestSearch22Hash12, 1);
    UtRegisterTest("WmTestSearch22Hash14", WmTestSearch22Hash14, 1);
    UtRegisterTest("WmTestSearch22Hash15", WmTestSearch22Hash15, 1);
    UtRegisterTest("WmTestSearch22Hash16", WmTestSearch22Hash16, 1);
}

