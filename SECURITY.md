# Security Policy

## Supported versions

This is a pre-1.0 research project. Only the latest `main` is supported.

## Reporting a vulnerability

If you discover a security issue — for example, a way for a malicious client to
crash the server, spoof another player's identity, bypass host authority for
combat/loot, or trigger memory corruption in the client plugin — please report
it privately:

1. Open a GitHub security advisory ("Report a vulnerability") on the repository,
   **or**
2. Open a minimal issue asking a maintainer to contact you, without disclosing
   details publicly.

Please include:

- A description of the issue and its impact.
- Steps to reproduce (a minimal packet sequence or repro is ideal).
- The affected component (server / client) and version/commit.

We aim to acknowledge reports within a few days and to address confirmed issues
promptly.

## Scope & hardening notes

The server performs shallow packet validation and enforces that:

- Clients may only update **their own** player state.
- Only the **host** may submit enemy state, adjudicate damage, and announce loot.
- Admin mutating routes require the `X-Admin-Token` header.

The client plugin routes **all** game-memory access through SEH-guarded helpers
and fails closed when the signature scanner cannot resolve an offset.

Out of scope: vulnerabilities in third-party dependencies (report upstream), and
any issue that requires the attacker to already have code execution on the host
machine.
