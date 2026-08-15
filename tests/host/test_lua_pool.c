/* test_lua_pool.c — host unit tests for the extracted static pool
 * (interfaces/lua_pool_if.h). Includes the >64 KB offset regression that
 * the pre-2026-08-16 uint16-offset allocator would have failed. */
#include "unity.h"
#include "lua_pool_if.h"
#include <string.h>
#include <stdint.h>

#define N_BIG   64
#define N_SMALL 40

/* pool sizes stay < 128 KB, uint32 casts are safe */
#define ASSERT_EQ_SZ(a, b) \
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(a), (uint32_t)(b))

static void *big[N_BIG];
static void *small_blk[N_SMALL];

static void fill(void *p, size_t n, uint8_t v)
{
    memset(p, v, n);
}

static int check_fill(void *p, size_t n, uint8_t v)
{
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; i++) {
        if (b[i] != v) {
            return 0;
        }
    }
    return 1;
}

static void test_pool_alloc_free_basic(void)
{
    lua_pool_init();
    ASSERT_EQ_SZ(0, lua_pool_used());

    void *p = lua_pool_alloc(100);
    TEST_ASSERT_NOT_NULL(p);
    size_t charge = lua_pool_charge(p);
    ASSERT_EQ_SZ(charge, lua_pool_used());
    TEST_ASSERT_TRUE(charge >= 100 + lua_pool_header_size());

    lua_pool_free(p);
    ASSERT_EQ_SZ(0, lua_pool_used());
    ASSERT_EQ_SZ(charge, lua_pool_peak());
}

static void test_pool_alignment(void)
{
    lua_pool_init();
    for (size_t s = 1; s <= 33; s += 7) {
        void *p = lua_pool_alloc(s);
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)((uintptr_t)p & 3));
        ASSERT_EQ_SZ(0, lua_pool_charge(p) & 3);
        lua_pool_free(p);
    }
    ASSERT_EQ_SZ(0, lua_pool_used());
}

static void test_pool_reuse_same_block(void)
{
    lua_pool_init();
    void *p = lua_pool_alloc(128);
    lua_pool_free(p);
    void *q = lua_pool_alloc(128);
    TEST_ASSERT_EQUAL_PTR(p, q);
    lua_pool_free(q);
}

static void test_pool_coalescing(void)
{
    lua_pool_init();
    void *a = lua_pool_alloc(64);
    void *b = lua_pool_alloc(64);
    void *c = lua_pool_alloc(64);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);

    lua_pool_free(b);
    lua_pool_free(a);   /* a must merge with the adjacent free b */

    /* a+b merged usable size = 64 + hdr + 64; a single 136-byte request
     * only fits if coalescing happened */
    void *m = lua_pool_alloc(64 + lua_pool_header_size() + 64);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_PTR(a, m);

    lua_pool_free(m);
    lua_pool_free(c);
    ASSERT_EQ_SZ(0, lua_pool_used());
}

static void test_pool_top_shrink(void)
{
    lua_pool_init();
    void *x = lua_pool_alloc(16);
    void *y = lua_pool_alloc(16);
    TEST_ASSERT_NOT_NULL(x);
    TEST_ASSERT_NOT_NULL(y);
    size_t used_x_only = lua_pool_charge(x);

    lua_pool_free(y);   /* top block returns to the bump pointer */
    ASSERT_EQ_SZ(used_x_only, lua_pool_used());

    void *z = lua_pool_alloc(16);
    TEST_ASSERT_EQUAL_PTR(y, z);  /* bump re-uses the returned top */
    lua_pool_free(z);
    lua_pool_free(x);
    ASSERT_EQ_SZ(0, lua_pool_used());
}

static void test_pool_oom(void)
{
    lua_pool_init();
    void *p = lua_pool_alloc(LUA_MEMORY_LIMIT);
    TEST_ASSERT_NULL(p);  /* single block can't exceed the whole pool+hdrs */

    size_t total = 0;
    while (total < LUA_MEMORY_LIMIT) {
        void *q = lua_pool_alloc(1024);
        if (q == NULL) {
            break;
        }
        total += lua_pool_charge(q);
    }
    TEST_ASSERT_TRUE(total <= LUA_MEMORY_LIMIT);
    TEST_ASSERT_NULL(lua_pool_alloc(1024));
    lua_pool_init();
    ASSERT_EQ_SZ(0, lua_pool_used());
}

/* Regression: the pre-extraction allocator used uint16 offsets and silently
 * truncated past 64 KB. Push the bump pointer well past 65536 and verify
 * data integrity across frees and re-allocs. */
static void test_pool_above_64k(void)
{
    lua_pool_init();

    for (int i = 0; i < N_BIG; i++) {
        big[i] = lua_pool_alloc(1024);
        TEST_ASSERT_NOT_NULL(big[i]);
        fill(big[i], 1024, (uint8_t)(0xA0 + i));
    }
    /* 64 * (1024 + 8) = 66048 > 65536: old uint16 offsets would truncate */
    TEST_ASSERT_TRUE(lua_pool_used() > 65536);

    for (int i = 0; i < N_BIG; i += 2) {
        lua_pool_free(big[i]);
    }

    for (int i = 0; i < N_SMALL; i++) {
        small_blk[i] = lua_pool_alloc(256);
        TEST_ASSERT_NOT_NULL(small_blk[i]);
        fill(small_blk[i], 256, (uint8_t)(0x40 + i));
    }

    /* survivors intact */
    for (int i = 1; i < N_BIG; i += 2) {
        TEST_ASSERT_TRUE(check_fill(big[i], 1024, (uint8_t)(0xA0 + i)));
    }
    /* new blocks intact */
    for (int i = 0; i < N_SMALL; i++) {
        TEST_ASSERT_TRUE(check_fill(small_blk[i], 256, (uint8_t)(0x40 + i)));
    }

    for (int i = 1; i < N_BIG; i += 2) {
        lua_pool_free(big[i]);
    }
    for (int i = 0; i < N_SMALL; i++) {
        lua_pool_free(small_blk[i]);
    }
    ASSERT_EQ_SZ(0, lua_pool_used());
}

static void test_pool_realloc_grow_preserves(void)
{
    lua_pool_init();
    uint8_t *p = lua_pool_alloc(32);
    TEST_ASSERT_NOT_NULL(p);
    for (int i = 0; i < 32; i++) {
        p[i] = (uint8_t)i;
    }
    uint8_t *q = lua_pool_realloc(p, 200);
    TEST_ASSERT_NOT_NULL(q);
    for (int i = 0; i < 32; i++) {
        TEST_ASSERT_EQUAL_UINT8(i, q[i]);
    }
    lua_pool_free(q);
    ASSERT_EQ_SZ(0, lua_pool_used());
}

static void test_pool_foreign_free_ignored(void)
{
    lua_pool_init();
    void *p = lua_pool_alloc(64);
    size_t used = lua_pool_used();
    int stack_var = 0;
    lua_pool_free(NULL);
    lua_pool_free(&stack_var);
    ASSERT_EQ_SZ(used, lua_pool_used());
    lua_pool_free(p);
    ASSERT_EQ_SZ(0, lua_pool_used());
}

static void test_pool_peak_monotonic(void)
{
    lua_pool_init();
    void *p = lua_pool_alloc(512);
    size_t peak1 = lua_pool_peak();
    lua_pool_free(p);
    ASSERT_EQ_SZ(peak1, lua_pool_peak());
    TEST_ASSERT_TRUE(lua_pool_peak() >= lua_pool_used());
}

int test_lua_pool_main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pool_alloc_free_basic);
    RUN_TEST(test_pool_alignment);
    RUN_TEST(test_pool_reuse_same_block);
    RUN_TEST(test_pool_coalescing);
    RUN_TEST(test_pool_top_shrink);
    RUN_TEST(test_pool_oom);
    RUN_TEST(test_pool_above_64k);
    RUN_TEST(test_pool_realloc_grow_preserves);
    RUN_TEST(test_pool_foreign_free_ignored);
    RUN_TEST(test_pool_peak_monotonic);
    return UNITY_END();
}
