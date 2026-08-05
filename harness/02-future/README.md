# Future Feature Specifications (v2+)

> ⚠️ **Files here are intentionally NOT auto-loaded as LLM context.**
>
> All v1 stages (0–4, 13 features) are implemented and live in
> `harness/01-features/`. This directory now holds v2+ specs only.

## Current contents

None yet. Documented v2 candidates (see `bug_check/README.md` deferred list
and `docs/archive/qwen_featuer.md` §11):

| Candidate | Notes |
|-----------|-------|
| BLE connections (GATT client) | scan-only in v1 |
| USB suspend detection | no suspend signal exposed by USB-Serial/JTAG on this hardware |
| PMIC-based current measurement | v1 `POWER STATUS` figures are firmware estimates |
| WiFi standalone mode / web dashboard | v1 is PC-tethered by design |

When a v2 feature gets specced, add `feature_<name>.md` here and move it to
`harness/01-features/<stage>/` when implementation starts.
