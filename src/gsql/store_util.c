/* Small containers shared by the candidates: page-number lists and sets, and
 * a page cache keyed by (page, version) with an LRU list. */

#include "store_util.h"

#include <stdlib.h>
#include <string.h>



int u64_push(U64s *a, uint64_t x) {
    if (a->n == a->cap) {
        size_t cap = a->cap ? a->cap * 2 : 16;
        uint64_t *v = realloc(a->v, cap * sizeof *v);
        if (!v) return 0;
        a->v = v;
        a->cap = cap;
    }
    a->v[a->n++] = x;
    return 1;
}
void u64_free(U64s *a) { free(a->v); memset(a, 0, sizeof *a); }


size_t set_hash(uint64_t x, size_t cap) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return (size_t)x & (cap - 1);
}
int set_has(const Set *s, uint64_t x) {
    if (!s->cap) return 0;
    for (size_t i = set_hash(x, s->cap);; i = (i + 1) & (s->cap - 1)) {
        if (s->slot[i] == x) return 1;
        if (s->slot[i] == 0) return 0;
    }
}
static int set_grow(Set *s) {
    Set t = {calloc(s->cap ? s->cap * 2 : 64, sizeof(uint64_t)), s->cap ? s->cap * 2 : 64, 0};
    if (!t.slot) return 0;
    for (size_t i = 0; i < s->cap; i++)
        if (s->slot[i] && s->slot[i] != UINT64_MAX) set_add(&t, s->slot[i]);
    free(s->slot);
    *s = t;
    return 1;
}
int set_add(Set *s, uint64_t x) {
    if ((s->n + 1) * 2 > s->cap && !set_grow(s)) return 0;
    size_t tomb = SIZE_MAX;
    for (size_t i = set_hash(x, s->cap);; i = (i + 1) & (s->cap - 1)) {
        if (s->slot[i] == x) return 1;
        if (s->slot[i] == UINT64_MAX && tomb == SIZE_MAX) tomb = i;
        if (s->slot[i] == 0) {
            s->slot[tomb != SIZE_MAX ? tomb : i] = x;
            s->n++;
            return 1;
        }
    }
}
void set_del(Set *s, uint64_t x) {
    if (!s->cap) return;
    for (size_t i = set_hash(x, s->cap);; i = (i + 1) & (s->cap - 1)) {
        if (s->slot[i] == x) { s->slot[i] = UINT64_MAX; return; } /* tombstone; n keeps counting it */
        if (s->slot[i] == 0) return;
    }
}
void set_clear(Set *s) { free(s->slot); memset(s, 0, sizeof *s); }

static size_t key_hash(uint64_t pgno, uint64_t ver, size_t cap) { return set_hash(pgno ^ (ver * 0x9E3779B97F4A7C15ULL), cap); }

/* ---- the page cache ------------------------------------------------------ */



void lru_unlink(CPage *p) { p->prev->next = p->next; p->next->prev = p->prev; }
void lru_front(Cache *c, CPage *p) {
    p->next = c->lru.next;
    p->prev = &c->lru;
    c->lru.next->prev = p;
    c->lru.next = p;
}
void cache_init(Cache *c) {
    memset(c, 0, sizeof *c);
    c->nbucket = 1024;
    c->bucket = calloc(c->nbucket, sizeof *c->bucket);
    c->lru.next = c->lru.prev = &c->lru;
}
CPage *cache_find(Cache *c, uint64_t pgno, uint64_t ver) {
    for (CPage *p = c->bucket[key_hash(pgno, ver, c->nbucket)]; p; p = p->hnext)
        if (p->pgno == pgno && p->ver == ver) return p;
    return NULL;
}
static void cache_rehash(Cache *c) {
    size_t nb = c->nbucket * 2;
    CPage **b = calloc(nb, sizeof *b);
    if (!b) return;
    for (size_t i = 0; i < c->nbucket; i++)
        for (CPage *p = c->bucket[i], *n; p; p = n) {
            n = p->hnext;
            size_t h = key_hash(p->pgno, p->ver, nb);
            p->hnext = b[h];
            b[h] = p;
        }
    free(c->bucket);
    c->bucket = b;
    c->nbucket = nb;
}
CPage *cache_insert(Cache *c, uint64_t pgno, uint64_t ver, unsigned char *buf) {
    if (c->count > c->nbucket) cache_rehash(c);
    CPage *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    p->pgno = pgno;
    p->ver = ver;
    p->buf = buf;
    size_t h = key_hash(pgno, ver, c->nbucket);
    p->hnext = c->bucket[h];
    c->bucket[h] = p;
    lru_front(c, p);
    c->count++;
    return p;
}
void cache_remove(Cache *c, CPage *p) {
    CPage **pp = &c->bucket[key_hash(p->pgno, p->ver, c->nbucket)];
    while (*pp != p) pp = &(*pp)->hnext;
    *pp = p->hnext;
    lru_unlink(p);
    free(p->buf);
    free(p);
    c->count--;
}
void cache_free(Cache *c) {
    while (c->lru.next != &c->lru) cache_remove(c, c->lru.next);
    free(c->bucket);
}

