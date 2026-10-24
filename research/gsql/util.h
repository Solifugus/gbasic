#ifndef GSQL_RESEARCH_UTIL_H
#define GSQL_RESEARCH_UTIL_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t *v;
    size_t n, cap;
} U64s;
int u64_push(U64s *a, uint64_t x);
void u64_free(U64s *a);

/* An open-addressing set of page numbers (0 is never a member). */
typedef struct {
    uint64_t *slot;
    size_t cap, n;
} Set;
size_t set_hash(uint64_t x, size_t cap);
int set_has(const Set *s, uint64_t x);
int set_add(Set *s, uint64_t x);
void set_del(Set *s, uint64_t x);
void set_clear(Set *s);

typedef struct CPage {
    uint64_t pgno;
    uint64_t ver; /* which version of the page: 0 for an engine with one */
    unsigned char *buf;
    int dirty;
    int dead; /* freed in this operation: dropped when it ends */
    struct CPage *hnext, *prev, *next;
} CPage;

typedef struct {
    CPage **bucket;
    size_t nbucket, count;
    CPage lru; /* sentinel: lru.next is the most recently used */
} Cache;
void cache_init(Cache *c);
CPage *cache_find(Cache *c, uint64_t pgno, uint64_t ver);
CPage *cache_insert(Cache *c, uint64_t pgno, uint64_t ver, unsigned char *buf);
void cache_remove(Cache *c, CPage *p);
void cache_free(Cache *c);
void lru_unlink(CPage *p);
void lru_front(Cache *c, CPage *p);

#endif
