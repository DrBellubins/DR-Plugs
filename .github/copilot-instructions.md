## Code Style

### Braces (Modified Allman Style)
- Use Allman-style brace placement: opening brace on its own line.
- **Only** use braces for `if`, `else`, and loop bodies that span **more than one line**.
- Single-line `if`/loop bodies must be written without braces, on the same line or the next.

```cpp
// ✅ Multi-line body — braces required
if (condition)
{
    doSomething();
    doSomethingElse();
}

// ✅ Single-line body — no braces
if (condition)
    doSomething();

// ✅ Loop with multiple lines
for (int i = 0; i < count; i++)
{
    process(i);
    log(i);
}

// ✅ Loop with one line — no braces
for (int i = 0; i < count; i++)
    process(i);
```

---

### Naming Conventions

#### Variables & Parameters — No Abbreviations
- **Never** use shortened or cryptic names. Names must be fully readable and self-documenting.
- Short abbreviations like `hc`, `g`, `btn`, `mgr`, `ctx` are forbidden.
- Simplified names are acceptable **only when they remain clearly understandable** in context.

```cpp
// ❌ Forbidden
HelpfulClass hc;
JuceGraphics g;
AudioManager am;

// ✅ Required
HelpfulClass helpfulClass;
JuceGraphics graphics;
AudioManager audioManager;
```

#### Access-Based Casing
- **Public** members (variables, functions): `PascalCase`
- **Private** members (variables, functions): `camelCase`
- **Local variables** inside functions: `camelCase`

```cpp
class MyComponent
{
public:
    int ItemCount;
    void RenderFrame();

private:
    int itemCount;
    void renderFrame();

    void someFunction()
    {
        int localValue = 0;
        float deltaTime = GetDelta();
    }
};
```