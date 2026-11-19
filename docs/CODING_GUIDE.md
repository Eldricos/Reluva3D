# Coding Guide

Rules for coding must be adhered to whenever possible. Rare exceptions may be made with prior approval.

## File Names

As in ScummVM: lower case for all source and header files and for folders, with words separated by underscores. A file holding one main class is named after it.

* `actor.cpp` (`class Actor`)
* `cool_game_function.h` (`class CoolGameFunction`)
* `engines/clue/game/board.cpp`, `video/smacker.h`

Every file starts with its license and copyright lines, and every header has `#pragma once`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once
```

That goes for every source file, not only C++: Python, shell and CMake
files in `#` comments (after a `#!` line), QVM assembly and `.def` files
in `;` ones. `tools/check-headers.py` checks them all, and `--fix` adds
the lines where they're missing.

---

## Namespaces

Lower case, nested with `::`. Everything is in `reluva`; an engine's code is in `reluva::<engine>`, named like its folder.

```cpp
namespace reluva::clue {
```

The closing brace says which namespace it closes (clang-format adds this):

```cpp
} // namespace reluva::clue
```

---

## Naming

### Constants

* Camel case with prefix `k`: `kRandomConstant`

(Avoid `#define` for creating constants. Instead use enums or `const`.)

### Type Names

Camel case starting with upper case. This includes template types.

```cpp
typedef int AnInt;
class AClass { /* ... */ };
struct AStruct { /* ... */ };

template<typename CoolThing>
void theseCool(CoolThing thing) { /* ... */ }
```

Values of an `enum` or `enum class` are constants: `kRed`, `kGreen`.

### Class Member Variables

Prefixed with `_` and in camel case starting with lowercase.

```cpp
char *_coolVariable;
```

The prefix is for private and protected members. Public data members, the fields of a plain struct, have no prefix:

```cpp
struct GameTarget {
    std::string gameId;
    std::filesystem::path path;
};
```

### Class Methods

Camel case starting with lowercase.

```cpp
void coolRandomMethod();
```

### Non-Member Functions

Camel case starting with lowercase.

```cpp
void coolGlobalFunction();
```

### Local Variables

Use camel case.

```cpp
char *coolVariable;
```

### Global Variables

Avoid global variables unless no other options work. Use the `g_` prefix for them.

```cpp
int g_someGlobalVariable;
```

### Acronyms

An acronym keeps its capitals, as in ScummVM: `kRGBA32`, `kCLUT8`, `kUK`, `HUDScreen`, `loadBMP()`. A name that starts with one is still camel case: `rgbImage`.

### Exceptions

Some names keep the spelling something outside Reluva gives them:

* Names the C++ standard library looks for: `value_type`, `result_type`, `is_transparent` and the like.
* Functions of the compiler's runtime that Reluva declares: `__register_frame`.
* The x86 processor's registers and flags, as its manuals name them: `reg[EAX]`, `eflags & CF`.
* A formula's or a specification's notation, in a local variable: one capital letter, perhaps with a digit or a letter (`S`, `T`, `Q` texture coordinates, `Y`, `U`, `V` planes, `Xc`), and Indeo's transform terms (`b0_1`).

`.clang-tidy` knows these. Any other name that has to stay as it is gets a `// NOLINT(readability-identifier-naming)` saying why.

---

## Braces

Braces should always look like these:

```cpp
for (int i = 0; i < t; i++) {
    // ...
}

if (j < k) {
    // ...
} else if (j > k) {
    // ...
} else {
    // ...
}

class Cool {
    // ...
};
```

---

## Indents Must Be Four Spaces

---

## Semicolons Must Be Followed By A New Line

No continuing lines of code.

```cpp
a = (b + c) * d;
e = (a - f); // GOOD
```

---

## Whitespaces

### Operators surrounded by a space character

```cpp
a = (b + c) * d;
```

### C++ reserved words separated from opening parentheses by a white space

```cpp
while (true) {
    // ...
}
```

### Commas followed by a white space

```cpp
someFunction(a, b, c);
```

### Comment operators followed by a space character

```cpp
// C++-style comment

/* C-style comment */

/*
 * Multiline C-style comments have vertical alignment of the star symbols
 */
```

### Mandatory `{}` for empty `for`/`while` loops

```cpp
while (i < length - 1 && array[++i] != item) {} // GOOD
while (i < length - 1 && array[++i] != item);   // BAD
```

### When declaring class inheritance and in a `?` construct, colons must be surrounded by white space

```cpp
class PowerUp : public PlayerArmor {
    // ...
};

(isRaining) ? giveUmbrella() : stayDry();
```

### Indentation level is not increased after namespace clause

```cpp
namespace reluva {

const int Actor::kMaxActors = 32;

void Actor::initActorClass(Engine *engine) {
    _engine = engine;
}

} // namespace reluva
```

### Array delete operator has no whitespace before `[]`

```cpp
delete[] chaos;
```

### Template definitions

No whitespace between template keyword and `<`:

```cpp
template<typename Chaos>
void myFunc(Chaos arg) {
    // ...
}
```

### Operator overloading

Operator keyword is NOT separated from the name, except for type conversion operators where it is required.

```cpp
struct Chaoss {
    void operator()() {
        // ...
    }

    operator bool() {
        return true;
    }
};
```

### Pointers and casts

No whitespace after a cast; and in a pointer, write a whitespace before the star, not after it.

```cpp
const char *ptr = (const char *)chaos;
```

### References

Use the same rule for references as for pointers: a whitespace before the `&` but not after it.

```cpp
int i = 0;
int &ref = i;
```

### Arrow operator

Put no spaces around the arrow operator.

```cpp
int a = foo->bar;
```

---

## Switch/Case Constructs

```cpp
switch (cmd) {
case kThisCrazy:
    a = 1;
    func1();
    b = a + 1;
    [[fallthrough]];
case kThisAlsoCrazy:
    moreCrazy();
    a = 3;
    break;
case kSaveCmd:
    save();
    break;
case kLoadCmd:
case kPlayCmd:
    close();
    break;
default:
    Menu::handleCommand(sender, cmd, data);
}
```

- Mark every fall through with `[[fallthrough]];`. Every compiler understands it and checks it; a `// fall through` comment is only understood by GCC.

---

## Vertical Space

### Avoid composite one-liners

```cpp
if (condition) beCrazy();  // BAD

// GOOD
if (condition)
    beCrazy();
```

### Exception: short functions in a class

A function defined inside its class, whose body is one short statement, may stay on one line. So may a short lambda (an argument, or an entry in a list such as a test suite's cases). Everywhere else the semicolon rule applies.

```cpp
class Actor {
public:
    int health() const { return _health; }       // GOOD
    void setHealth(int health) { _health = health; } // GOOD
};

std::sort(list.begin(), list.end(), [](int a, int b) { return a > b; }); // GOOD

int Actor::health() const { return _health; }   // BAD: not inside the class
```

---

## Line Length

Lines are at most 120 characters. clang-format wraps longer ones.

### Split variable declarations

```cpp
int a = 1;
int b = 2;
int c;

c = a + b;
```

### Split logical program blocks

```cpp
int coolGameFunction(float importantVariable) {
    float coolVariable = importantVariable;
    int otherVariable = 10;

    if (importantVariable > 1) {
        otherVariable = 20;
    } else {
        otherVariable = otherVariable + 30;
    }

    return otherVariable;
}
```

---

## Preprocessor Pragmas

All preprocessor pragmas must start from the first column:

```cpp
#include "common/system.h"

#pragma mark --- Start of example ---
void example() {
    int i = 0;

#ifdef USE_FEATURE
    Feature *f = new Feature();
#endif

    for (int x = 0; x < 10; x++) {
#define CRAZY f
        warning("%d", (CRAZY).code());
#undef CRAZY
    }
}
```

---

Macros are in upper case, and Reluva's own start with `RELUVA_`: `RELUVA_FORCE_INLINE`.

---

## Modern C++ & Best Practices

### Null Pointers

Always use `nullptr` instead of `NULL` or integer `0` for pointer initializations and comparisons.
```cpp
Actor *currentActor = nullptr;
if (currentActor == nullptr) {
    // ...
}
```

### Override and Final Specifiers

Always append the `override` keyword when overriding virtual functions from a base class.
```cpp
void update() override;
```

### Const-Correctness

* Mark all member functions that do not mutate object state as `const`.
* Pass non-primitive parameters by `const &` when they are not modified.
```cpp
int getHealth() const;
void processData(const std::string &name);
```

---

## Comments

These rules are for everyone writing code here, people and AI tools alike,
so comments read the same whoever wrote them. `tools/comments.py` flags
the breaches a script can be sure of (see [DEVELOPMENT.md](DEVELOPMENT.md#code-style)).

### Say why, not what

A comment says what the code can't: never what a reader sees by reading
the code.

```cpp
// Bad: says what the code says
++lap; // add one to the lap
if (speed > kMaxSpeed) // if the speed is more than the most
    speed = kMaxSpeed;

// Good: says why
++lap; // the original counts laps from 1 (0x42cc20)
if (speed > kMaxSpeed) // a car pushed by another can go faster for a tick
    speed = kMaxSpeed;
```

### Only what isn't obvious from the code

Comment only where reading the code won't tell you:

- **why** it is done this way, or why the obvious way isn't used;
- **edge cases**: what happens at 0, at the end, when a file is short;
- **units and ranges**: `// seconds`, `// 0-255`, `// in 1/256ths of a pixel`;
- **ownership and lifetime**: who frees it, how long a pointer stays good;
- **threads**: what may be called from where;
- **where a fact came from**: the original program, a file comparison, a recording (below);
- **the game's own rules**, where the code alone would look arbitrary.

```cpp
// Bad: nothing the code doesn't say
// Loop over the entries.
for (const Entry &e : entries) {

// Good: what the code can't say
// Entries past the first empty one are left over from an earlier save:
// the original stops there too (0x41a860).
for (const Entry &e : entries) {
```

### Short and plain

One line where one line does it; plain sentences. No banners, boxes or
lines of `=` or `*` between parts of a file.

```cpp
// Bad
/*************************************************
 *              Helper Functions                 *
 *************************************************/

// Good: nothing, or a plain line when it helps
// Reading a level's tiles.
```

### Notes from the original program

Engines are worked out from the original games, and many comments say
where in the original program something was found. Give the address in
the original executable in parentheses after what it says, with the
original function's name in front where it is known; a structure's field
gives its offset in the original's structure.

```cpp
fogAt(roadSliceAt(), fog, fogDensity); // by the camera's road slice (0x478e10)
// cop_ai_pick_target (0x402e10): a racer within 16 slices, free of other cops.
struct ParticleTemplate {
    int count = 0; // +0x00 a burst's count (0x4cac20)
    int life = 0;  // +0x04 ticks
};
```

How a fact was found, and how sure it is, goes in the engine's pages in
`docs/` (see [CONTRIBUTING.md](../CONTRIBUTING.md)): a comment can point
there, but doesn't hold the whole story.

### Never from leaked source

A comment never quotes, paraphrases or names anything from leaked or
unlicensed source code: not its names, its comments, nor what it says the
code does. This holds for AI tools too, which may have seen such source;
see [CONTRIBUTING.md](../CONTRIBUTING.md) (Not allowed).

### Never written

- **Commented-out code.** Delete it: git keeps it.
- **History.** "Fixed the crash", "previously 1", "changed from…", "new:"
  are for the commit message. A comment says what the code is now.
- **A `TODO` or `FIXME` a stranger couldn't act on.** Say what's missing
  and why it was left.

```cpp
// Bad
// x = oldRead(f);
int y = 0; // previously 1
// TODO

// Good
// TODO: the second channel; every file seen so far has one.
```

Code examples inside a comment, showing how to use what it describes,
aren't commented-out code: indent them under the comment's text.

```cpp
// A run written as a record of "step: value" lines:
//
//     test::Record rec;
//     rec.add("maps.read", count);
```

When code changes, change or delete the comments it makes wrong: a stale
comment is worse than none.

## Documentation (Doxygen)

Public classes, methods and structures, in headers, get Doxygen blocks;
everything else (private members, code inside functions, `.cpp` files)
plain `//` comments. The rules above hold for both.

New code uses Doxygen blocks from the start. Existing code has plain `//` comments: when you work on a file, turn the comments on its public classes, methods and structures into Doxygen blocks.

A block starts with one sentence saying what it is or does, then more
only if there is more to say. Use `@param` and `@return` only when they
say more than the name and type already do: a range, a unit, what 0 or
`nullptr` means, what happens on failure. Leave them out otherwise.

```cpp
/**
 * A level's tiles and the things on them, read from its .lvl file.
 *
 * Holds the file's data while it is alive: the tiles point into it.
 */
class Level {
public:
    /**
     * Reads the level, checking its size against the header's.
     * @param skipItems Leave out the items (as the editor's preview does).
     * @return An error naming the first part that didn't read.
     */
    Error read(const std::string &path, bool skipItems = false);

    /** The number of tiles across. */
    int width() const { return _width; }
};

// Bad: tags that say only the name
/**
 * Sets the size.
 * @param size The size.
 * @return The result.
 */
```

---

## Special Keywords

The following keywords mark certain sections of the code:
- **FIXME**: Code that while functional needs to be replaced at some point.
- **TODO**: Items needing to be worked on at a later time: say what is missing and why it was left.
- **WORKAROUND**: Code used for getting around old bugs or broken code in the original engine.
