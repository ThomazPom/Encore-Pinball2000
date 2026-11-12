#!/usr/bin/env python3
"""Capture, import and validate readable external-reference bundles.

The original bytes are evidence.  README.md is a derived, readable view.  A
manifest binds both to their provenance and SHA-256 digests.
"""

from __future__ import annotations

import argparse
import hashlib
import html
from html.parser import HTMLParser
import json
import mimetypes
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import sys
import tempfile
import unicodedata
from urllib.parse import quote, unquote, urljoin, urlparse
from urllib.request import Request, urlopen


VERSION = "1.2"
KINDS = ("mediawiki", "dokuwiki", "html")
VOID_TAGS = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr",
}
ASSET_EXTENSIONS = {
    ".bmp", ".csv", ".gif", ".jpeg", ".jpg", ".odt", ".pdf", ".png",
    ".svg", ".tif", ".tiff", ".txt", ".webp",
}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def decode_source_bytes(data: bytes, kind: str) -> str:
    """Decode text for derivation while retaining the untouched raw bytes."""
    # Prefer valid UTF-8 even when a legacy template still advertises Latin-1.
    # Several surviving JSP pages do exactly that.
    encodings = ["utf-8-sig"]
    if kind == "html":
        header = data[:8192].decode("ascii", "ignore")
        match = re.search(
            r"charset\s*=\s*[\"']?\s*([a-zA-Z0-9._-]+)", header, re.I)
        if match:
            encodings.append(match.group(1))
    encodings.append("cp1252")
    for encoding in dict.fromkeys(encodings):
        try:
            return data.decode(encoding)
        except (LookupError, UnicodeDecodeError):
            continue
    return data.decode("utf-8", "replace")


def normalize_asset_key(value: str) -> str:
    """Normalize wiki/browser filename damage without changing stored bytes."""
    value = unquote(value).split("#", 1)[0].split("?", 1)[0]
    value = value.replace("\\", "/").rsplit("/", 1)[-1]
    # DokuWiki media identifiers use namespaces such as
    # ``:xina:pub_card_user_guide.odt``.  The downloaded file itself only
    # carries the final component.
    value = value.rsplit(":", 1)[-1]
    value = unicodedata.normalize("NFC", value)
    value = "".join(ch for ch in value if unicodedata.category(ch) != "Cf")
    value = value.strip().rstrip("_").strip()
    # MediaWiki displays spaces in titles but commonly stores the downloaded
    # file with underscores.  They denote the same title there.
    value = re.sub(r"[ _]+", "_", value)
    return value.casefold()


def safe_filename(value: str, fallback: str = "asset") -> str:
    value = unquote(value).replace("\\", "/").rsplit("/", 1)[-1]
    value = unicodedata.normalize("NFC", value)
    value = "".join(ch for ch in value if unicodedata.category(ch) != "Cf")
    value = re.sub(r"[\x00-\x1f\x7f/]", "_", value).strip().rstrip(".")
    return value or fallback


class AssetResolver:
    def __init__(self, files: list[Path]):
        self.files = files
        self.by_key: dict[str, list[Path]] = {}
        for path in files:
            self.by_key.setdefault(normalize_asset_key(path.name), []).append(path)

    def resolve(self, reference: str) -> Path | None:
        matches = self.by_key.get(normalize_asset_key(reference), [])
        if len(matches) == 1:
            return matches[0]
        if len(matches) > 1:
            exact = [p for p in matches if p.name == safe_filename(reference)]
            if len(exact) == 1:
                return exact[0]
            raise ValueError(f"ambiguous asset reference {reference!r}: {matches}")
        return None


class Node:
    def __init__(self, tag: str = "", attrs=None, parent=None):
        self.tag = tag
        self.attrs = dict(attrs or [])
        self.parent = parent
        self.children: list[Node | str] = []


class LooseHTMLTree(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = Node("document")
        self.current = self.root

    def close_open(self, tags: set[str], stop_tags: set[str] | None = None):
        cursor = self.current
        while cursor is not self.root:
            if cursor.tag in tags:
                self.current = cursor.parent
                return
            if stop_tags and cursor.tag in stop_tags:
                return
            cursor = cursor.parent

    def handle_starttag(self, tag, attrs):
        tag = tag.lower()
        blocks_close_p = {
            "address", "article", "aside", "blockquote", "div", "dl",
            "fieldset", "figcaption", "figure", "footer", "form",
            "h1", "h2", "h3", "h4", "h5", "h6", "header", "hr",
            "main", "nav", "ol", "p", "pre", "section", "table", "ul",
        }
        if tag in blocks_close_p:
            self.close_open({"p"})
        if tag == "li":
            self.close_open({"li"}, {"ul", "ol"})
        elif tag == "tr":
            self.close_open({"tr"}, {"table"})
        elif tag in {"td", "th"}:
            self.close_open({"td", "th"}, {"tr", "table"})
        elif tag in {"h1", "h2", "h3", "h4", "h5", "h6"}:
            self.close_open({"h1", "h2", "h3", "h4", "h5", "h6"})
        node = Node(tag, attrs, self.current)
        self.current.children.append(node)
        if tag not in VOID_TAGS:
            self.current = node

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if tag.lower() not in VOID_TAGS:
            self.handle_endtag(tag)

    def handle_endtag(self, tag):
        tag = tag.lower()
        cursor = self.current
        while cursor is not self.root:
            if cursor.tag == tag:
                self.current = cursor.parent
                return
            cursor = cursor.parent

    def handle_data(self, data):
        self.current.children.append(data)


def node_text(node: Node | str) -> str:
    if isinstance(node, str):
        return node
    if node.tag in {"script", "style", "noscript", "svg"}:
        return ""
    return "".join(node_text(child) for child in node.children)


def clean_inline(value: str) -> str:
    return re.sub(r"\s+", " ", value).strip()


def clean_paragraph(value: str) -> str:
    """Collapse source wrapping but retain intentional HTML ``br`` breaks."""
    marker = "\x00BR\x00"
    value = value.replace("  \n", marker)
    value = re.sub(r"\s+", " ", value).strip()
    value = re.sub(re.escape(marker) + r"(?:\s*" + re.escape(marker) + r")+",
                   "\n\n", value)
    return value.replace(marker, "  \n").strip()


def render_html_node(node: Node | str, base_url: str, asset_names: dict[str, str],
                     list_depth: int = 0) -> str:
    if isinstance(node, str):
        # Source indentation is presentation, not content.  Keeping it turns
        # wrapped 1990s HTML prose into Markdown code blocks and indents later
        # list items according to the original tag indentation.
        return re.sub(r"\s+", " ", html.unescape(node)) if node.strip() else ""
    tag = node.tag
    if tag in {"script", "style", "noscript", "svg", "head"}:
        return ""
    if tag == "img":
        src = node.attrs.get("src", "")
        alt = clean_inline(node.attrs.get("alt", "")) or safe_filename(src, "image")
        target = asset_names.get(urljoin(base_url, src), urljoin(base_url, src))
        return f"![{alt}]({target})"
    if tag == "br":
        return "  \n"
    if tag == "hr":
        return "\n\n---\n\n"
    if tag == "table":
        owned_rows = list(table_rows(node))
        owned_cells = [cell for tr in owned_rows for cell in tr.children
                       if isinstance(cell, Node) and cell.tag in {"th", "td"}]
        block_tags = {"h1", "h2", "h3", "h4", "h5", "h6", "p", "div",
                      "ul", "ol", "table"}
        row_widths = [sum(isinstance(cell, Node) and cell.tag in {"th", "td"}
                          for cell in row.children) for row in owned_rows]
        has_blocks = any(any(True for _ in walk_nodes(cell, block_tag))
                         for cell in owned_cells for block_tag in block_tags)
        identity = " ".join((node.attrs.get("id", ""), node.attrs.get("class", "")))
        layout_table = (
            (len(owned_rows) <= 2 and has_blocks) or
            (row_widths and max(row_widths) <= 1 and len(owned_rows) > 1) or
            re.search(r"(?:^|[-_ ])(?:layout|box|content)(?:$|[-_ ])",
                      identity, re.I)
        )
        if layout_table:
            # Presentation tables were the standard page-layout primitive on
            # the surviving Williams-era sites.  Rendering those as one giant
            # Markdown cell destroys paragraph and heading structure.
            parts = [render_html_node(cell, base_url, asset_names).strip()
                     for cell in owned_cells]
            return "\n\n" + "\n\n".join(part for part in parts if part) + "\n\n"

        rows = []
        for tr in owned_rows:
            cells = [clean_inline(render_children(cell, base_url, asset_names))
                     for cell in tr.children
                     if isinstance(cell, Node) and cell.tag in {"th", "td"}]
            if cells:
                rows.append(cells)
        if not rows:
            return ""
        width = max(map(len, rows))
        lines = []
        for index, row in enumerate(rows):
            row += [""] * (width - len(row))
            row = [cell.replace("|", "\\|") for cell in row]
            lines.append("| " + " | ".join(row) + " |")
            if index == 0:
                lines.append("| " + " | ".join(["---"] * width) + " |")
        return "\n\n" + "\n".join(lines) + "\n\n"

    body = render_children(node, base_url, asset_names, list_depth)
    identity = " ".join((node.attrs.get("id", ""), node.attrs.get("class", "")))
    if tag in {"td", "th", "div"} and re.search(
            r"(?:^|[-_ ])pagehead(?:$|[-_ ])", identity, re.I):
        label = clean_inline(node_text(node))
        return f"\n\n### {label}\n\n" if label else ""
    if tag in {"h1", "h2", "h3", "h4", "h5", "h6"}:
        return f"\n\n{'#' * int(tag[1])} {clean_inline(body)}\n\n"
    if tag == "p":
        paragraph = clean_paragraph(body)
        return f"\n\n{paragraph}\n\n" if paragraph else ""
    if tag in {"div", "section", "article", "main", "header", "footer",
               "figure", "figcaption", "address", "details", "summary"}:
        return f"\n\n{body.strip()}\n\n" if body.strip() else ""
    if tag in {"strong", "b"}:
        suffix = "  \n" if "  \n" in body else ""
        return f"**{clean_inline(body)}**{suffix}"
    if tag in {"em", "i"}:
        return f"*{clean_inline(body)}*"
    if tag == "code" and node.parent and node.parent.tag != "pre":
        return f"`{body.strip()}`"
    if tag == "pre":
        return f"\n\n```\n{node_text(node).strip()}\n```\n\n"
    if tag == "blockquote":
        lines = clean_markdown(body).splitlines()
        return "\n\n" + "\n".join("> " + line for line in lines) + "\n\n"
    if tag == "a":
        label = clean_inline(body) or clean_inline(node.attrs.get("title", ""))
        href = node.attrs.get("href", "")
        if not href:
            return label
        absolute = urljoin(base_url, href)
        target = asset_names.get(absolute, absolute)
        return f"[{label or target}]({target})"
    if tag in {"ul", "ol"}:
        return f"\n{body}\n"
    if tag == "li":
        marker = "1." if node.parent and node.parent.tag == "ol" else "-"
        inline = "".join(
            render_html_node(child, base_url, asset_names, list_depth)
            for child in node.children
            if not (isinstance(child, Node) and child.tag in {"ul", "ol"}))
        nested = "".join(
            render_html_node(child, base_url, asset_names, list_depth)
            for child in node.children
            if isinstance(child, Node) and child.tag in {"ul", "ol"})
        return (f"{'  ' * max(0, list_depth - 1)}{marker} {clean_inline(inline)}\n"
                f"{nested}")
    return body


def render_children(node: Node, base_url: str, asset_names: dict[str, str],
                    list_depth: int = 0) -> str:
    result = []
    for child in node.children:
        depth = list_depth + 1 if node.tag in {"ul", "ol"} else list_depth
        result.append(render_html_node(child, base_url, asset_names, depth))
    return "".join(result)


def walk_nodes(node: Node, tag: str):
    for child in node.children:
        if isinstance(child, Node):
            if child.tag == tag:
                yield child
            yield from walk_nodes(child, tag)


def node_depth(node: Node) -> int:
    depth = 0
    while node.parent is not None:
        depth += 1
        node = node.parent
    return depth


def content_root(root: Node) -> Node:
    """Choose the article body instead of archiving site navigation as prose.

    Old technical sites often use one large layout table and provide neither a
    ``main`` element nor semantic classes.  A useful content cell is normally
    almost as text-heavy as the body, but deeper and much less link-dense.
    """
    semantic = [node for tag in ("main", "article") for node in walk_nodes(root, tag)
                if len(clean_inline(node_text(node))) >= 200]
    if semantic:
        return max(semantic, key=lambda node: len(clean_inline(node_text(node))))

    candidates = [root]
    for tag in ("td", "div", "section"):
        candidates.extend(walk_nodes(root, tag))
    body_length = max(1, len(clean_inline(node_text(root))))
    eligible = []
    boilerplate = re.compile(
        r"(?:^|[-_ ])(?:nav|menu|sidebar|header|footer|search|copyright)(?:$|[-_ ])",
        re.I,
    )
    for node in candidates:
        text_length = len(clean_inline(node_text(node)))
        if text_length < max(400, body_length * 0.35):
            continue
        identity = " ".join((node.attrs.get("id", ""), node.attrs.get("class", "")))
        if boilerplate.search(identity):
            continue
        link_length = sum(len(clean_inline(node_text(link)))
                          for link in walk_nodes(node, "a"))
        link_density = link_length / max(1, text_length)
        eligible.append((link_density, -node_depth(node), -text_length, node))
    return min(eligible, key=lambda item: item[:3])[-1] if eligible else root


def table_rows(table: Node):
    """Yield rows owned by *table*, excluding rows from nested tables."""
    def visit(node):
        for child in node.children:
            if not isinstance(child, Node):
                continue
            if child.tag == "table":
                continue
            if child.tag == "tr":
                yield child
            else:
                yield from visit(child)
    rows = list(visit(table))
    if rows:
        yield from rows
        return
    # A surprising amount of surviving 1990s HTML begins ``<table><table>``
    # and relies on browser error recovery.  If the outer shell owns no row at
    # all, treat its immediate nested table as the real table rather than
    # dropping the page body.
    for child in table.children:
        if isinstance(child, Node) and child.tag == "table":
            yield from table_rows(child)


def clean_markdown(text: str) -> str:
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    text = re.sub(r"[ \t]+\n", "\n", text)
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text.strip() + "\n"


def html_to_markdown(data: str, base_url: str,
                     asset_names: dict[str, str]) -> tuple[str, str]:
    parser = LooseHTMLTree()
    parser.feed(data)
    title = ""
    titles = list(walk_nodes(parser.root, "title"))
    if titles:
        title = clean_inline(node_text(titles[0]))
    body_nodes = list(walk_nodes(parser.root, "body"))
    root = content_root(body_nodes[0] if body_nodes else parser.root)
    return clean_markdown(render_html_node(root, base_url, asset_names)), title


def mediawiki_asset_refs(source: str) -> list[str]:
    refs = [m.group(1).strip() for m in re.finditer(
        r"\[\[(?:File|Image|Media):\s*([^|\]]+)", source, re.I)]
    for gallery in re.finditer(r"<gallery[^>]*>(.*?)</gallery>", source,
                               re.I | re.S):
        refs.extend(m.group(1).strip() for m in re.finditer(
            r"^\s*(?:File|Image|Media):\s*([^|\r\n]+)",
            gallery.group(1), re.I | re.M))
    return refs


def dokuwiki_asset_refs(source: str) -> list[str]:
    return [m.group(1).strip() for m in re.finditer(
        r"\{\{\s*([:a-zA-Z0-9_./ -]+\.[a-zA-Z0-9]+)", source)]


def html_asset_refs(source: str, base_url: str,
                    asset_base_url: str | None = None) -> list[str]:
    parser = LooseHTMLTree()
    parser.feed(source)
    refs = []
    for node in walk_nodes(parser.root, "img"):
        if node.attrs.get("src"):
            refs.append(urljoin(asset_base_url or base_url, node.attrs["src"]))
    for tag in ("source", "video", "audio"):
        for node in walk_nodes(parser.root, tag):
            if node.attrs.get("src"):
                refs.append(urljoin(asset_base_url or base_url, node.attrs["src"]))
    for node in walk_nodes(parser.root, "a"):
        href = node.attrs.get("href", "")
        if Path(urlparse(href).path).suffix.lower() in ASSET_EXTENSIONS:
            refs.append(urljoin(asset_base_url or base_url, href))
    return refs


def convert_mediawiki(source: str, source_url: str,
                      local_assets: dict[str, str]) -> str:
    def asset(m):
        raw = m.group(2).strip()
        options = (m.group(3) or "").split("|")
        label = next((clean_inline(x) for x in reversed(options)
                      if clean_inline(x) and not re.match(
                          r"^(thumb|frame|frameless|left|right|center|none|\d+px)$",
                          clean_inline(x), re.I)), safe_filename(raw))
        target = local_assets.get(normalize_asset_key(raw), raw)
        return f"[{label}]({target})" if m.group(1).lower() == "media" \
            else f"![{label}]({target})"

    def gallery(m):
        lines = []
        for line in m.group(1).splitlines():
            match = re.match(r"\s*(?:File|Image):\s*([^|]+)(?:\|(.*))?$", line, re.I)
            if match:
                raw = match.group(1).strip()
                label = clean_inline(match.group(2) or raw)
                target = local_assets.get(normalize_asset_key(raw), raw)
                lines.append(f"![{label}]({target})")
        return "\n".join(lines)

    text = source.replace("\r\n", "\n").replace("\r", "\n")
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    text = re.sub(r"<gallery[^>]*>(.*?)</gallery>", gallery, text, flags=re.I | re.S)
    text = re.sub(r"\[\[(File|Image|Media):\s*([^|\]]+)(?:\|([^\]]+))?\]\]",
                  asset, text, flags=re.I)
    text = convert_mediawiki_tables(text, local_assets)
    # Convert wiki list markers before generating Markdown headings: both
    # syntaxes use '#', so reversing this order turns every Markdown heading
    # into an ordered-list item.
    text = re.sub(r"^([*#]+)\s*(.*)$", lambda m:
                  "  " * (len(m.group(1)) - 1) +
                  ("- " if m.group(1)[-1] == "*" else "1. ") + m.group(2),
                  text, flags=re.M)
    text = re.sub(r"^======\s*(.*?)\s*======\s*$", r"###### \1", text, flags=re.M)
    text = re.sub(r"^=====\s*(.*?)\s*=====\s*$", r"##### \1", text, flags=re.M)
    text = re.sub(r"^====\s*(.*?)\s*====\s*$", r"#### \1", text, flags=re.M)
    text = re.sub(r"^===\s*(.*?)\s*===\s*$", r"### \1", text, flags=re.M)
    text = re.sub(r"^==\s*(.*?)\s*==\s*$", r"## \1", text, flags=re.M)
    text = re.sub(r"^=\s*(.*?)\s*=\s*$", r"# \1", text, flags=re.M)
    text = re.sub(r"'''''(.*?)'''''", r"***\1***", text, flags=re.S)
    text = re.sub(r"'''(.*?)'''", r"**\1**", text, flags=re.S)
    text = re.sub(r"''(.*?)''", r"*\1*", text, flags=re.S)
    text = re.sub(r"\[(https?://[^\]\s|]+)(?:\s*\|\s*|\s+)([^\]]+)\]",
                  r"[\2](\1)", text)
    text = re.sub(r"\[(https?://[^\]]+)\]", r"[\1](\1)", text)
    text = re.sub(r"\[\[([^|\]]+)\|([^\]]+)\]\]", lambda m:
                  f"[{m.group(2)}]({urljoin(source_url, m.group(1).replace(' ', '_'))})", text)
    text = re.sub(r"\[\[([^\]]+)\]\]", lambda m:
                  f"[{m.group(1)}]({urljoin(source_url, m.group(1).replace(' ', '_'))})", text)
    text = re.sub(r"<br(?:\s+[^>]*)?\s*/?>", "  \n", text, flags=re.I)
    text = re.sub(r"</?(?:center|div|span|small|big|font|p)(?:\s[^>]*)?>", "", text,
                  flags=re.I)
    text = re.sub(r"\{\{(?:clear|toc|notoc)[^}]*\}\}", "", text, flags=re.I)
    return clean_markdown(text)


def convert_mediawiki_tables(text: str, local_assets: dict[str, str]) -> str:
    def one_table(match):
        rows = []
        current = []
        for raw_line in match.group(1).splitlines():
            line = raw_line.strip()
            if line.startswith("|-"):
                if current:
                    rows.append(current)
                    current = []
            elif line.startswith("!"):
                current.extend(re.split(r"\s*!!\s*", line[1:]))
            elif line.startswith("|"):
                current.extend(re.split(r"\s*\|\|\s*", line[1:]))
            elif current and line:
                current[-1] += " " + line
        if current:
            rows.append(current)
        if not rows:
            return ""
        width = max(map(len, rows))
        output = []
        for index, row in enumerate(rows):
            cleaned = []
            for cell in row + [""] * (width - len(row)):
                cell = re.sub(r"^[^|\[\]]*\|", "", cell).strip()
                cell = cell.replace("|", "\\|")
                cleaned.append(cell)
            output.append("| " + " | ".join(cleaned) + " |")
            if index == 0:
                output.append("| " + " | ".join(["---"] * width) + " |")
        return "\n\n" + "\n".join(output) + "\n\n"
    return re.sub(r"^\{\|[^\n]*\n(.*?)^\|\}\s*$", one_table, text,
                  flags=re.M | re.S)


def convert_dokuwiki(source: str, source_url: str,
                     local_assets: dict[str, str]) -> str:
    footnotes = []

    def media(m):
        raw = m.group(1).strip()
        label = clean_inline(m.group(2) or "")
        target = local_assets.get(normalize_asset_key(raw), raw)
        return f"![{label}]({target})"

    def footnote(m):
        footnotes.append(clean_inline(m.group(1)))
        return f"[^{len(footnotes)}]"

    text = source.replace("\r\n", "\n").replace("\r", "\n")
    text = re.sub(r"^(=+)\s*(.*?)\s*=+\s*$", lambda m:
                  f"{'#' * max(1, min(6, 7 - len(m.group(1))))} {m.group(2)}",
                  text, flags=re.M)
    text = re.sub(r"\{\{\s*([:a-zA-Z0-9_./ -]+\.[a-zA-Z0-9]+)\s*(?:\|([^}]*))?\}\}",
                  media, text)
    text = re.sub(r"\[\[([^|\]]+)(?:\|([^\]]+))?\]\]", lambda m:
                  f"[{clean_inline(m.group(2) or m.group(1))}]({urljoin(source_url, m.group(1))})",
                  text)
    text = re.sub(r"\(\(([^)]+)\)\)", footnote, text)
    text = re.sub(r"//(.*?)//", r"*\1*", text)
    text = re.sub(r"__(.*?)__", r"**\1**", text)
    text = re.sub(r"''(.*?)''", r"`\1`", text)
    text = re.sub(r"<code(?:\s+[^>]*)?>(.*?)</code>",
                  lambda m: f"\n```\n{m.group(1).strip()}\n```\n", text,
                  flags=re.I | re.S)
    text = re.sub(r"^(\s{2,})\*\s+(.*)$", lambda m:
                  " " * max(0, len(m.group(1)) - 2) + "- " + m.group(2), text,
                  flags=re.M)
    text = re.sub(r"^(\s{2,})-\s+(.*)$", lambda m:
                  " " * max(0, len(m.group(1)) - 2) + "1. " + m.group(2), text,
                  flags=re.M)
    if footnotes:
        text = text.rstrip() + "\n\n" + "\n".join(
            f"[^{index}]: {value}" for index, value in enumerate(footnotes, 1))
    return clean_markdown(text)


def gather_files(root: Path) -> list[Path]:
    if not root:
        return []
    return sorted(path for path in root.rglob("*") if path.is_file())


def reserve_output_name(path: Path, used: set[str]) -> str:
    name = safe_filename(path.name)
    if name not in used:
        used.add(name)
        return name
    stem, suffix = Path(name).stem, Path(name).suffix
    candidate = f"{stem}-{sha256_file(path)[:10]}{suffix}"
    if candidate in used:
        raise ValueError(f"duplicate asset output name for {path}")
    used.add(candidate)
    return candidate


def build_bundle(kind: str, source_url: str, source_bytes: bytes,
                 page_bytes: bytes | None, asset_files: list[Path], output: Path,
                 captured_at: str, title_override: str | None = None,
                 allow_missing: bool = False, capture_mode: str = "local-import",
                 asset_origins: dict[str, str] | None = None,
                 render_base_url: str | None = None):
    source = decode_source_bytes(source_bytes, kind)
    page = decode_source_bytes(page_bytes or b"", "html")
    resolver = AssetResolver(asset_files)
    refs = (mediawiki_asset_refs(source) if kind == "mediawiki" else
            dokuwiki_asset_refs(source) if kind == "dokuwiki" else
            html_asset_refs(source, render_base_url or source_url))
    selected = []
    missing = []
    for ref in refs:
        lookup = urlparse(ref).path if kind == "html" else ref
        path = resolver.resolve(lookup)
        if path and path not in selected:
            selected.append(path)
        elif not path:
            missing.append(ref)
    if missing and not allow_missing:
        formatted = "\n  ".join(sorted(set(missing)))
        raise ValueError(f"missing referenced assets:\n  {formatted}")

    output = output.resolve()
    if output.exists():
        raise FileExistsError(f"output already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent))
    try:
        (temporary / "raw").mkdir()
        (temporary / "assets").mkdir()
        raw_name = f"source.{kind}" if kind != "html" else "page.html"
        (temporary / "raw" / raw_name).write_bytes(source_bytes)
        if page_bytes is not None and raw_name != "page.html":
            (temporary / "raw" / "page.html").write_bytes(page_bytes)

        local_assets = {}
        html_assets = {}
        used = set()
        copied_origins = {}
        for path in selected:
            name = reserve_output_name(path, used)
            target = temporary / "assets" / name
            shutil.copyfile(path, target)
            local_assets[normalize_asset_key(path.name)] = f"assets/{name}"
            origin = (asset_origins or {}).get(str(path), "")
            if origin:
                html_assets[origin] = f"assets/{name}"
                copied_origins[f"assets/{name}"] = origin
            for ref in refs:
                if normalize_asset_key(urlparse(ref).path) == normalize_asset_key(path.name):
                    if kind == "html":
                        html_assets[urljoin(source_url, ref)] = f"assets/{name}"

        derived_title = ""
        if kind == "mediawiki":
            readable = convert_mediawiki(source, source_url, local_assets)
        elif kind == "dokuwiki":
            readable = convert_dokuwiki(source, source_url, local_assets)
        else:
            readable, derived_title = html_to_markdown(
                source, render_base_url or source_url, html_assets)
        title = title_override or derived_title or safe_filename(urlparse(source_url).path, "Reference")
        header = (
            f"# {title}\n\n"
            f"> [!NOTE]\n"
            f"> Preserved from <{source_url}> on `{captured_at}`. "
            f"This Markdown is a readable derivative; `raw/` contains the captured evidence.\n\n"
        )
        (temporary / "README.md").write_text(header + readable, encoding="utf-8")

        records = []
        for path in sorted(p for p in temporary.rglob("*") if p.is_file()):
            rel = path.relative_to(temporary).as_posix()
            role = "readable" if rel == "README.md" else (
                "raw" if rel.startswith("raw/") else "asset")
            records.append({
                "path": rel,
                "role": role,
                "size": path.stat().st_size,
                "sha256": sha256_file(path),
                "media_type": mimetypes.guess_type(path.name)[0] or
                              "application/octet-stream",
                "source_url": copied_origins.get(rel, ""),
            })
        manifest = {
            "schema_version": 1,
            "bundle": {
                "title": title,
                "kind": kind,
                "source_url": source_url,
                "captured_at": captured_at,
                "capture_mode": capture_mode,
                "generator": f"reference-bundle.py/{VERSION}",
                "render_base_url": render_base_url or "",
                "missing_assets": sorted(set(missing)),
            },
            "files": records,
        }
        (temporary / "manifest.json").write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        checksum_paths = [p for p in temporary.rglob("*") if p.is_file()
                          and p.name != "MANIFEST.sha256"]
        checksum_text = "".join(
            f"{sha256_file(path)}  {path.relative_to(temporary).as_posix()}\n"
            for path in sorted(checksum_paths))
        (temporary / "MANIFEST.sha256").write_text(checksum_text, encoding="ascii")
        validate_bundle(temporary)
        os.replace(temporary, output)
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise


def local_markdown_links(path: Path):
    text = path.read_text(encoding="utf-8")
    for match in re.finditer(r"!?\[[^\]]*\]\(([^)]+)\)", text):
        target = match.group(1).strip().strip("<>").split("#", 1)[0]
        if not target or re.match(r"^[a-z][a-z0-9+.-]*:", target, re.I):
            continue
        yield unquote(target)


def validate_bundle(root: Path):
    root = root.resolve()
    manifest_path = root / "manifest.json"
    checksum_path = root / "MANIFEST.sha256"
    readme = root / "README.md"
    for required in (manifest_path, checksum_path, readme):
        if not required.is_file():
            raise ValueError(f"missing bundle file: {required.name}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 1:
        raise ValueError("unsupported manifest schema")
    expected = {}
    for record in manifest.get("files", []):
        rel = PurePosixPath(record["path"])
        if rel.is_absolute() or ".." in rel.parts:
            raise ValueError(f"unsafe manifest path: {rel}")
        if rel.as_posix() in expected:
            raise ValueError(f"duplicate manifest path: {rel}")
        expected[rel.as_posix()] = record
    actual = {p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file()}
    allowed_meta = {"manifest.json", "MANIFEST.sha256"}
    if actual - allowed_meta != set(expected):
        raise ValueError(
            f"manifest file set mismatch: missing={sorted(set(expected) - actual)} "
            f"extra={sorted(actual - allowed_meta - set(expected))}")
    for rel, record in expected.items():
        path = root / rel
        if path.stat().st_size != record["size"]:
            raise ValueError(f"size mismatch: {rel}")
        if sha256_file(path) != record["sha256"]:
            raise ValueError(f"SHA-256 mismatch: {rel}")
    checksum_records = {}
    for line in checksum_path.read_text(encoding="ascii").splitlines():
        digest, rel = line.split("  ", 1)
        checksum_records[rel] = digest
    checksum_targets = actual - {"MANIFEST.sha256"}
    if set(checksum_records) != checksum_targets:
        raise ValueError("MANIFEST.sha256 file set mismatch")
    for rel, digest in checksum_records.items():
        if sha256_file(root / rel) != digest:
            raise ValueError(f"MANIFEST.sha256 mismatch: {rel}")
    for target in local_markdown_links(readme):
        rel = PurePosixPath(target)
        if rel.is_absolute() or ".." in rel.parts:
            raise ValueError(f"unsafe README link: {target}")
        if not (root / rel.as_posix()).is_file():
            raise ValueError(f"broken README link: {target}")


def request_url(url: str) -> str:
    """Quote legacy URLs that browsers accept but urllib correctly rejects."""
    parsed = urlparse(url)
    path = quote(unquote(parsed.path), safe="/:@!$&'()*+,;=-._~")
    query = quote(unquote(parsed.query), safe="=&?/:@!$'()*+,;~-._")
    return parsed._replace(path=path, query=query).geturl()


def fetch(url: str, user_agent: str) -> tuple[bytes, str, str]:
    request = Request(request_url(url), headers={"User-Agent": user_agent})
    with urlopen(request, timeout=45) as response:
        return response.read(), response.geturl(), response.headers.get_content_type()


def auto_raw_url(kind: str, page_url: str) -> str:
    parsed = urlparse(page_url)
    if kind == "mediawiki":
        title = unquote(parsed.path.rstrip("/").rsplit("/", 1)[-1])
        base_path = parsed.path
        if "/index.php/" in base_path:
            index = base_path.split("/index.php/", 1)[0] + "/index.php"
        else:
            index = base_path.rstrip("/") + "/index.php"
        return parsed._replace(path=index, query=f"title={quote(title)}&action=raw").geturl()
    page_id = parsed.path.strip("/").replace("/", ":")
    return parsed._replace(path="/doku.php",
                           query=f"id={quote(page_id)}&do=export_raw").geturl()


def media_download_url(kind: str, page_url: str, reference: str) -> str:
    parsed = urlparse(page_url)
    if kind == "mediawiki":
        prefix = parsed.path.split("/index.php", 1)[0]
        return parsed._replace(
            path=f"{prefix}/index.php/Special:FilePath/{quote(reference)}",
            query="", fragment="").geturl()
    media = reference.lstrip(":").replace("/", ":")
    return parsed._replace(path="/lib/exe/fetch.php",
                           query=f"media={quote(media, safe=':')}", fragment="").geturl()


def capture(args):
    page_bytes, final_url, _ = fetch(args.url, args.user_agent)
    raw_url = args.raw_url or (auto_raw_url(args.kind, final_url)
                               if args.kind != "html" else final_url)
    source_bytes = page_bytes
    if args.kind != "html":
        source_bytes, _, _ = fetch(raw_url, args.user_agent)
    source = decode_source_bytes(source_bytes, args.kind)
    refs = (mediawiki_asset_refs(source) if args.kind == "mediawiki" else
            dokuwiki_asset_refs(source) if args.kind == "dokuwiki" else
            html_asset_refs(source, final_url, args.asset_base_url))
    with tempfile.TemporaryDirectory(prefix="reference-capture.") as directory:
        assets = Path(directory)
        origins = {}
        used = set()
        for ref in dict.fromkeys(refs):
            url = (media_download_url(args.kind, final_url, ref)
                   if args.kind != "html" else ref)
            try:
                data, asset_url, _ = fetch(url, args.user_agent)
            except Exception as error:
                if args.allow_missing_assets:
                    print(f"warning: failed to fetch {url}: {error}", file=sys.stderr)
                    continue
                raise RuntimeError(f"failed to fetch asset {url}: {error}") from error
            name = safe_filename(urlparse(ref).path, "asset")
            if name in used:
                stem, suffix = Path(name).stem, Path(name).suffix
                name = f"{stem}-{sha256_bytes(data)[:10]}{suffix}"
            used.add(name)
            path = assets / name
            path.write_bytes(data)
            origins[str(path)] = asset_url
        build_bundle(args.kind, final_url, source_bytes,
                     page_bytes if args.kind != "html" else None,
                     gather_files(assets), args.output, args.captured_at,
                     args.title, args.allow_missing_assets, "network-capture", origins,
                     args.asset_base_url)


def import_local(args):
    source_bytes = args.source.read_bytes()
    page_bytes = args.page_html.read_bytes() if args.page_html else None
    build_bundle(args.kind, args.source_url, source_bytes, page_bytes,
                 gather_files(args.assets) if args.assets else [], args.output,
                 args.captured_at, args.title, args.allow_missing_assets)


def import_artifacts(args):
    output = args.output.resolve()
    if output.exists():
        raise FileExistsError(f"output already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent))
    try:
        raw = temporary / "raw"
        raw.mkdir()
        used = set()
        copied = []
        seen_hashes = set()
        for source in args.file:
            if not source.is_file():
                raise FileNotFoundError(f"artifact does not exist: {source}")
            digest = sha256_file(source)
            if digest in seen_hashes:
                continue
            seen_hashes.add(digest)
            name = reserve_output_name(source, used)
            target = raw / name
            shutil.copyfile(source, target)
            copied.append(target)

        rows = []
        for path in copied:
            rows.append(
                f"| [{path.name}](raw/{quote(path.name)}) | "
                f"{path.stat().st_size:,} | `{sha256_file(path)}` |")
        description = args.description.strip() if args.description else (
            "Preserved documentary artifact. See the source catalog for its "
            "editorial qualification.")
        readme = (
            f"# {args.title}\n\n"
            f"> [!NOTE]\n"
            f"> Imported on `{args.captured_at}` from `{args.source_note}`. "
            f"The files under `raw/` are byte-for-byte evidence.\n\n"
            f"{description}\n\n"
            "| File | Bytes | SHA-256 |\n"
            "|---|---:|---|\n" + "\n".join(rows) + "\n")
        (temporary / "README.md").write_text(readme, encoding="utf-8")

        records = []
        for path in sorted(p for p in temporary.rglob("*") if p.is_file()):
            rel = path.relative_to(temporary).as_posix()
            records.append({
                "path": rel,
                "role": "readable" if rel == "README.md" else "raw",
                "size": path.stat().st_size,
                "sha256": sha256_file(path),
                "media_type": mimetypes.guess_type(path.name)[0] or
                              "application/octet-stream",
                "source_url": args.source_url or "",
            })
        manifest = {
            "schema_version": 1,
            "bundle": {
                "title": args.title,
                "kind": "artifact",
                "source_url": args.source_url or "",
                "captured_at": args.captured_at,
                "capture_mode": "local-artifact-import",
                "source_note": args.source_note,
                "generator": f"reference-bundle.py/{VERSION}",
                "missing_assets": [],
            },
            "files": records,
        }
        (temporary / "manifest.json").write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8")
        checksum_paths = [p for p in temporary.rglob("*") if p.is_file()]
        (temporary / "MANIFEST.sha256").write_text("".join(
            f"{sha256_file(path)}  {path.relative_to(temporary).as_posix()}\n"
            for path in sorted(checksum_paths)), encoding="ascii")
        validate_bundle(temporary)
        os.replace(temporary, output)
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise


def rebuild_bundle(args):
    source_root = args.bundle.resolve()
    validate_bundle(source_root)
    manifest = json.loads((source_root / "manifest.json").read_text(encoding="utf-8"))
    metadata = manifest["bundle"]
    kind = metadata.get("kind")
    if kind not in KINDS:
        raise ValueError(f"cannot rebuild bundle kind: {kind}")
    raw_name = "page.html" if kind == "html" else f"source.{kind}"
    source = source_root / "raw" / raw_name
    page = source_root / "raw" / "page.html" if kind != "html" else None
    assets = gather_files(source_root / "assets")
    origins = {}
    for record in manifest.get("files", []):
        if record.get("role") == "asset" and record.get("source_url"):
            origins[str(source_root / record["path"])] = record["source_url"]
    build_bundle(
        kind, metadata["source_url"], source.read_bytes(),
        page.read_bytes() if page and page.is_file() else None,
        assets, args.output, metadata["captured_at"], metadata.get("title"),
        bool(metadata.get("missing_assets")), metadata.get("capture_mode", "bundle-rebuild"),
        origins, getattr(args, "render_base_url", None) or
        metadata.get("render_base_url") or None,
    )


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--kind", choices=KINDS, required=True)
    common.add_argument("--source-url", help="original public page URL")
    common.add_argument("--output", type=Path, required=True)
    common.add_argument("--captured-at", required=True,
                        help="known capture time/date (ISO-8601)")
    common.add_argument("--title")
    common.add_argument("--allow-missing-assets", action="store_true")

    capture_parser = sub.add_parser("capture", parents=[common])
    capture_parser.add_argument("--url", required=True)
    capture_parser.add_argument("--raw-url")
    capture_parser.add_argument(
        "--asset-base-url",
        help="base URL for broken legacy relative media paths (HTML only)")
    capture_parser.add_argument(
        "--user-agent", default="EncoreReferencePreserver/1.0 (+offline documentation archive)")

    import_parser = sub.add_parser("import", parents=[common])
    import_parser.add_argument("--source", type=Path, required=True)
    import_parser.add_argument("--page-html", type=Path)
    import_parser.add_argument("--assets", type=Path)

    artifact_parser = sub.add_parser("artifact")
    artifact_parser.add_argument("--file", type=Path, action="append", required=True)
    artifact_parser.add_argument("--source-url")
    artifact_parser.add_argument("--source-note", required=True)
    artifact_parser.add_argument("--captured-at", required=True)
    artifact_parser.add_argument("--title", required=True)
    artifact_parser.add_argument("--description")
    artifact_parser.add_argument("--output", type=Path, required=True)

    rebuild_parser = sub.add_parser("rebuild")
    rebuild_parser.add_argument("bundle", type=Path)
    rebuild_parser.add_argument("--output", type=Path, required=True)
    rebuild_parser.add_argument("--render-base-url")

    validate_parser = sub.add_parser("validate")
    validate_parser.add_argument("bundle", type=Path)
    args = parser.parse_args(argv)
    if args.command == "capture":
        args.source_url = args.url
    elif args.command == "import" and not args.source_url:
        parser.error("import requires --source-url")
    return args


def main(argv=None):
    args = parse_args(argv)
    try:
        if args.command == "capture":
            capture(args)
        elif args.command == "import":
            import_local(args)
        elif args.command == "artifact":
            import_artifacts(args)
        elif args.command == "rebuild":
            rebuild_bundle(args)
        else:
            validate_bundle(args.bundle)
        print("reference bundle: OK")
        return 0
    except Exception as error:
        print(f"reference bundle: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
