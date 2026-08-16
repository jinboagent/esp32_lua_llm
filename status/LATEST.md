# LATEST — Status Pointer

**Current status:** [`status-2026-08-16-0204.md`](status-2026-08-16-0204.md)

## At a glance

- **Commit-message standard now enforced**: `.githooks/commit-msg` rejects missing sections / bad subjects; enable per clone with `git config core.hooksPath .githooks` (README documents it)
- **History rewritten (last 14 commits)**: all messages since the CR terminator fix now carry the full 4-section standard; content byte-identical; force-pushed with lease (old tip 5f94dff → new tip 2c55ef0); local backup branch `backup/pre-msg-rewrite-2026-08-16` kept
- **Details:** `docs/commit-message-standard-2026-08-16.md`
- **Firmware unchanged** — v1.0.0 + pool rewrite + lock decoupling, host **86/86**, all HW suites green
- **Note (kept):** closing the COM port resets the chip (N3) — suites and soak keep the port open
- **Next:** optional 2 h re-soak of the new allocator; then LLM-loop field test
