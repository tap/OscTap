# `oscpack/` — compatibility redirect shim

This directory is **not** the library source. The OscTap headers live under
[`../osctap/`](../osctap/) and are included as `<osctap/...>`.

Every header here is a thin redirect that simply `#include`s its `<osctap/...>`
counterpart, so existing code that still uses the old `<oscpack/...>` include
paths keeps compiling unchanged. This is the include-path half of the
oscpack→osctap compatibility moat (the other half is `namespace oscpack = osctap;`).

**Deprecated.** New code should include `<osctap/...>`. Do not delete this tree —
`tests/CompatIncludeShim.cpp` is the CI guard that keeps it working. See
[`../ROADMAP.md`](../ROADMAP.md) and [`../docs/STATUS.md`](../docs/STATUS.md).
