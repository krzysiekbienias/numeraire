# Numeraire++ — coding style

We follow the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
with a small set of project-specific overrides:

| Topic | Project setting | Notes |
|---|---|---|
| Indentation | 4 spaces, no tabs | Set in `.clang-format` |
| Line length | 120 columns | Wider than Google's 80; better for modern monitors |
| Pointer alignment | `T* p`, `T& r` | Bound to type, not name |
| Includes | sorted, regrouped | C system → C++ stdlib → third-party → `<numeraire/...>` → local |
| Namespace closing comment | required | `}  // namespace foo` |

Mechanical rules are enforced by **`.clang-format`** (whitespace, layout) and
**`.clang-tidy`** (naming, bug/perf/modernize checks). Both run live in any
editor wired up to clangd (see `.vscode/settings.json`).

---

## Naming — five rules to memorize

1. **Functions / methods** → `PascalCase`
2. **Local variables, parameters** → `snake_case`
3. **Private / protected class fields** → `snake_case_` (with trailing `_`)
4. **Public struct fields** → `snake_case` (no trailing `_`)
5. **Constants** (`constexpr`, `const` at scope, enum values) → `kCamelCase`

Full table:

| Element | Convention | Example |
|---|---|---|
| Namespace | `lower_case` | `numeraire::utils` |
| Class / struct / enum / typedef | `PascalCase` | `PricingEngine`, `OptionType` |
| Function / method | `PascalCase` | `Generate()`, `SetCalendar(...)` |
| Variable, parameter, local | `snake_case` | `int retry_count;` |
| Public struct field | `snake_case` | `struct Foo { int output_path; };` |
| Private / protected class field | `snake_case_` | `int output_path_;` |
| Static class member (mutable) | `snake_case_` | `static int instance_count_;` |
| `constexpr` / `const` (any scope) | `kCamelCase` | `inline constexpr int kMaxPaths = 1000;` |
| Static class constant | `kCamelCase` | `static constexpr int kBufferSize = 128;` |
| Enum value | `kCamelCase` | `OptionType::kCall` |
| Macro | `SCREAMING_SNAKE_CASE` | `NUM_INFO(...)` |
| File / directory | `snake_case` | `pricing_engine.hpp` |
| Template parameter | `PascalCase` | `template <typename ProductT>` |

### Booleans

Prefer `is_/has_/can_` prefixes:

```cpp
bool is_business_day_;          // private member
bool has_dividends() const;     // method (still PascalCase: HasDividends())
```

### Getters / setters

Symmetric `Get/Set` pairs in PascalCase:

```cpp
const std::string& ProductId() const;          // simple getter
void SetProductId(std::string id);              // setter
```

STL-like accessors (`size()`, `empty()`, `begin()`) are an explicit
exception — only when the class genuinely mimics an STL container.

---

## struct vs class

Google C++ defines this with a strong intent:

- **`struct`** — passive aggregate. Public data, minimal logic. Fields **without** trailing `_`.
- **`class`**  — behavior with private state. Fields **with** trailing `_`.

```cpp
// passive data → struct
struct PricingResult {
    double                npv = 0.0;
    Currency              currency = Currency::kUsd;
    std::optional<Greeks> greeks;
};

// behavior + state → class
class PricingEngine {
   public:
    PricingResult Price(const IProduct& product, const IPricer& pricer, const IMarketData& market) const;

   private:
    std::chrono::milliseconds timeout_;
    mutable std::atomic<int>  call_count_{0};
};
```

---

## Other rules we live by

- **English everywhere** — identifiers, comments, commit messages, docs.
- **No raw `new`/`delete`** — use `std::unique_ptr`, `std::make_unique`,
  `std::shared_ptr` (only when shared ownership is real).
- **Exceptions over error codes** — derive from `numeraire::NumeraireException`
  (Sprint 1+).
- **`const` everything you can** — parameters, methods, locals.
- **No `using namespace` in headers**, ever.
- **Comments explain *why*, not *what*** — the code already says what.

---

## Tooling

| Tool | What it enforces | How to run |
|---|---|---|
| `clang-format` | Whitespace, indentation, includes, alignment | `scripts/format.sh` (or auto on save in editors using clangd) |
| `clang-tidy` | Naming, bugs, perf, modernize | live via clangd; `--clang-tidy` flag is set in `.vscode/settings.json` |
| Editor integration | Inline warnings as you type | install **clangd** extension; ignore `ms-vscode.cpptools` |

When clang-tidy flags a name, it tells you exactly which one to use, e.g.:

```
warning: invalid case style for function 'readAll' [readability-identifier-naming]
note: Did you mean 'ReadAll'?
```
