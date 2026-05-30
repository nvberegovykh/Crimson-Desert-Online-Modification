# Contributing

Thanks for your interest in improving the Crimson Desert Online Modification.

## Ground rules

- Read [LEGAL.md](LEGAL.md) first. **Never** commit game assets, decompiled game
  code, hardcoded offsets from a leaked build, or anything that circumvents DRM
  or anti-cheat. PRs containing such material will be rejected.
- Every source file carries an SPDX header: `// SPDX-License-Identifier: MIT`.
- Be respectful; see [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

## Development setup

See [docs/setup.md](docs/setup.md) for building the server and client.

## Coding standards

### Server (TypeScript)
- `strict` mode is on. Avoid `any` unless genuinely unavoidable.
- Run `npm run build` before opening a PR; the build must be clean.
- Keep packets defined in `protocol.ts` and mirror any new packet in the C++
  `net/protocol.*` files.

### Client (C++17)
- Follow the existing style: 4-space indent, `cdmp` namespace, header + cpp
  pairs.
- All memory access must go through the SEH-guarded helpers in `game/memory`.
- Never block in `DllMain`; do work on the init thread.
- Hooks use MinHook and must be removed cleanly on shutdown.

## Commit & PR process

1. Branch from `main`.
2. Make focused commits with clear messages.
3. Ensure CI (server build + client build) passes.
4. Open a PR describing the change and how you tested it. Do **not** self-approve.

## Reporting bugs / requesting features

Use the GitHub issue templates under `.github/ISSUE_TEMPLATE/`.
