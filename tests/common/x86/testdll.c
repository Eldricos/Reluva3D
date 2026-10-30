/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 The Reluva contributors
 *
 * Code for the x86 interpreter's tests: built as a 32-bit Windows DLL (by
 * build_testdll.sh, with clang, for the x87 and without SSE as the games'
 * compilers did) and natively; each test's results must agree.
 */

#include <stdint.h>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

#ifdef TESTDLL_PE
/* No C library in the DLL: the compiler may still call these. */
void* memcpy(void* to, const void* from, unsigned n) {
    unsigned char* t = to;
    const unsigned char* f = from;
    while (n--)
        *t++ = *f++;
    return to;
}
void* memset(void* to, int v, unsigned n) {
    unsigned char* t = to;
    while (n--)
        *t++ = (unsigned char)v;
    return to;
}
__declspec(dllimport) void* __stdcall HeapAlloc(void* heap, unsigned flags, unsigned size);
__declspec(dllimport) void* __stdcall GetProcessHeap(void);
__declspec(dllimport) unsigned __stdcall GetVersion(void);
int _fltused = 0x9875; /* the compiler marks floating point use with it */
static unsigned attached;
int __stdcall DllMain(void* instance, unsigned reason, void* reserved) {
    (void)instance;
    (void)reserved;
    if (reason == 1)
        attached = GetVersion();
    return 1;
}
EXPORT unsigned testdll_attached(void) { return attached; }
EXPORT unsigned testdll_heap(unsigned size) {
    unsigned char* p = HeapAlloc(GetProcessHeap(), 8, size);
    unsigned sum = 0, i;
    for (i = 0; i < size; ++i)
        p[i] = (unsigned char)(i * 7);
    for (i = 0; i < size; ++i)
        sum += p[i];
    return sum;
}
#endif

static uint32_t crc32(const unsigned char* p, int n) {
    uint32_t c = 0xffffffffu;
    int i, k;
    for (i = 0; i < n; ++i) {
        c ^= p[i];
        for (k = 0; k < 8; ++k)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}

static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

static void sort(int32_t* v, int n) {
    int i, j;
    for (i = 1; i < n; ++i) {
        int32_t x = v[i];
        for (j = i - 1; j >= 0 && v[j] > x; --j)
            v[j + 1] = v[j];
        v[j + 1] = x;
    }
}

static int format(char* out, int value, unsigned hex) {
    char tmp[16];
    int n = 0, len = 0, neg = value < 0;
    unsigned u = neg ? 0u - (unsigned)value : (unsigned)value;
    do {
        tmp[n++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (neg)
        out[len++] = '-';
    while (n)
        out[len++] = tmp[--n];
    out[len++] = ' ';
    do {
        out[len++] = "0123456789abcdef"[hex >> 28];
        hex <<= 4;
    } while (hex);
    out[len] = 0;
    return len;
}

typedef uint32_t (*Op)(uint32_t, uint32_t);
static uint32_t opAdd(uint32_t a, uint32_t b) { return a + b; }
static uint32_t opXor(uint32_t a, uint32_t b) { return a ^ b; }
static uint32_t opRot(uint32_t a, uint32_t b) { return (a << (b & 31)) | (a >> ((32 - (b & 31)) & 31)); }
static Op ops[3] = {opAdd, opXor, opRot};

static uint32_t bits(double d) {
    union {
        float f;
        uint32_t u;
    } x;
    x.f = (float)d;
    return x.u;
}

/* Test `which` on a and b: an integer result (floats as their bits). */
EXPORT uint32_t testdll_run(int which, uint32_t a, uint32_t b) {
    int32_t sa = (int32_t)a, sb = (int32_t)b;
    switch (which) {
    case 0: return a * b + (a >> 3) - (b << 5);
    case 1: return sb && sb != -1 ? (uint32_t)(sa / sb) : 0;
    case 2: return sb && sb != -1 ? (uint32_t)(sa % sb) : 0;
    case 3: return b ? a / b + a % b : 0;
    case 4: return (uint32_t)((uint64_t)a * b >> 32);
    case 5: return (uint32_t)(((int64_t)sa * sb) >> 17);
    case 6: return (uint32_t)(((uint64_t)a << 32 | b) >> (a & 63));
    case 7: return (uint32_t)(sa >> (b & 31));
    case 8: return (uint32_t)(sa < sb) | (uint32_t)(a < b) << 1 | (uint32_t)(sa == sb) << 2 | (uint32_t)(sa >= sb) << 3;
    case 9: {
        unsigned char buf[64];
        int i;
        for (i = 0; i < 64; ++i)
            buf[i] = (unsigned char)(a + i * b);
        return crc32(buf, (int)(b % 64));
    }
    case 10: return (uint32_t)fib((int)(a % 20));
    case 11: {
        int32_t v[16];
        uint32_t s = a | 1, h = 0;
        int i;
        for (i = 0; i < 16; ++i) {
            s = s * 1103515245u + 12345u + b;
            v[i] = (int32_t)s;
        }
        sort(v, 16);
        for (i = 0; i < 16; ++i)
            h = h * 31 + (uint32_t)v[i];
        return h;
    }
    case 12: {
        char buf[48];
        int len = format(buf, sa, b);
        return crc32((const unsigned char*)buf, len) ^ (uint32_t)len;
    }
    case 13: return ops[a % 3](a, b);
    case 14: { /* float arithmetic */
        double x = (double)sa / 1000.0, y = (double)sb / 977.0;
        return bits(x * y + x / (y == 0.0 ? 1.0 : y) - y * 0.5);
    }
    case 15: { /* conversions, truncating as C does */
        double x = (double)sa / 7.0 + (double)b / 3.0;
        return (uint32_t)(int32_t)x ^ (uint32_t)(int64_t)(x * 1000.0);
    }
    case 16: { /* comparisons */
        float x = (float)sa * 0.25f, y = (float)sb * 0.5f;
        return (uint32_t)(x < y) | (uint32_t)(x <= y) << 1 | (uint32_t)(x == y) << 2 | (uint32_t)(x > y) << 3;
    }
    case 17: { /* a series, at double precision */
        double s = 0.0, t = 1.0;
        int i;
        for (i = 1; i < 40; ++i) {
            t *= (double)(a % 5 + 1) / i;
            s += t;
        }
        return bits(s) ^ (uint32_t)(s * 1e6);
    }
    case 18: { /* unsigned to float and back */
        volatile float f = (float)a; /* stored, so rounded to a float on the x87 too */
        double d = (double)b;
        return (uint32_t)((double)f / 3.0) + (uint32_t)(d * 0.75); /* in double: the x87 keeps float division wider than SSE */
    }
    case 19: { /* switch of a float's sign, abs, neg */
        double x = (double)sa - (double)sb;
        double m = x < 0 ? -x : x;
        return bits(-m) ^ bits(x * x);
    }
    }
    return 0xdeadbeef;
}
EXPORT int testdll_count(void) { return 20; }
