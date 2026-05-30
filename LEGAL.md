# Legal Notice

This document describes the intended, permitted use of the Crimson Desert Online
Modification project and the obligations of anyone who uses, builds, or
distributes it. It is **not** legal advice. If you are unsure whether your use
is lawful in your jurisdiction, consult a qualified attorney.

## Permitted use

- **Private research and personal testing only.** This project exists to study
  game networking, the BLACKSPACE engine, and self-hosted multiplayer design.
- **Every participant must own a legitimate copy of Crimson Desert.** The mod
  does not contain, distribute, or enable access to any game asset, executable,
  or copyrighted content.
- **Self-hosted, non-commercial sessions among consenting owners** of the game.

## Prohibited use

- **Redistribution of game assets.** Do not bundle, copy, or share any Pearl
  Abyss-owned art, audio, code, or data with this project.
- **Authentication or DRM bypass.** This project does not circumvent Denuvo,
  Steam, or any account/licensing system, and must not be modified to do so.
  The proxy DLL technique loads alongside the unmodified game binary.
- **Monetization.** Do not sell, rent, or charge for access to this software,
  servers running it, or any derivative.
- **Trademark use.** "Crimson Desert", "Pearl Abyss", and "BLACKSPACE" are
  trademarks of their respective owners. Do not imply endorsement or
  affiliation. This is an unofficial, fan-made project.
- **Cheating in official/online services.** Do not use this against Pearl Abyss
  official servers or to gain advantage in any official online mode.

## Pearl Abyss EULA considerations

Use of Crimson Desert is governed by Pearl Abyss's End User License Agreement
and Terms of Service. Those terms may prohibit modification, reverse
engineering, or third-party software. **You are responsible for reading and
complying with the publisher's terms.** Using this mod may violate those terms
and could put your game account at risk. Use it only in offline/private contexts
where you accept that risk.

## DMCA / safe harbor notes

- This repository contains only original source code authored by its
  contributors plus references to independently licensed open-source libraries
  (ImGui, MinHook, nlohmann/json). It contains no decompiled game code and no
  copyrighted game content.
- Memory offsets are **not** shipped; they are discovered at runtime by the
  signature scanner on the user's own machine, from the user's own legally
  obtained copy. No proprietary data is redistributed.
- If you are a rights holder and believe content here infringes your rights,
  open an issue or file a DMCA notice and the maintainers will respond promptly.

## Public release gate checklist

Before any public/binary release, confirm all of the following:

- [ ] No game assets, strings, or decompiled code are present in the repo.
- [ ] No hardcoded memory offsets derived from a leaked/pre-release build.
- [ ] No anti-cheat or DRM circumvention logic.
- [ ] LEGAL.md, README legal notice, and per-file SPDX headers are intact.
- [ ] Third-party license texts are included/credited.
- [ ] The build produces only original code; dependencies are fetched, not
      vendored from unknown sources.
- [ ] A clear "requires game ownership / private use" notice is shown to users.

## Disclaimer

THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND. THE AUTHORS ARE
NOT AFFILIATED WITH PEARL ABYSS. USE AT YOUR OWN RISK. THE AUTHORS ACCEPT NO
LIABILITY FOR ACCOUNT ACTIONS, DATA LOSS, OR ANY DAMAGES ARISING FROM USE OF
THIS SOFTWARE.
