// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Reluva's MSVCP60.dll: the parts of Visual C++ 6's C++ library that the
// games we run import: string, the skeleton of locales, the stream classes
// over FILEs and memory, and a few exceptions. It's built as a real 32-bit
// DLL (build_msvcp60.sh) and loaded into the emulated process in place of
// Microsoft's, because the games' own code derives from these classes,
// builds them inline and reads their fields, so they must be laid out as
// Visual C++ 6's were.
//
// How it was written: each function does what the C++ standard (ISO/IEC
// 14882:1998) says it does. Each class's size, and the places of the fields
// the games' code touches, were measured from that code: the offsets it
// reads and writes, what its inline constructors set up, the order of the
// vftables it builds. They're listed in docs/engines/alphateam.md. The rest
// of each class is ours. The exported names are the ones the games import,
// mapped to these functions by msvcp60.def. No part of Microsoft's library
// was looked at or used.
//
// Built freestanding: no headers, and no library but MSVCRT, which it
// imports (Reluva's MSVCRT stands in for that).

typedef unsigned int size_t;

#define THISCALL __attribute__((thiscall))
#define EXPORT extern "C"

// --- MSVCRT -------------------------------------------------------------------------

struct FILE;
extern "C" {
__declspec(dllimport) void free(void*);
__declspec(dllimport) void* memcpy(void*, const void*, size_t);
__declspec(dllimport) void* memmove(void*, const void*, size_t);
__declspec(dllimport) int memcmp(const void*, const void*, size_t);
__declspec(dllimport) size_t strlen(const char*);
__declspec(dllimport) int sprintf(char*, const char*, ...);
__declspec(dllimport) FILE* fopen(const char*, const char*);
__declspec(dllimport) int fclose(FILE*);
__declspec(dllimport) size_t fread(void*, size_t, size_t, FILE*);
__declspec(dllimport) size_t fwrite(const void*, size_t, size_t, FILE*);
__declspec(dllimport) int fseek(FILE*, long, int);
__declspec(dllimport) long ftell(FILE*);
__declspec(dllimport) int fflush(FILE*);
__declspec(dllimport) int fgetc(FILE*);
__declspec(dllimport) int fputc(int, FILE*);
__declspec(dllimport) int ungetc(int, FILE*);
__declspec(dllimport) int fputs(const char*, FILE*);
__declspec(dllimport) void abort();
__declspec(dllimport) extern char _iob[]; // stdin, stdout, stderr: 32 bytes each
}
// The compiler's mark that floating point is used, which MSVCRT would define.
extern "C" int _fltused = 0;
__declspec(dllimport) void* operator new(size_t);
__declspec(dllimport) void operator delete(void*);
inline void* operator new(size_t, void* p) { return p; }

namespace {

constexpr int kEof = -1;
FILE* stdErr() { return (FILE*)(_iob + 2 * 32); }

// Ends the program over a misuse the library would throw an exception for;
// exceptions from here aren't supported.
[[noreturn]] void fail(const char* what) {
    fputs("MSVCP60: ", stdErr());
    fputs(what, stdErr());
    fputs("\n", stdErr());
    abort();
    for (;;) {
    }
}

// Byte and boolean arguments arrive in a whole stack slot; only the low
// byte is meant.
inline bool flag(unsigned x) { return (x & 0xff) != 0; }
inline char byte(unsigned x) { return char(x & 0xff); }
inline size_t least(size_t a, size_t b) { return a < b ? a : b; }

// The vftable entry `index` of a polymorphic object.
template <class F>
F slot(const void* object, int index) {
    return (F)(*(const void* const* const*)object)[index];
}

} // namespace

// --- Locales ------------------------------------------------------------------------
// Only the "C" locale, with one facet: ctype<char>. A facet is its vftable
// and a reference count (+4), which the games' inline code increments
// itself; a locale is a pointer to its implementation, also a facet.

struct Facet {
    const void* const* vf;
    size_t refs;
};
struct Locale {
    Facet* imp;
};
struct Ctype { // ctype<char>: 0x18 bytes, as the games build it inline
    Facet facet;
    unsigned long handle; // +8
    unsigned page;        // +0xc
    const short* table;   // +0x10: each byte's classification
    int deleteTable;      // +0x14: whether to free the table
};
struct CtypeVec { // what _Getctype returns, copied to Ctype +8 by the games
    unsigned long handle;
    unsigned page;
    const short* table;
    int deleteTable;
};
struct LocaleId {
    size_t id;
};
static_assert(sizeof(Ctype) == 0x18 && sizeof(CtypeVec) == 16, "ctype layout");

namespace {

// The C runtime's classification bits.
enum : short {
    kUpper = 1, kLower = 2, kDigit = 4, kSpace = 8, kPunct = 0x10, kControl = 0x20, kBlank = 0x40, kXdigit = 0x80,
    kAlpha = 0x100
};
short gTable[256]; // the "C" locale's, filled in at start-up

void buildTable() {
    for (int c = 0; c < 256; ++c) {
        short m = 0;
        if (c < 0x20 || c == 0x7f)
            m |= kControl;
        if ((c >= 9 && c <= 13) || c == ' ')
            m |= kSpace;
        if (c == ' ')
            m |= kBlank;
        if (c >= '0' && c <= '9')
            m |= kDigit | kXdigit;
        if (c >= 'A' && c <= 'Z')
            m |= kUpper | kAlpha;
        if (c >= 'a' && c <= 'z')
            m |= kLower | kAlpha;
        if ((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))
            m |= kXdigit;
        if (c > ' ' && c < 0x7f && !(m & (kDigit | kAlpha)))
            m |= kPunct;
        gTable[c] = m;
    }
}

inline bool isSpace(int c) { return c >= 0 && c < 256 && (gTable[c] & kSpace); }

THISCALL void* facetDeleting(Facet* self, unsigned flags) {
    if (flags & 1)
        operator delete(self);
    return self;
}
THISCALL void* ctypeDeleting(Ctype* self, unsigned flags) {
    if (self->deleteTable > 0)
        free((void*)self->table);
    if (flags & 1)
        operator delete(self);
    return self;
}
THISCALL void* locimpDeleting(Facet* self, unsigned) { return self; } // the one there is lives on

} // namespace

EXPORT THISCALL const char* ctypeToLowerRange(const Ctype*, char* first, const char* last) {
    for (; first != last; ++first)
        if (*first >= 'A' && *first <= 'Z')
            *first = char(*first - 'A' + 'a');
    return last;
}
EXPORT THISCALL char ctypeToLower(const Ctype*, unsigned c) {
    const char ch = byte(c);
    return ch >= 'A' && ch <= 'Z' ? char(ch - 'A' + 'a') : ch;
}
EXPORT THISCALL const char* ctypeToUpperRange(const Ctype*, char* first, const char* last) {
    for (; first != last; ++first)
        if (*first >= 'a' && *first <= 'z')
            *first = char(*first - 'a' + 'A');
    return last;
}
EXPORT THISCALL char ctypeToUpper(const Ctype*, unsigned c) {
    const char ch = byte(c);
    return ch >= 'a' && ch <= 'z' ? char(ch - 'a' + 'A') : ch;
}

EXPORT const void* const facetVftable[] = {(const void*)facetDeleting};
EXPORT const void* const ctypeBaseVftable[] = {(const void*)facetDeleting};
// In the order the games' own copies have: the destructor, then do_tolower
// and do_toupper, each for a range and then for a character.
EXPORT const void* const ctypeVftable[] = {(const void*)ctypeDeleting, (const void*)ctypeToLowerRange,
                                           (const void*)ctypeToLower, (const void*)ctypeToUpperRange,
                                           (const void*)ctypeToUpper};

namespace {
const void* const locimpVftable[] = {(const void*)locimpDeleting};
Facet gLocimp = {locimpVftable, 1};               // the global locale's; its own reference keeps it
Ctype gCtype = {{ctypeVftable, 1}, 0, 0, gTable, 0}; // its ctype<char>
} // namespace

EXPORT Facet* localeGlobal = &gLocimp;      // locale::_Locimp::_Global
EXPORT int localeIdCount = 1;               // locale::id::_Id_cnt: ctype<char> took 1
EXPORT LocaleId ctypeId = {1};              // ctype<char>::id
EXPORT const short* ctypeClassicTable = 0;  // ctype<char>::_Cltab, which the games set

EXPORT THISCALL void facetIncref(Facet* self) {
    if (self->refs < size_t(-1))
        ++self->refs;
}
EXPORT THISCALL Facet* facetDecref(Facet* self) {
    if (self->refs > 0 && self->refs < size_t(-1))
        --self->refs;
    return self->refs == 0 ? self : 0;
}
EXPORT THISCALL void ctypeBaseDestroy(Facet* self) { self->vf = facetVftable; }
EXPORT THISCALL void ctypeDestroy(Ctype* self) {
    if (self->deleteTable > 0)
        free((void*)self->table);
    self->deleteTable = 0;
    ctypeBaseDestroy(&self->facet);
}
EXPORT void ctypeTerm() {} // the table _Cltab takes is ours and lives on

// _Getctype: the "C" locale's classification table, not to be freed.
EXPORT CtypeVec getCtype() { return CtypeVec{0, 0, gTable, 0}; }

EXPORT Facet* localeInit() { return localeGlobal; } // the caller counts its reference
EXPORT THISCALL Locale* localeConstruct(Locale* self) {
    self->imp = localeInit();
    facetIncref(self->imp);
    return self;
}
EXPORT THISCALL void localeDestroy(Locale* self) {
    if (self->imp)
        if (Facet* gone = facetDecref(self->imp))
            slot<void*(THISCALL*)(Facet*, unsigned)>(gone, 0)(gone, 1);
}
EXPORT THISCALL bool localeIsClassic(const Locale*) { return true; }
// _Getfacet: ctype<char> is the one facet there is.
EXPORT THISCALL const Facet* localeGetFacet(const Locale*, size_t id, unsigned) {
    return id == ctypeId.id ? &gCtype.facet : 0;
}

namespace {
void localeCopy(Locale* self, const Locale* from) {
    self->imp = from->imp;
    facetIncref(self->imp);
}
void localeAssign(Locale* self, const Locale* from) {
    if (self->imp == from->imp)
        return;
    Locale old = *self;
    localeCopy(self, from);
    localeDestroy(&old);
}
} // namespace

// _Locinfo: it names the locale whose parts _Getctype and its kind return;
// only "C" is there.
struct Locinfo {
    char unused[0x40];
};
EXPORT THISCALL Locinfo* locinfoConstruct(Locinfo* self, const char*) { return self; }
EXPORT THISCALL void locinfoDestroy(Locinfo*) {}

// _Lockit guards the library's shared state between threads; the emulated
// process runs one thread at a time, so it has nothing to do yet.
struct Lockit {
    char unused;
};
EXPORT THISCALL Lockit* lockitConstruct(Lockit* self) { return self; }
EXPORT THISCALL void lockitDestroy(Lockit*) {}

// --- string ---------------------------------------------------------------------------
// 16 bytes: the allocator (+0), the characters (+4), the length (+8) and
// the capacity (+12). The byte before the characters counts the other
// strings sharing them; 0 means none, 255 frozen (handed out by a non-const
// begin() or operator[], so never shared again). The games' inline code
// uses _Grow, _Copy, _Eos, _Split, _Freeze, _Refcnt and _Tidy as below.
// These functions never share, so the count stays 0 unless a string is
// frozen.

struct String {
    char allocator;
    char* ptr;
    size_t len;
    size_t res;
};
static_assert(sizeof(String) == 16, "string layout");

namespace {
constexpr size_t kNpos = size_t(-1);
constexpr unsigned char kFrozen = 255;
constexpr size_t kMinCapacity = 31;
constexpr size_t kMaxSize = size_t(-1) - 2;
inline unsigned char& refs(char* p) { return ((unsigned char*)p)[-1]; }
inline bool shared(const String* s) { return s->ptr && refs(s->ptr) != 0 && refs(s->ptr) != kFrozen; }
} // namespace

EXPORT const size_t stringNpos = kNpos;
EXPORT const char stringEmpty = 0; // _Nullstr's: what c_str() gives with no characters

EXPORT void stringTooLong() { fail("string too long (length_error)"); }      // _Xlen
EXPORT void stringBadPosition() { fail("invalid string position (out_of_range)"); } // _Xran

EXPORT THISCALL unsigned char* stringRefcnt(String*, char* p) { return &refs(p); }
EXPORT THISCALL void stringEos(String* self, size_t n) {
    self->len = n;
    self->ptr[n] = 0;
}
EXPORT THISCALL void stringTidy(String* self, unsigned built) {
    if (flag(built) && self->ptr) {
        if (shared(self))
            --refs(self->ptr);
        else
            operator delete(self->ptr - 1);
    }
    self->ptr = 0;
    self->len = 0;
    self->res = 0;
}
// _Copy: new characters with room for at least n, keeping what fits.
EXPORT THISCALL void stringCopy(String* self, size_t n) {
    size_t capacity = n | kMinCapacity;
    if (capacity > kMaxSize)
        capacity = n;
    char* block = (char*)operator new(capacity + 2);
    const size_t keep = least(self->len, capacity);
    if (keep)
        memcpy(block + 1, self->ptr, keep);
    stringTidy(self, 1);
    self->ptr = block + 1;
    refs(self->ptr) = 0;
    self->res = capacity;
    stringEos(self, keep);
}
// _Grow: makes the characters this string's own with room for n, keeping
// those there (trimming the room if `trim`). False if n is 0, when the
// string is left empty.
EXPORT THISCALL bool stringGrow(String* self, size_t n, unsigned trim) {
    if (n > kMaxSize)
        stringTooLong();
    if (shared(self)) {
        if (n == 0) {
            --refs(self->ptr);
            stringTidy(self, 0);
            return false;
        }
        stringCopy(self, n);
        return true;
    }
    if (n == 0) {
        if (flag(trim))
            stringTidy(self, 1);
        else if (self->ptr)
            stringEos(self, 0);
        return false;
    }
    if (flag(trim) && (self->res > kMinCapacity || self->res < n)) {
        stringTidy(self, 1);
        stringCopy(self, n);
    } else if (!flag(trim) && self->res < n) {
        stringCopy(self, n);
    }
    return true;
}
// _Split: unshares the characters.
EXPORT THISCALL void stringSplit(String* self) {
    if (!shared(self))
        return;
    char* old = self->ptr;
    const size_t n = self->len;
    self->ptr = 0;
    self->len = 0;
    self->res = 0;
    if (stringGrow(self, n, 1)) {
        memcpy(self->ptr, old, n);
        stringEos(self, n);
    }
    --refs(old);
}
EXPORT THISCALL void stringFreeze(String* self) {
    if (shared(self))
        stringSplit(self);
    if (self->ptr)
        refs(self->ptr) = kFrozen;
}
EXPORT THISCALL size_t stringMaxSize(const String*) { return kMaxSize; }

EXPORT THISCALL String* stringConstruct(String* self, const void*) {
    stringTidy(self, 0);
    return self;
}
EXPORT THISCALL void stringConstructDefault(String* self) { stringTidy(self, 0); } // `default ctor closure'
EXPORT THISCALL void stringDestroy(String* self) { stringTidy(self, 1); }

EXPORT THISCALL String* stringAssign(String* self, const char* s, size_t n) {
    if (self->ptr && s >= self->ptr && s < self->ptr + self->len) { // part of itself
        const size_t at = size_t(s - self->ptr);
        stringSplit(self);
        memmove(self->ptr, self->ptr + at, n);
        stringEos(self, n);
        return self;
    }
    if (stringGrow(self, n, 1)) {
        memcpy(self->ptr, s, n);
        stringEos(self, n);
    }
    return self;
}
EXPORT THISCALL String* stringErase(String* self, size_t at, size_t n) {
    if (at > self->len)
        stringBadPosition();
    stringSplit(self);
    const size_t m = least(n, self->len - at);
    if (m == 0)
        return self;
    memmove(self->ptr + at, self->ptr + at + m, self->len - at - m);
    const size_t length = self->len - m;
    if (stringGrow(self, length, 0))
        stringEos(self, length);
    return self;
}
EXPORT THISCALL String* stringAssignPart(String* self, const String* from, size_t at, size_t n) {
    if (at > from->len)
        stringBadPosition();
    const size_t m = least(n, from->len - at);
    if (self == from) {
        stringErase(self, at + m, kNpos);
        stringErase(self, 0, at);
        return self;
    }
    if (stringGrow(self, m, 1)) {
        memcpy(self->ptr, from->ptr + at, m);
        stringEos(self, m);
    }
    return self;
}
EXPORT THISCALL String* stringConstructCopy(String* self, const String* from) {
    stringTidy(self, 0);
    return stringAssignPart(self, from, 0, kNpos);
}
EXPORT THISCALL String* stringConstructChars(String* self, const char* s, const void*) {
    stringTidy(self, 0);
    return stringAssign(self, s, strlen(s));
}
EXPORT THISCALL String* stringAppend(String* self, const char* s, size_t n) {
    if (kNpos - self->len <= n)
        stringTooLong();
    const bool inside = self->ptr && s >= self->ptr && s < self->ptr + self->len;
    const size_t at = inside ? size_t(s - self->ptr) : 0;
    const size_t length = self->len + n;
    if (n && stringGrow(self, length, 0)) {
        memmove(self->ptr + self->len, inside ? self->ptr + at : s, n);
        stringEos(self, length);
    }
    return self;
}
EXPORT THISCALL String* stringAppendPart(String* self, const String* from, size_t at, size_t n) {
    if (at > from->len)
        stringBadPosition();
    const size_t m = least(n, from->len - at);
    return m ? stringAppend(self, from->ptr + at, m) : self;
}
EXPORT THISCALL String* stringAppendFill(String* self, size_t n, unsigned c) {
    if (kNpos - self->len <= n)
        stringTooLong();
    const size_t length = self->len + n;
    if (n && stringGrow(self, length, 0)) {
        for (size_t i = self->len; i < length; ++i)
            self->ptr[i] = byte(c);
        stringEos(self, length);
    }
    return self;
}

EXPORT THISCALL String* stringSubstr(const String* self, String* out, size_t at, size_t n) {
    stringTidy(out, 0);
    stringAssignPart(out, self, at, n);
    return out;
}

namespace {
inline bool among(char c, const char* set, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (set[i] == c)
            return true;
    return false;
}
} // namespace

EXPORT THISCALL size_t stringFind(const String* self, const char* s, size_t at, size_t n) {
    if (n == 0 && at <= self->len)
        return at;
    if (at >= self->len || n > self->len - at)
        return kNpos;
    for (size_t i = at; i + n <= self->len; ++i)
        if (memcmp(self->ptr + i, s, n) == 0)
            return i;
    return kNpos;
}
EXPORT THISCALL size_t stringRfind(const String* self, const char* s, size_t at, size_t n) {
    if (n == 0)
        return least(at, self->len);
    if (n > self->len)
        return kNpos;
    for (size_t i = least(at, self->len - n) + 1; i-- > 0;)
        if (memcmp(self->ptr + i, s, n) == 0)
            return i;
    return kNpos;
}
EXPORT THISCALL size_t stringFindFirstOf(const String* self, const char* s, size_t at, size_t n) {
    if (n > 0)
        for (size_t i = at; i < self->len; ++i)
            if (among(self->ptr[i], s, n))
                return i;
    return kNpos;
}
EXPORT THISCALL size_t stringFindFirstNotOf(const String* self, const char* s, size_t at, size_t n) {
    for (size_t i = at; i < self->len; ++i)
        if (!among(self->ptr[i], s, n))
            return i;
    return kNpos;
}
EXPORT THISCALL size_t stringFindLastOf(const String* self, const char* s, size_t at, size_t n) {
    if (n > 0 && self->len > 0)
        for (size_t i = least(at, self->len - 1) + 1; i-- > 0;)
            if (among(self->ptr[i], s, n))
                return i;
    return kNpos;
}
EXPORT THISCALL size_t stringFindLastNotOf(const String* self, const char* s, size_t at, size_t n) {
    if (self->len > 0)
        for (size_t i = least(at, self->len - 1) + 1; i-- > 0;)
            if (!among(self->ptr[i], s, n))
                return i;
    return kNpos;
}

namespace {
const char* chars(const String* s) { return s->ptr ? s->ptr : &stringEmpty; }
int compare(const char* a, size_t an, const char* b, size_t bn) {
    const int r = memcmp(a, b, least(an, bn));
    return r ? r : an < bn ? -1 : an > bn ? 1 : 0;
}
int compare(const String* a, const String* b) { return compare(chars(a), a->len, chars(b), b->len); }
int compare(const String* a, const char* b) { return compare(chars(a), a->len, b, strlen(b)); }
} // namespace

EXPORT bool stringEq(const String* a, const String* b) { return compare(a, b) == 0; }
EXPORT bool stringEqChars(const String* a, const char* b) { return compare(a, b) == 0; }
EXPORT bool charsEqString(const char* a, const String* b) { return compare(b, a) == 0; }
EXPORT bool stringNe(const String* a, const String* b) { return compare(a, b) != 0; }
EXPORT bool stringNeChars(const String* a, const char* b) { return compare(a, b) != 0; }
EXPORT bool stringLt(const String* a, const String* b) { return compare(a, b) < 0; }
EXPORT bool stringLtChars(const String* a, const char* b) { return compare(a, b) < 0; }
EXPORT bool stringLe(const String* a, const String* b) { return compare(a, b) <= 0; }
EXPORT bool stringGt(const String* a, const String* b) { return compare(a, b) > 0; }
EXPORT bool stringGe(const String* a, const String* b) { return compare(a, b) >= 0; }

// operator+: the result is constructed in place at `out`.
EXPORT String* stringPlus(String* out, const String* a, const String* b) {
    stringConstructCopy(out, a);
    return stringAppendPart(out, b, 0, kNpos);
}
EXPORT String* stringPlusChar(String* out, const String* a, unsigned c) {
    stringConstructCopy(out, a);
    return stringAppendFill(out, 1, c);
}
EXPORT String* stringPlusChars(String* out, const String* a, const char* b) {
    stringConstructCopy(out, a);
    return stringAppend(out, b, strlen(b));
}
EXPORT String* charPlusString(String* out, unsigned c, const String* b) {
    stringTidy(out, 0);
    stringAppendFill(out, 1, c);
    return stringAppendPart(out, b, 0, kNpos);
}
EXPORT String* charsPlusString(String* out, const char* a, const String* b) {
    stringConstructChars(out, a, 0);
    return stringAppendPart(out, b, 0, kNpos);
}

// --- Exceptions ----------------------------------------------------------------------
// MSVCRT's exception class: its vftable (the deleting destructor, what()),
// the message and whether it's to be freed. logic_error adds the message
// as a string; out_of_range is a logic_error; bad_cast adds nothing.

struct Exception {
    const void* const* vf;
    const char* message;
    int owned;
};
struct LogicError {
    Exception base;
    String text;
};

namespace {
void exceptionDestroy(Exception* self) {
    if (self->owned)
        free((void*)self->message);
    self->owned = 0;
}
void exceptionCopy(Exception* self, const Exception* from) {
    self->message = 0;
    self->owned = 0;
    if (from->message) {
        const size_t n = strlen(from->message) + 1;
        char* copy = (char*)operator new(n);
        memcpy(copy, from->message, n);
        self->message = copy;
        self->owned = 1;
    }
}
THISCALL const char* exceptionWhat(const Exception* self) {
    return self->message ? self->message : "Unknown exception";
}
THISCALL const char* logicErrorWhat(const LogicError* self) { return chars(&self->text); }
THISCALL void* badCastDeleting(Exception* self, unsigned flags);
THISCALL void* outOfRangeDeleting(LogicError* self, unsigned flags);
const void* const logicErrorVftable[] = {(const void*)outOfRangeDeleting, (const void*)logicErrorWhat};
} // namespace

EXPORT const void* const badCastVftable[] = {(const void*)badCastDeleting, (const void*)exceptionWhat};
EXPORT const void* const outOfRangeVftable[] = {(const void*)outOfRangeDeleting, (const void*)logicErrorWhat};

EXPORT THISCALL void badCastDestroy(Exception* self) { exceptionDestroy(self); }
EXPORT THISCALL Exception* badCastCopy(Exception* self, const Exception* from) {
    exceptionCopy(self, from);
    self->vf = badCastVftable;
    return self;
}
EXPORT THISCALL void outOfRangeDestroy(LogicError* self) {
    stringTidy(&self->text, 1);
    exceptionDestroy(&self->base);
}
EXPORT THISCALL LogicError* logicErrorCopy(LogicError* self, const LogicError* from) {
    exceptionCopy(&self->base, &from->base);
    stringConstructCopy(&self->text, &from->text);
    self->base.vf = logicErrorVftable;
    return self;
}
EXPORT THISCALL LogicError* outOfRangeCopy(LogicError* self, const LogicError* from) {
    logicErrorCopy(self, from);
    self->base.vf = outOfRangeVftable;
    return self;
}
EXPORT THISCALL LogicError* outOfRangeConstruct(LogicError* self, const String* text) {
    self->base.message = 0;
    self->base.owned = 0;
    stringConstructCopy(&self->text, text);
    self->base.vf = outOfRangeVftable;
    return self;
}

namespace {
THISCALL void* badCastDeleting(Exception* self, unsigned flags) {
    badCastDestroy(self);
    if (flags & 1)
        operator delete(self);
    return self;
}
THISCALL void* outOfRangeDeleting(LogicError* self, unsigned flags) {
    outOfRangeDestroy(self);
    if (flags & 1)
        operator delete(self);
    return self;
}
} // namespace

EXPORT const long long zeroPosition = 0; // _Fpz: fpos's "no position in the file"

// --- ios_base and basic_ios ------------------------------------------------------------
// ios_base (0x28): vftable, state +4, exceptions +8, flags +0xc, precision
// +0x10, width +0x14, the iword/pword array +0x18, the callbacks +0x1c, the
// locale (a pointer to one on the heap) +0x20, which standard stream it is
// +0x24. basic_ios (0x34) adds the stream buffer +0x28, the tied stream
// +0x2c and the fill character +0x30.

struct Streambuf;
struct IosBase {
    const void* const* vf;
    int state;
    int except;
    int flags;
    int precision;
    int width;
    void* words;
    void* callbacks;
    Locale* locale;
    int standard;
};
struct Ios {
    IosBase base;
    Streambuf* buffer;
    void* tie; // an ostream
    char fill;
};
static_assert(sizeof(IosBase) == 0x28 && sizeof(Ios) == 0x34, "ios layout");

namespace {
enum : int { kGood = 0, kEofBit = 1, kFailBit = 2, kBadBit = 4, kStateMask = 0x17 };
enum : int {
    kSkipws = 0x1, kUnitbuf = 0x2, kUppercase = 0x4, kShowbase = 0x8, kShowpoint = 0x10, kShowpos = 0x20,
    kLeft = 0x40, kRight = 0x80, kInternal = 0x100, kDec = 0x200, kOct = 0x400, kHex = 0x800,
    kScientific = 0x1000, kFixed = 0x2000, kBasefield = 0xe00, kFloatfield = 0x3000
};
enum : int { kIn = 1, kOut = 2, kAte = 4, kApp = 8, kTrunc = 0x10, kBinary = 0x20 };
THISCALL void* iosBaseDeleting(IosBase* self, unsigned flags);
THISCALL void* iosDeleting(Ios* self, unsigned flags);
} // namespace

EXPORT const void* const iosBaseVftable[] = {(const void*)iosBaseDeleting};
EXPORT const void* const iosVftable[] = {(const void*)iosDeleting};

EXPORT THISCALL IosBase* iosBaseConstruct(IosBase* self) {
    self->vf = iosBaseVftable;
    self->standard = 0;
    return self;
}
EXPORT THISCALL void iosBaseClear(IosBase* self, int state, unsigned) {
    self->state = state & kStateMask;
    if (self->state & self->except)
        fail("a stream's state raised an exception (ios_base::failure)");
}
EXPORT THISCALL void iosBaseInit(IosBase* self) {
    Locale* locale = (Locale*)operator new(sizeof(Locale));
    localeConstruct(locale);
    self->locale = locale;
    self->except = kGood;
    self->flags = kSkipws | kDec;
    self->precision = 6;
    self->width = 0;
    self->words = 0;
    self->callbacks = 0;
    iosBaseClear(self, kGood, 0);
}
// _Addstd marks cin, cout and the like for flushing at exit; ours aren't.
EXPORT THISCALL void iosBaseAddStandard(IosBase*) {}
EXPORT THISCALL void iosBaseDestroy(IosBase* self) {
    self->vf = iosBaseVftable;
    if (self->locale) {
        localeDestroy(self->locale);
        operator delete(self->locale);
        self->locale = 0;
    }
}
EXPORT THISCALL IosBase* iosBaseCopyFormat(IosBase* self, const IosBase* from) {
    if (self == from)
        return self;
    self->flags = from->flags;
    self->precision = from->precision;
    self->width = from->width;
    if (self->locale && from->locale)
        localeAssign(self->locale, from->locale);
    self->except = from->except & kStateMask;
    iosBaseClear(self, self->state, 0);
    return self;
}
EXPORT THISCALL IosBase* iosBaseAssign(IosBase* self, const IosBase* from) {
    if (self != from) {
        self->state = from->state;
        iosBaseCopyFormat(self, from);
    }
    return self;
}
EXPORT THISCALL Locale* iosBaseGetloc(const IosBase* self, Locale* out) {
    localeCopy(out, self->locale);
    return out;
}

EXPORT THISCALL Ios* iosConstruct(Ios* self) {
    iosBaseConstruct(&self->base);
    self->base.vf = iosVftable;
    return self;
}
EXPORT THISCALL void iosClear(Ios* self, int state, unsigned reraise) {
    iosBaseClear(&self->base, self->buffer ? state : state | kBadBit, reraise);
}
EXPORT THISCALL void iosSetstate(Ios* self, int state, unsigned reraise) {
    iosClear(self, self->base.state | state, reraise);
}
EXPORT THISCALL char iosWiden(const Ios*, unsigned c) { return byte(c); }
EXPORT THISCALL void iosInit(Ios* self, Streambuf* buffer, unsigned standard) {
    self->buffer = buffer;
    self->tie = 0;
    self->fill = ' ';
    iosBaseInit(&self->base);
    if (!buffer)
        iosSetstate(self, kBadBit, 0);
    if (flag(standard))
        iosBaseAddStandard(&self->base);
}
EXPORT THISCALL void iosDestroy(Ios* self) {
    self->base.vf = iosVftable;
    iosBaseDestroy(&self->base);
}

namespace {
THISCALL void* iosBaseDeleting(IosBase* self, unsigned flags) {
    iosBaseDestroy(self);
    if (flags & 1)
        operator delete(self);
    return self;
}
THISCALL void* iosDeleting(Ios* self, unsigned flags) {
    iosDestroy(self);
    if (flags & 1)
        operator delete(self);
    return self;
}
} // namespace

// ios_base::Init and _Winit count the translation units that use the
// standard streams; ours are made at start-up, so there's only counting.
namespace {
int gInitCount, gWinitCount;
}
EXPORT THISCALL void* iosInitConstruct(void* self) {
    ++gInitCount;
    return self;
}
EXPORT THISCALL void iosInitDestroy(void*) { --gInitCount; }
EXPORT THISCALL void* winitConstruct(void* self) {
    ++gWinitCount;
    return self;
}
EXPORT THISCALL void winitDestroy(void*) { --gWinitCount; }

// --- basic_streambuf ------------------------------------------------------------------
// 0x38 bytes: vftable; the get and put areas' starts (+4, +8); pointers to
// where the starts, the next characters (+0x14, +0x18) and the counts left
// (+0x24, +0x28) are kept (+0xc, +0x10, +0x1c, +0x20, +0x2c, +0x30), so a
// buffer can keep them elsewhere; and its locale (+0x34). The count left in
// the get area is from the next character. The vftable: the destructor,
// overflow, pbackfail, showmanyc, underflow, uflow, xsgetn, xsputn,
// seekoff, seekpos, setbuf, sync, imbue.

struct Fpos { // fpos<int>
    long offset;
    long long position;
    int state;
};
static_assert(sizeof(Fpos) == 24 && __builtin_offsetof(Fpos, position) == 8, "fpos layout");

struct Streambuf {
    const void* const* vf;
    char* getStart;
    char* putStart;
    char** getStartAt;
    char** putStartAt;
    char* getNext;
    char* putNext;
    char** getNextAt;
    char** putNextAt;
    int getCount;
    int putCount;
    int* getCountAt;
    int* putCountAt;
    Locale locale;
};
static_assert(sizeof(Streambuf) == 0x38, "streambuf layout");

namespace {
enum { kOverflow = 1, kPbackfail, kShowmanyc, kUnderflow, kUflow, kXsgetn, kXsputn, kSeekoff, kSeekpos, kSetbuf,
       kSync, kImbue };

inline char* eback(Streambuf* b) { return *b->getStartAt; }
inline char* gptr(Streambuf* b) { return *b->getNextAt; }
inline int gavail(Streambuf* b) { return *b->getNextAt ? *b->getCountAt : 0; }
inline void gbump(Streambuf* b, int n) {
    *b->getCountAt -= n;
    *b->getNextAt += n;
}
inline char* pbase(Streambuf* b) { return *b->putStartAt; }
inline char* pptr(Streambuf* b) { return *b->putNextAt; }
inline char* epptr(Streambuf* b) { return *b->putNextAt + *b->putCountAt; }
inline int pavail(Streambuf* b) { return *b->putNextAt ? *b->putCountAt : 0; }
inline void pbump(Streambuf* b, int n) {
    *b->putCountAt -= n;
    *b->putNextAt += n;
}

int underflow(Streambuf* b) { return slot<int(THISCALL*)(Streambuf*)>(b, kUnderflow)(b); }
int uflow(Streambuf* b) { return slot<int(THISCALL*)(Streambuf*)>(b, kUflow)(b); }
int overflow(Streambuf* b, int c) { return slot<int(THISCALL*)(Streambuf*, int)>(b, kOverflow)(b, c); }
int sgetc(Streambuf* b) { return gavail(b) > 0 ? (unsigned char)*gptr(b) : underflow(b); }
int sbumpc(Streambuf* b) {
    if (gavail(b) <= 0)
        return uflow(b);
    const int c = (unsigned char)*gptr(b);
    gbump(b, 1);
    return c;
}
int snextc(Streambuf* b) { return sbumpc(b) == kEof ? kEof : sgetc(b); }
int sputc(Streambuf* b, char c) {
    if (pavail(b) <= 0)
        return overflow(b, (unsigned char)c);
    *pptr(b) = c;
    pbump(b, 1);
    return (unsigned char)c;
}
int sgetn(Streambuf* b, char* s, int n) { return slot<int(THISCALL*)(Streambuf*, char*, int)>(b, kXsgetn)(b, s, n); }
int sputn(Streambuf* b, const char* s, int n) {
    return slot<int(THISCALL*)(Streambuf*, const char*, int)>(b, kXsputn)(b, s, n);
}
int pubsync(Streambuf* b) { return slot<int(THISCALL*)(Streambuf*)>(b, kSync)(b); }
Fpos* pubseekpos(Streambuf* b, Fpos* out, Fpos at, int which) {
    return slot<Fpos*(THISCALL*)(Streambuf*, Fpos*, Fpos, int)>(b, kSeekpos)(b, out, at, which);
}
long offsetOf(const Fpos& p) { return p.offset + long(p.position); }
Fpos position(long offset) { return Fpos{offset, 0, 0}; }

THISCALL void* streambufDeleting(Streambuf* self, unsigned flags);
} // namespace

EXPORT THISCALL void streambufSetg(Streambuf* self, char* start, char* next, char* end) {
    *self->getStartAt = start;
    *self->getNextAt = next;
    *self->getCountAt = int(end - next);
}
EXPORT THISCALL void streambufSetp(Streambuf* self, char* start, char* end) {
    *self->putStartAt = start;
    *self->putNextAt = start;
    *self->putCountAt = int(end - start);
}
EXPORT THISCALL void streambufInit(Streambuf* self) {
    self->getStartAt = &self->getStart;
    self->putStartAt = &self->putStart;
    self->getNextAt = &self->getNext;
    self->putNextAt = &self->putNext;
    self->getCountAt = &self->getCount;
    self->putCountAt = &self->putCount;
    streambufSetp(self, 0, 0);
    streambufSetg(self, 0, 0, 0);
}

EXPORT THISCALL int streambufOverflow(Streambuf*, int) { return kEof; }
EXPORT THISCALL int streambufPbackfail(Streambuf*, int) { return kEof; }
EXPORT THISCALL int streambufShowmanyc(Streambuf*) { return 0; }
EXPORT THISCALL int streambufUnderflow(Streambuf*) { return kEof; }
EXPORT THISCALL int streambufUflow(Streambuf* self) {
    if (underflow(self) == kEof)
        return kEof;
    const int c = (unsigned char)*gptr(self);
    gbump(self, 1);
    return c;
}
EXPORT THISCALL int streambufXsgetn(Streambuf* self, char* s, int n) {
    int done = 0;
    while (done < n) {
        const int ready = gavail(self);
        if (ready > 0) {
            const int m = ready < n - done ? ready : n - done;
            memcpy(s + done, gptr(self), size_t(m));
            gbump(self, m);
            done += m;
        } else {
            const int c = uflow(self);
            if (c == kEof)
                break;
            s[done++] = char(c);
        }
    }
    return done;
}
EXPORT THISCALL int streambufXsputn(Streambuf* self, const char* s, int n) {
    int done = 0;
    while (done < n) {
        const int room = pavail(self);
        if (room > 0) {
            const int m = room < n - done ? room : n - done;
            memcpy(pptr(self), s + done, size_t(m));
            pbump(self, m);
            done += m;
        } else {
            if (overflow(self, (unsigned char)s[done]) == kEof)
                break;
            ++done;
        }
    }
    return done;
}
EXPORT THISCALL Fpos* streambufSeekoff(Streambuf*, Fpos* out, long, int, int) {
    *out = position(-1);
    return out;
}
EXPORT THISCALL Fpos* streambufSeekpos(Streambuf*, Fpos* out, Fpos, int) {
    *out = position(-1);
    return out;
}
EXPORT THISCALL Streambuf* streambufSetbuf(Streambuf* self, char*, int) { return self; }
EXPORT THISCALL int streambufSync(Streambuf*) { return 0; }
EXPORT THISCALL void streambufImbue(Streambuf*, const Locale*) {}

EXPORT const void* const streambufVftable[] = {
    (const void*)streambufDeleting,  (const void*)streambufOverflow, (const void*)streambufPbackfail,
    (const void*)streambufShowmanyc, (const void*)streambufUnderflow, (const void*)streambufUflow,
    (const void*)streambufXsgetn,    (const void*)streambufXsputn,   (const void*)streambufSeekoff,
    (const void*)streambufSeekpos,   (const void*)streambufSetbuf,   (const void*)streambufSync,
    (const void*)streambufImbue};

EXPORT THISCALL Streambuf* streambufConstruct(Streambuf* self) {
    self->vf = streambufVftable;
    localeConstruct(&self->locale);
    streambufInit(self);
    return self;
}
// The copy constructor copies every field as it is, the pointers to where
// the other buffer keeps its positions too.
EXPORT THISCALL Streambuf* streambufConstructCopy(Streambuf* self, const Streambuf* from) {
    memcpy(self, from, sizeof(Streambuf));
    self->vf = streambufVftable;
    localeCopy(&self->locale, &from->locale);
    return self;
}
EXPORT THISCALL void streambufDestroy(Streambuf* self) {
    self->vf = streambufVftable;
    localeDestroy(&self->locale);
}

namespace {
THISCALL void* streambufDeleting(Streambuf* self, unsigned flags) {
    streambufDestroy(self);
    if (flags & 1)
        operator delete(self);
    return self;
}
} // namespace

// --- basic_filebuf ----------------------------------------------------------------------
// 0x54 bytes: a streambuf, then ours up to +0x4c, a locale of its own
// (+0x4c, which the games' inline constructors make) and the FILE (+0x50;
// is_open() is inline). It reads and writes the FILE directly, keeping no
// characters of its own, so its get and put areas stay empty.

struct Filebuf {
    Streambuf base;
    int closeFile; // +0x38: opened by open(), so closed when destroyed
    int unused[4];
    Locale locale; // +0x4c
    FILE* file;    // +0x50
};
static_assert(sizeof(Filebuf) == 0x54 && __builtin_offsetof(Filebuf, file) == 0x50, "filebuf layout");

namespace {
enum { kNewFile, kOpenedFile, kClosedFile }; // filebuf::_Initfl
THISCALL void* filebufDeleting(Filebuf* self, unsigned flags);
} // namespace

EXPORT int filebufStateInit = 0; // filebuf::_Init's _Stinit: the conversion state's start

EXPORT THISCALL void filebufInit(Filebuf* self, FILE* file, int which) {
    self->closeFile = which == kOpenedFile;
    streambufInit(&self->base);
    self->file = file;
}
EXPORT THISCALL int filebufOverflow(Filebuf* self, int c) {
    if (c == kEof)
        return 0;
    return self->file && fputc(c, self->file) != kEof ? c : kEof;
}
EXPORT THISCALL int filebufPbackfail(Filebuf* self, int c) {
    if (!self->file)
        return kEof;
    if (c == kEof)
        return 0;
    return ungetc(c, self->file) != kEof ? c : kEof;
}
EXPORT THISCALL int filebufUnderflow(Filebuf* self) {
    if (!self->file)
        return kEof;
    const int c = fgetc(self->file);
    if (c != kEof)
        ungetc(c, self->file);
    return c;
}
EXPORT THISCALL int filebufUflow(Filebuf* self) { return self->file ? fgetc(self->file) : kEof; }
EXPORT THISCALL int filebufXsgetn(Filebuf* self, char* s, int n) {
    return self->file && n > 0 ? int(fread(s, 1, size_t(n), self->file)) : 0;
}
EXPORT THISCALL int filebufXsputn(Filebuf* self, const char* s, int n) {
    return self->file && n > 0 ? int(fwrite(s, 1, size_t(n), self->file)) : 0;
}
EXPORT THISCALL Fpos* filebufSeekoff(Filebuf* self, Fpos* out, long offset, int way, int) {
    // seekdir's beg, cur and end are fseek's 0, 1 and 2.
    *out = position(self->file && fseek(self->file, offset, way) == 0 ? ftell(self->file) : -1);
    return out;
}
EXPORT THISCALL Fpos* filebufSeekpos(Filebuf* self, Fpos* out, Fpos at, int) {
    *out = position(self->file && fseek(self->file, offsetOf(at), 0) == 0 ? ftell(self->file) : -1);
    return out;
}
EXPORT THISCALL Streambuf* filebufSetbuf(Filebuf* self, char*, int) { return self->file ? &self->base : 0; }
EXPORT THISCALL int filebufSync(Filebuf* self) { return self->file && fflush(self->file) != 0 ? -1 : 0; }

EXPORT const void* const filebufVftable[] = {
    (const void*)filebufDeleting,    (const void*)filebufOverflow,  (const void*)filebufPbackfail,
    (const void*)streambufShowmanyc, (const void*)filebufUnderflow, (const void*)filebufUflow,
    (const void*)filebufXsgetn,      (const void*)filebufXsputn,    (const void*)filebufSeekoff,
    (const void*)filebufSeekpos,     (const void*)filebufSetbuf,    (const void*)filebufSync,
    (const void*)streambufImbue};

EXPORT THISCALL Filebuf* filebufConstruct(Filebuf* self, FILE* file) {
    streambufConstruct(&self->base);
    localeConstruct(&self->locale);
    self->base.vf = filebufVftable;
    filebufInit(self, file, kNewFile);
    return self;
}
EXPORT THISCALL Filebuf* filebufOpen(Filebuf* self, const char* name, int mode) {
    if (self->file)
        return 0;
    const char* how;
    switch (mode & ~(kAte | kBinary)) {
    case kIn: how = "r"; break;
    case kOut:
    case kOut | kTrunc: how = "w"; break;
    case kOut | kApp: how = "a"; break;
    case kIn | kOut: how = "r+"; break;
    case kIn | kOut | kTrunc: how = "w+"; break;
    case kIn | kOut | kApp: how = "a+"; break;
    default: return 0;
    }
    char text[4];
    size_t n = strlen(how);
    memcpy(text, how, n);
    if (mode & kBinary)
        text[n++] = 'b';
    text[n] = 0;
    FILE* file = fopen(name, text);
    if (!file)
        return 0;
    filebufInit(self, file, kOpenedFile);
    if ((mode & kAte) && fseek(file, 0, 2) != 0) {
        fclose(file);
        filebufInit(self, 0, kClosedFile);
        return 0;
    }
    return self;
}
EXPORT THISCALL Filebuf* filebufClose(Filebuf* self) {
    if (!self->file || fclose(self->file) != 0)
        return 0;
    filebufInit(self, 0, kClosedFile);
    return self;
}
EXPORT THISCALL void filebufDestroy(Filebuf* self) {
    self->base.vf = filebufVftable;
    if (self->closeFile)
        filebufClose(self);
    localeDestroy(&self->locale);
    streambufDestroy(&self->base);
}

namespace {
THISCALL void* filebufDeleting(Filebuf* self, unsigned flags) {
    filebufDestroy(self);
    if (flags & 1)
        operator delete(self);
    return self;
}
} // namespace

// --- strstreambuf -----------------------------------------------------------------------
// 0x50 bytes: a streambuf, then ours. The games build them inline, with
// vftables of their own, and call _Init; so the functions here can't count
// on our vftable.

struct Strstreambuf {
    Streambuf base;
    int mode;      // +0x38
    int growBy;    // +0x3c
    char* unused;  // +0x40
    char* highest; // +0x44: the furthest anything was written
    void* unused2[2];
};
static_assert(sizeof(Strstreambuf) == 0x50, "strstreambuf layout");

namespace {
enum { kAllocated = 1, kConstant = 2, kDynamic = 4, kFrozenBuffer = 8 };
constexpr int kMinGrowth = 32;
void noteHighest(Strstreambuf* self) {
    if (pptr(&self->base) && self->highest < pptr(&self->base))
        self->highest = pptr(&self->base);
}
} // namespace

// _Init(n, get, put, mode): over n characters at `get` (n 0: up to its
// terminator; below 0: without end), writing from `put` if that's given;
// with no `get`, over memory it allocates as it's written.
EXPORT THISCALL void strstreambufInit(Strstreambuf* self, int n, char* get, char* put, int mode) {
    self->mode = mode;
    self->unused = 0;
    self->highest = 0;
    self->unused2[0] = self->unused2[1] = 0;
    Streambuf* b = &self->base;
    if (!get) {
        self->growBy = kMinGrowth;
        self->mode |= kDynamic;
        return;
    }
    self->growBy = 0;
    const size_t size = n > 0 ? size_t(n) : n == 0 ? strlen(get) : 0x7fffffff;
    char* end = get + size;
    self->highest = end;
    if (!put) {
        streambufSetg(b, get, get, end);
    } else {
        if (put < get)
            put = get;
        else if (put > end)
            put = end;
        streambufSetp(b, put, end);
        streambufSetg(b, get, get, put);
    }
}
EXPORT THISCALL int strstreambufOverflow(Strstreambuf* self, int c) {
    Streambuf* b = &self->base;
    if (c == kEof)
        return 0;
    if (pptr(b) && pptr(b) < epptr(b)) {
        *pptr(b) = char(c);
        pbump(b, 1);
        return (unsigned char)c;
    }
    if (!(self->mode & kDynamic) || (self->mode & (kConstant | kFrozenBuffer)))
        return kEof;
    // Grows the memory by half again (at least the growth asked for).
    char* old = eback(b);
    const int used = pptr(b) ? int(epptr(b) - old) : 0;
    int more = used / 2;
    if (more < self->growBy)
        more = self->growBy;
    if (more < kMinGrowth)
        more = kMinGrowth;
    char* memory = (char*)operator new(size_t(used + more));
    if (used)
        memcpy(memory, old, size_t(used));
    if (!used) {
        self->highest = memory;
        streambufSetp(b, memory, memory + more);
        streambufSetg(b, memory, memory, memory);
    } else {
        self->highest = memory + (self->highest - old);
        const int written = int(pptr(b) - old);
        const int read = int(gptr(b) - old);
        streambufSetp(b, memory + (pbase(b) - old), memory + used + more);
        pbump(b, written - int(pbase(b) - memory));
        streambufSetg(b, memory, memory + read, memory + written);
    }
    if (old && (self->mode & kAllocated))
        operator delete(old);
    self->mode |= kAllocated;
    *pptr(b) = char(c);
    pbump(b, 1);
    return (unsigned char)c;
}
EXPORT THISCALL int strstreambufPbackfail(Strstreambuf* self, int c) {
    Streambuf* b = &self->base;
    if (!gptr(b) || gptr(b) <= eback(b))
        return kEof;
    if (c != kEof && byte(unsigned(c)) != gptr(b)[-1] && (self->mode & kConstant))
        return kEof;
    gbump(b, -1);
    if (c == kEof)
        return 0;
    *gptr(b) = char(c);
    return c;
}
EXPORT THISCALL int strstreambufUnderflow(Strstreambuf* self) {
    Streambuf* b = &self->base;
    if (!gptr(b))
        return kEof;
    if (gavail(b) > 0)
        return (unsigned char)*gptr(b);
    noteHighest(self);
    if (self->highest <= gptr(b))
        return kEof;
    streambufSetg(b, eback(b), gptr(b), self->highest);
    return (unsigned char)*gptr(b);
}
EXPORT THISCALL Fpos* strstreambufSeekoff(Strstreambuf* self, Fpos* out, long offset, int way, int which) {
    Streambuf* b = &self->base;
    noteHighest(self);
    *out = position(-1);
    char* start = eback(b);
    if (!start)
        return out;
    long base;
    switch (way) {
    case 0: base = 0; break;
    case 1:
        if ((which & kIn) && gptr(b))
            base = long(gptr(b) - start);
        else if ((which & kOut) && pptr(b))
            base = long(pptr(b) - start);
        else
            return out;
        break;
    case 2: base = long(self->highest - start); break;
    default: return out;
    }
    const long to = base + offset;
    if (to < 0 || to > long(self->highest - start))
        return out;
    if ((which & kIn) && gptr(b))
        streambufSetg(b, start, start + to, self->highest);
    if ((which & kOut) && pptr(b)) {
        char* end = epptr(b);
        streambufSetp(b, pbase(b), end);
        pbump(b, int(start + to - pbase(b)));
    }
    *out = position(to);
    return out;
}
EXPORT THISCALL Fpos* strstreambufSeekpos(Strstreambuf* self, Fpos* out, Fpos at, int which) {
    return strstreambufSeekoff(self, out, offsetOf(at), 0, which);
}
EXPORT THISCALL void strstreambufFreeze(Strstreambuf* self, unsigned freeze) {
    if (flag(freeze) && (self->mode & kDynamic))
        self->mode |= kFrozenBuffer;
    else if (!flag(freeze))
        self->mode &= ~kFrozenBuffer;
}
EXPORT THISCALL void strstreambufDestroy(Strstreambuf* self) {
    if ((self->mode & kAllocated) && !(self->mode & kFrozenBuffer))
        operator delete(eback(&self->base));
    self->mode = 0;
    streambufDestroy(&self->base);
}

// --- Streams ------------------------------------------------------------------------------
// Each stream is a virtual base basic_ios after what's its own, reached by
// the offset in the table its first word points to (its vbtable's second
// entry). istream's own part is that pointer and the count of characters
// last read (8 bytes), ostream's the pointer alone; ifstream adds its
// filebuf (+8, so the ios is at +0x5c), ofstream its filebuf (+4; +0x58),
// fstream (an istream, then an ostream, then its filebuf at +0xc) has the
// ios at +0x60, istrstream and ostrstream their strstreambufs (ios at +0x58
// and +0x54). Constructors take a last, hidden argument: whether to make the
// virtual base (only the most derived class does). Destructors and the
// vftable entries are called with `this` at the ios, and leave it for the
// caller (or `vbase destructor') to destroy.

struct Istream {
    const int* vbtable;
    int count;
};
struct Ostream {
    const int* vbtable;
};

namespace {
inline Ios* iosOf(const void* stream) { return (Ios*)((char*)stream + (*(const int* const*)stream)[1]); }
inline bool good(const Ios* ios) { return ios->base.state == kGood; }

constexpr int kIstreamIos = 8, kOstreamIos = 4, kIfstreamIos = 0x5c, kOfstreamIos = 0x58, kFstreamIos = 0x60;
} // namespace

EXPORT const int istreamVbtable[2] = {0, kIstreamIos};
EXPORT const int ifstreamVbtable[2] = {0, kIfstreamIos};
EXPORT const int ofstreamVbtable[2] = {0, kOfstreamIos};
namespace {
const int ostreamVbtable[2] = {0, kOstreamIos};
const int fstreamVbtable[2] = {0, kFstreamIos};
const int fstreamOstreamVbtable[2] = {0, kFstreamIos - 8};

template <int Offset, void (*Destroy)(Ios*)>
THISCALL void* streamDeleting(Ios* ios, unsigned flags) {
    Destroy(ios);
    iosDestroy(ios);
    char* whole = (char*)ios - Offset;
    if (flags & 1)
        operator delete(whole);
    return whole;
}
void istreamDestroyIos(Ios* ios);
void ostreamDestroyIos(Ios* ios);
void ifstreamDestroyIos(Ios* ios);
void ofstreamDestroyIos(Ios* ios);
void fstreamDestroyIos(Ios* ios);
} // namespace

EXPORT const void* const istreamVftable[] = {(const void*)streamDeleting<kIstreamIos, istreamDestroyIos>};
EXPORT const void* const ostreamVftable[] = {(const void*)streamDeleting<kOstreamIos, ostreamDestroyIos>};
EXPORT const void* const ifstreamVftable[] = {(const void*)streamDeleting<kIfstreamIos, ifstreamDestroyIos>};
EXPORT const void* const ofstreamVftable[] = {(const void*)streamDeleting<kOfstreamIos, ofstreamDestroyIos>};
namespace {
const void* const fstreamVftable[] = {(const void*)streamDeleting<kFstreamIos, fstreamDestroyIos>};
} // namespace

// --- ostream

namespace {
void flush(Ostream* os) {
    Ios* ios = iosOf(os);
    if (ios->buffer && pubsync(ios->buffer) == -1)
        iosSetstate(ios, kBadBit, 0);
}
bool prefix(Ostream* os) { // opfx: flushes the tied stream first
    Ios* ios = iosOf(os);
    if (good(ios) && ios->tie)
        flush((Ostream*)ios->tie);
    return good(ios);
}
void suffix(Ostream* os) { // osfx
    if (iosOf(os)->base.flags & kUnitbuf)
        flush(os);
}
// Writes n characters padded to the width, then resets the width.
Ostream* putField(Ostream* os, const char* s, size_t n) {
    Ios* ios = iosOf(os);
    if (!prefix(os))
        return os;
    int state = kGood;
    const size_t width = ios->base.width > 0 ? size_t(ios->base.width) : 0;
    size_t pad = width > n ? width - n : 0;
    const bool left = (ios->base.flags & (kLeft | kRight | kInternal)) == kLeft;
    for (; !left && pad > 0; --pad)
        if (sputc(ios->buffer, ios->fill) == kEof)
            state |= kBadBit;
    if (state == kGood && sputn(ios->buffer, s, int(n)) != int(n))
        state |= kBadBit;
    for (; state == kGood && pad > 0; --pad)
        if (sputc(ios->buffer, ios->fill) == kEof)
            state |= kBadBit;
    ios->base.width = 0;
    if (state)
        iosSetstate(ios, state, 0);
    suffix(os);
    return os;
}
Ostream* putInteger(Ostream* os, long value) {
    const int flags = iosOf(os)->base.flags;
    char format[8], *f = format;
    *f++ = '%';
    const int base = flags & kBasefield;
    if (flags & kShowpos && base != kOct && base != kHex)
        *f++ = '+';
    if (flags & kShowbase && (base == kOct || base == kHex))
        *f++ = '#';
    *f++ = 'l';
    *f++ = base == kOct ? 'o' : base == kHex ? (flags & kUppercase ? 'X' : 'x') : 'd';
    *f = 0;
    char text[40];
    const int n = sprintf(text, format, value);
    return putField(os, text, size_t(n));
}
void destroyOstream(Ios* ios) { ios->base.vf = ostreamVftable; }
void ostreamDestroyIos(Ios* ios) { destroyOstream(ios); }
} // namespace

EXPORT THISCALL Ostream* ostreamConstruct(Ostream* self, Streambuf* buffer, unsigned standard, unsigned init,
                                          int makeBase) {
    if (makeBase) {
        self->vbtable = ostreamVbtable;
        iosConstruct(iosOf(self));
    }
    Ios* ios = iosOf(self);
    ios->base.vf = ostreamVftable;
    if (flag(init))
        iosInit(ios, buffer, standard);
    return self;
}
EXPORT THISCALL void ostreamDestroy(Ios* ios) { destroyOstream(ios); }
EXPORT THISCALL Ostream* ostreamWrite(Ostream* self, const char* s, int n) {
    if (!prefix(self))
        return self;
    Ios* ios = iosOf(self);
    if (sputn(ios->buffer, s, n) != n)
        iosSetstate(ios, kBadBit, 0);
    suffix(self);
    return self;
}
EXPORT THISCALL Ostream* ostreamPutInt(Ostream* self, int value) { return putInteger(self, value); }
EXPORT THISCALL Ostream* ostreamPutLong(Ostream* self, long value) { return putInteger(self, value); }
EXPORT THISCALL Ostream* ostreamPutFloat(Ostream* self, float value) {
    const IosBase& b = iosOf(self)->base;
    const int floatfield = b.flags & kFloatfield;
    int precision = b.precision;
    if (precision <= 0 && floatfield != kFixed)
        precision = 6;
    char format[12], *f = format;
    *f++ = '%';
    if (b.flags & kShowpos)
        *f++ = '+';
    if (b.flags & kShowpoint)
        *f++ = '#';
    *f++ = '.';
    *f++ = '*';
    const bool upper = (b.flags & kUppercase) != 0;
    *f++ = floatfield == kFixed ? 'f' : floatfield == kScientific ? (upper ? 'E' : 'e') : (upper ? 'G' : 'g');
    *f = 0;
    char text[400];
    const int n = sprintf(text, format, precision > 100 ? 100 : precision, double(value));
    return putField(self, text, size_t(n));
}
EXPORT Ostream* ostreamPutChars(Ostream* os, const char* s) { return putField(os, s, strlen(s)); }
EXPORT Ostream* ostreamPutString(Ostream* os, const String* s) { return putField(os, chars(s), s->len); }
EXPORT Ostream* ostreamEndl(Ostream* os) {
    Ios* ios = iosOf(os);
    if (prefix(os)) {
        if (sputc(ios->buffer, '\n') == kEof)
            iosSetstate(ios, kBadBit, 0);
        suffix(os);
    }
    flush(os);
    return os;
}

// --- istream

namespace {
// ipfx: flushes the tied stream and, unless told not to, skips white space.
bool prefix(Istream* is, bool noskip) {
    Ios* ios = iosOf(is);
    if (good(ios)) {
        if (ios->tie)
            flush((Ostream*)ios->tie);
        if (!noskip && (ios->base.flags & kSkipws)) {
            int c = sgetc(ios->buffer);
            while (c != kEof && isSpace(c))
                c = snextc(ios->buffer);
            if (c == kEof)
                iosSetstate(ios, kEofBit, 0);
        }
        if (good(ios))
            return true;
    }
    iosSetstate(ios, kFailBit, 0);
    return false;
}
void destroyIstream(Ios* ios) { ios->base.vf = istreamVftable; }
void istreamDestroyIos(Ios* ios) { destroyIstream(ios); }
} // namespace

EXPORT THISCALL Istream* istreamConstruct(Istream* self, Streambuf* buffer, unsigned standard, int makeBase) {
    if (makeBase) {
        self->vbtable = istreamVbtable;
        iosConstruct(iosOf(self));
    }
    Ios* ios = iosOf(self);
    ios->base.vf = istreamVftable;
    self->count = 0;
    iosInit(ios, buffer, standard);
    return self;
}
// For the standard streams, made before anything else: nothing set up.
EXPORT THISCALL Istream* istreamConstructUninitialized(Istream* self, int, int makeBase) {
    if (makeBase) {
        self->vbtable = istreamVbtable;
        iosConstruct(iosOf(self));
    }
    Ios* ios = iosOf(self);
    ios->base.vf = istreamVftable;
    iosBaseAddStandard(&ios->base);
    return self;
}
EXPORT THISCALL void istreamDestroy(Ios* ios) { destroyIstream(ios); }
EXPORT THISCALL int istreamGet(Istream* self) {
    self->count = 0;
    if (!prefix(self, true))
        return kEof;
    Ios* ios = iosOf(self);
    const int c = sbumpc(ios->buffer);
    if (c == kEof)
        iosSetstate(ios, kEofBit | kFailBit, 0);
    else
        self->count = 1;
    return c;
}
EXPORT THISCALL Istream* istreamGetline(Istream* self, char* s, int n, unsigned delimiter) {
    self->count = 0;
    Ios* ios = iosOf(self);
    int state = kGood;
    if (n > 0 && prefix(self, true)) {
        const int end = (unsigned char)byte(delimiter);
        char* out = s;
        for (int c = sgetc(ios->buffer);; c = snextc(ios->buffer)) {
            if (c == kEof) {
                state |= kEofBit;
                break;
            }
            if (c == end) {
                ++self->count;
                sbumpc(ios->buffer);
                break;
            }
            if (out - s >= n - 1) {
                state |= kFailBit;
                break;
            }
            *out++ = char(c);
            ++self->count;
        }
        *out = 0;
    } else if (n > 0) {
        *s = 0;
    }
    if (self->count == 0)
        state |= kFailBit;
    if (state)
        iosSetstate(ios, state, 0);
    return self;
}
EXPORT THISCALL Istream* istreamRead(Istream* self, char* s, int n) {
    self->count = 0;
    if (prefix(self, true)) {
        Ios* ios = iosOf(self);
        const int m = sgetn(ios->buffer, s, n);
        self->count = m;
        if (m != n)
            iosSetstate(ios, kEofBit | kFailBit, 0);
    }
    return self;
}
EXPORT THISCALL Istream* istreamSeekg(Istream* self, Fpos at) {
    Ios* ios = iosOf(self);
    if (!(ios->base.state & (kFailBit | kBadBit))) {
        Fpos result;
        if (offsetOf(*pubseekpos(ios->buffer, &result, at, kIn)) == -1)
            iosSetstate(ios, kFailBit, 0);
    }
    return self;
}

// --- File streams

namespace {
void ifstreamDestroyIos(Ios* ios) {
    ios->base.vf = ifstreamVftable;
    filebufDestroy((Filebuf*)((char*)ios - sizeof(Filebuf)));
    destroyIstream(ios);
}
void ofstreamDestroyIos(Ios* ios) {
    ios->base.vf = ofstreamVftable;
    filebufDestroy((Filebuf*)((char*)ios - sizeof(Filebuf)));
    destroyOstream(ios);
}
void fstreamDestroyIos(Ios* ios) {
    ios->base.vf = fstreamVftable;
    filebufDestroy((Filebuf*)((char*)ios - sizeof(Filebuf)));
    destroyOstream(ios);
    destroyIstream(ios);
}
} // namespace

EXPORT THISCALL Istream* ifstreamConstruct(Istream* self, const char* name, int mode, int makeBase) {
    Filebuf* buffer = (Filebuf*)((char*)self + 8);
    if (makeBase) {
        self->vbtable = ifstreamVbtable;
        iosConstruct(iosOf(self));
    }
    istreamConstruct(self, &buffer->base, 0, 0);
    filebufConstruct(buffer, 0);
    Ios* ios = iosOf(self);
    ios->base.vf = ifstreamVftable;
    if (!filebufOpen(buffer, name, mode | kIn))
        iosSetstate(ios, kFailBit, 0);
    return self;
}
EXPORT THISCALL void ifstreamDestroy(Ios* ios) { ifstreamDestroyIos(ios); }
EXPORT THISCALL void ifstreamVbaseDestroy(Istream* self) {
    Ios* ios = (Ios*)((char*)self + kIfstreamIos);
    ifstreamDestroyIos(ios);
    iosDestroy(ios);
}

EXPORT THISCALL Ostream* ofstreamConstruct(Ostream* self, int makeBase) {
    Filebuf* buffer = (Filebuf*)((char*)self + 4);
    if (makeBase) {
        self->vbtable = ofstreamVbtable;
        iosConstruct(iosOf(self));
    }
    ostreamConstruct(self, &buffer->base, 0, 1, 0);
    filebufConstruct(buffer, 0);
    iosOf(self)->base.vf = ofstreamVftable;
    return self;
}
EXPORT THISCALL void ofstreamOpen(Ostream* self, const char* name, int mode) {
    if (!filebufOpen((Filebuf*)((char*)self + 4), name, mode | kOut))
        iosSetstate(iosOf(self), kFailBit, 0);
}
EXPORT THISCALL void ofstreamClose(Ostream* self) {
    if (!filebufClose((Filebuf*)((char*)self + 4)))
        iosSetstate(iosOf(self), kFailBit, 0);
}
EXPORT THISCALL void ofstreamDestroy(Ios* ios) { ofstreamDestroyIos(ios); }
EXPORT THISCALL void ofstreamVbaseDestroy(Ostream* self) {
    Ios* ios = (Ios*)((char*)self + kOfstreamIos);
    ofstreamDestroyIos(ios);
    iosDestroy(ios);
}

EXPORT THISCALL Istream* fstreamConstruct(Istream* self, const char* name, int mode, int makeBase) {
    Ostream* out = (Ostream*)((char*)self + 8);
    Filebuf* buffer = (Filebuf*)((char*)self + 0xc);
    if (makeBase) {
        self->vbtable = fstreamVbtable;
        out->vbtable = fstreamOstreamVbtable;
        iosConstruct(iosOf(self));
    }
    istreamConstruct(self, &buffer->base, 0, 0);
    ostreamConstruct(out, &buffer->base, 0, 0, 0);
    filebufConstruct(buffer, 0);
    Ios* ios = iosOf(self);
    ios->base.vf = fstreamVftable;
    if (!filebufOpen(buffer, name, mode))
        iosSetstate(ios, kFailBit, 0);
    return self;
}
EXPORT THISCALL void fstreamClose(Istream* self) {
    if (!filebufClose((Filebuf*)((char*)self + 0xc)))
        iosSetstate(iosOf(self), kFailBit, 0);
}
EXPORT THISCALL void fstreamDestroy(Ios* ios) { fstreamDestroyIos(ios); }

// --- String streams (the games construct them inline; these are their
// destructors)

EXPORT THISCALL void istrstreamDestroy(Ios* ios) {
    strstreambufDestroy((Strstreambuf*)((char*)ios - sizeof(Strstreambuf)));
    destroyIstream(ios);
}
EXPORT THISCALL void ostrstreamDestroy(Ios* ios) {
    strstreambufDestroy((Strstreambuf*)((char*)ios - sizeof(Strstreambuf)));
    destroyOstream(ios);
}

// --- cerr -----------------------------------------------------------------------------------
// An ostream over stderr, unbuffered (unitbuf), made at start-up.

struct StandardOstream {
    Ostream stream;
    Ios ios;
};
EXPORT StandardOstream standardError = {}; // cerr
namespace {
Filebuf gErrorBuffer;
} // namespace

extern "C" int __stdcall _DllMainCRTStartup(void*, unsigned reason, void*) {
    if (reason == 1) { // DLL_PROCESS_ATTACH
        buildTable();
        filebufConstruct(&gErrorBuffer, stdErr());
        ostreamConstruct(&standardError.stream, &gErrorBuffer.base, 1, 1, 1);
        standardError.ios.base.flags |= kUnitbuf;
    }
    return 1;
}
