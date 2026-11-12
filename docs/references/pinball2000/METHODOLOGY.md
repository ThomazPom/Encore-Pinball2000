# Preservation method

The archive answers one question: **what knowledge would materially hurt
Pinball 2000 understanding, operation, repair or historical research if its last
web source vanished?**

## Qualification gate

A candidate is accepted only when all of these questions have satisfactory
answers:

1. **Contribution:** what specific knowledge does it add?
2. **Authority:** is it official, first-hand, independently measured, specialist
   synthesis or an unverified community report?
3. **Uniqueness:** is the same information already preserved in a better source?
4. **Scope:** does it concern original Pinball 2000 hardware, XINA/software,
   games, service, updating, networking, tournament operation or verified
   history?
5. **Integrity:** can the original bytes, source URL, capture date, size and
   SHA-256 be retained?
6. **Readability:** can a person navigate and understand it offline?
7. **Risk:** does it unnecessarily expose personal data, credentials, licensed
   game payloads or unrelated software?

The possible decisions are:

| Decision | Meaning |
|---|---|
| `preserve` | unique or materially useful knowledge; keep readable and raw forms |
| `index-only` | useful lead, but redistribution, size or duplication argues against vendoring |
| `duplicate` | byte/content duplicate of a better preserved source |
| `unverified` | potentially useful claim awaiting a second source or a measurement |
| `reject` | noise, personal data, commercial residue, unrelated material or unsafe payload |

## Evidence classes

| Class | Source type | How it may be used |
|---|---|---|
| A | contemporary Williams/Bally manual, support page, bulletin or bytes | primary evidence, while still checking known errata |
| B | first-hand account by a named project participant | primary historical/engineering testimony; memory limits remain explicit |
| C | specialist repair or collector documentation with reproducible detail | accepted when internally coherent and preferably corroborated |
| D | forum post, owner report or unattributed archive | lead or qualified observation, not fact by repetition |
| E | Encore measurement, guest observation or binary/code analysis | direct evidence for the tested version and conditions only |

> [!WARNING]
> Authority is not infallibility. Factory manuals contain errors; memories can
> drift; a successful repair can be model-specific; an emulator observation can
> expose emulator behaviour rather than board behaviour. Conflicts remain
> visible until resolved.

## Bundle contract

An accepted web source is stored as a self-contained bundle:

```text
source-name/
├── README.md          generated readable derivative
├── raw/               unmodified captured page/native wiki source
├── assets/            only media and documents referenced by that source
├── manifest.json      provenance, roles, sizes and SHA-256
└── MANIFEST.sha256    independently checkable checksum list
```

The generator rejects missing assets and broken local links by default. A known
historical gap may be accepted only when recorded in `manifest.json`. It never
overwrites an existing bundle.

## Research protocol

Research is thematic rather than domain-limited:

1. inventory existing Encore links and the local FlipperFiles/collector corpus;
2. search the live web in English, German and French for missing technical and
   historical material;
3. follow outbound references and identify the earliest/most authoritative
   surviving version;
4. compare duplicates by SHA-256 and content, not filename;
5. enter a catalog decision before copying bytes into the archive;
6. generate and validate the bundle;
7. inspect the Markdown for hierarchy, tables, links and navigation;
8. extract durable knowledge into a topic page only after source review;
9. record conflicts and unknowns instead of smoothing them over.

Periodic refreshes compare new captures with the retained manifest. A changed
page is a new piece of evidence, not a reason to silently replace history.

## Explicit exclusions

- NuCore documentation, installers, packages and ROM sets;
- ROM/update payloads already governed by Encore's update-preservation process;
- owner lists, email directories and other unnecessary personal data;
- storefront pages, expired listings and generic pinball material;
- duplicated frame wrappers, thumbnails and branded PDF derivatives when the
  underlying source is already preserved;
- entire forum threads when one qualified post or attached artifact contains
  the durable knowledge.

[Knowledge archive](README.md) · [Source catalog](CATALOG.md) ·
[Preservation tooling](../../../tools/reference-preservation/README.md)
