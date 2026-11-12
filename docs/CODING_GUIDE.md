# Coding Guide

Rules for coding must be adhered to whenever possible. Rare exceptions may be made with prior approval.

## File Names

Use camel case starting with an upper case letter for all source and header files (matching class names).

* `ActorClass.cpp`
* `CoolGameFunction.h`

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

### Class Member Variables

Prefixed with `_` and in camel case starting with lowercase.

```cpp
char *_coolVariable;
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
int d, e;
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
namespace Reluva {

byte Actor::kRandomActor = 0;

void Actor::initActorClass(ReluvaLauncher *reluva) {
    _vm = reluva;
}

} // End of namespace Reluva
```

### Array delete operator has no whitespace before `[]`

```cpp
delete[] chaos;
```

### Template definitions

No whitespace between template keyword and `<`:

```cpp
template<typename chaos>
void myFunc(chaos arg) {
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
    // fall through
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
    Dialog::handleCommand(sender, cmd, data);
}
```
- Use the `fall through` comment for consistency and so that the compiler will see it and suppress potential warnings.

---

## Vertical Space

### Avoid composite one-liners

```cpp
if (condition) be_crazy();  // BAD

// GOOD
if (condition)
    be_crazy();
```

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
void processData(const Common::String &name);
```

---

## Documentation (Doxygen)

Use standard Doxygen blocks for public classes, methods, and structures to ensure clear documentation generation.

```cpp
/**
 * Brief description of what the class or method does.
 *
 * Detailed explanation of parameters, behavior, or return values.
 */
class EngineService {
public:
    /**
     * Initialize the service with target parameters.
     * @param mode The initialization mode flag.
     * @return True on success, false otherwise.
     */
    bool init(int mode);
};
```

---

## Special Keywords

The following keywords mark certain sections of the code:
- **FIXME**: Code that while functional needs to be replaced at some point.
- **TODO**: Items needing to be worked on at a later time.
- **WORKAROUND**: Code used for getting around old bugs or broken code in the original engine.
