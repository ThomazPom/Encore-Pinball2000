# Encore — third-party reference material

This folder vendors / mirrors external Pinball 2000 documentation and code
that Encore relies on. Cited sources have a habit of disappearing
(forum threads close, wikis move, personal sites lapse), so we keep a
local copy under licence-compatible terms.

| Path | Upstream | Licence | Mirrored | Why |
|------|---------|--------|---------|----|
| `PinballDiag/` | <https://github.com/boilerbots/PinballDiag> | BSD 3-Clause | 2026-04-24 | Working Linux C++ reference for the PB2K LPT protocol (ioperm + raw `inb`/`outb`, busy-wait strobes). Authoritative source for register map and bit polarities. |
| `web_archive/flipprojets_p2000_driver_tester.md` | <https://www.flipprojets.fr/TestP2000Driver_EN.php> | Page text quoted under fair-use for technical preservation | 2026-04-24 | Independent confirmation that a 20 MHz PIC can drive the board — useful sanity check for host-side timing budgets. |
| `web_archive/pinwiki_p2k_repair.md` | <https://pinwiki.com/wiki/index.php?title=Pinball_2000_Repair> | PinWiki community wiki — see source page | 2026-04-24 | Power Driver Board LED map (LED1 = blanking/watchdog), chipset compatibility (CX5520 only), system overview. |

## Refetch / verify

Each mirrored file carries a `Source:` URL and a `Mirrored on:` date in
its header. To refetch and compare:

```bash
# Plain text mirrors:
curl -sL <source-url> | diff - docs/references/web_archive/<file>.md

# PinballDiag (whole repo):
git clone https://github.com/boilerbots/PinballDiag /tmp/pd-upstream
diff -ru docs/references/PinballDiag/ /tmp/pd-upstream/
```

## Adding a new mirror

1. Choose the right subfolder:
   - `web_archive/` for static pages (HTML / wiki / forum text).
   - A new top-level subfolder for full source repos.
2. Add a header to your mirror file with: `Source:`, `Mirrored on:`,
   licence note, and a one-line `Why:` justifying preservation.
3. If the upstream is a binary (PDF, image archive, large ROM tooling),
   do **not** commit it here. Instead add an entry to the planned
   `binary-sources.md` index with URL + suggested SHA-256 so users can
   fetch it independently.
4. Update this README's table.
5. Run the project's forbidden-term scrub before committing.
