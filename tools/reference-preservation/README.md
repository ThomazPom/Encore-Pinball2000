# External reference preservation

`reference-bundle.py` turns a MediaWiki page, a DokuWiki page, a static HTML
page or a local artifact into a self-contained evidence bundle. It can capture
a live page, import an older local snapshot or rebuild a readable derivative
from retained raw evidence.

> [!IMPORTANT]
> Capture is not acceptance. Inventory and qualify a source before adding its
> bundle to Encore. A technically working download may still be redundant,
> unreliable, private, out of scope or unsuitable for redistribution.

Each bundle contains:

```text
README.md          readable Markdown derivative
raw/               exact captured HTML and native wiki source
assets/            only referenced local media and documents
manifest.json      provenance, roles, sizes and SHA-256 values
MANIFEST.sha256    independent checksum list
```

The raw source remains the evidence. Markdown conversion is deliberately
repeatable and reviewable; it never replaces the captured bytes.

## Capture a live page

```sh
python3 tools/reference-preservation/reference-bundle.py capture \
  --kind dokuwiki \
  --url https://www.gerwiki.de/xina/p2k \
  --captured-at 2026-11-11 \
  --title "XINA and Pinball 2000" \
  --output /tmp/xina-p2k
```

`mediawiki` and `dokuwiki` derive their raw-source endpoint automatically.
Use `--raw-url` when a site has a nonstandard layout. `html` captures the page
plus directly referenced images and downloadable documents; it does not crawl
an entire site. ZIP/executable links are deliberately not downloaded by the
HTML collector.

Legacy pages frequently declare the wrong charset or use table-based layout.
The converter prefers valid UTF-8 bytes over a conflicting declaration, closes
browser-style implicit paragraphs/lists/tables and flattens one-column layout
tables into reading order. The raw page remains available when those heuristics
need review.

Use `--asset-base-url` only when a historical page has known broken relative
media paths whose correct base is independently established.

## Rebuild an older snapshot

```sh
python3 tools/reference-preservation/reference-bundle.py import \
  --kind mediawiki \
  --source /archive/pinwiki/source.wiki \
  --page-html /archive/pinwiki/page.html \
  --assets /archive/pinwiki/pics \
  --source-url https://pinwiki.com/wiki/index.php/Pinball_2000_Repair \
  --captured-at 2026-03-28 \
  --title "Pinball 2000 Repair" \
  --output /tmp/pinwiki-p2k
```

The importer resolves case differences, trailing underscores and invisible
Unicode formatting characters in old wiki media references. By default a
missing referenced asset is fatal. `--allow-missing-assets` records the gap in
the manifest and is only appropriate for an explicitly incomplete historical
snapshot.

## Validate

```sh
python3 tools/reference-preservation/reference-bundle.py validate /tmp/pinwiki-p2k
```

Validation checks the manifest file set, every byte count and SHA-256,
`MANIFEST.sha256`, local Markdown links and path safety. An output directory is
never overwritten.

## Preserve a local artifact

```sh
python3 tools/reference-preservation/reference-bundle.py artifact \
  --file /archive/manual.pdf \
  --source-url https://example.invalid/original-manual \
  --source-note 'Factory manual; local scan supplied by a collector.' \
  --captured-at 2026-11-11 \
  --title 'Example operations manual' \
  --description 'Operation, diagnostics and wiring.' \
  --output /tmp/example-manual
```

Identical artifact bytes are stored once even when more than one local copy is
provided during corpus triage. A source note must distinguish an official
document from an OCR derivative or an unverified community file.

## Rebuild from retained evidence

```sh
python3 tools/reference-preservation/reference-bundle.py rebuild \
  docs/references/pinball2000/sources/example-page \
  --output /tmp/example-page-rebuilt
```

`rebuild` reads the original raw source and assets, regenerates the Markdown and
creates a new manifest without contacting the network. This is the safe way to
apply converter improvements: validate and review the new bundle before
replacing the old one.

> [!CAUTION]
> `--allow-missing-assets` records an incomplete historical capture; it does not
> make the gap disappear. Use it only after local and web recovery attempts have
> failed, and document the missing URLs in the archive index.
