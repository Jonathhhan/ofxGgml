#!/usr/bin/env python3
import argparse
import collections
import datetime as _dt
import html
import os
import posixpath
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
from html.parser import HTMLParser


USER_AGENT = "ofxGgmlMojoCrawler/1.0"


def slugify(value: str) -> str:
    value = value.strip().lower()
    value = re.sub(r"[^a-z0-9]+", "-", value)
    value = value.strip("-")
    return value or "page"


def normalize_url(url: str) -> str:
    parsed = urllib.parse.urlsplit(url)
    scheme = parsed.scheme.lower() or "https"
    netloc = parsed.netloc.lower()
    path = parsed.path or "/"
    path = posixpath.normpath(path)
    if not path.startswith("/"):
        path = "/" + path
    if parsed.path.endswith("/") and not path.endswith("/"):
        path += "/"
    if path == "/.":
        path = "/"
    return urllib.parse.urlunsplit((scheme, netloc, path, parsed.query, ""))


def same_origin(a: str, b: str) -> bool:
    pa = urllib.parse.urlsplit(a)
    pb = urllib.parse.urlsplit(b)
    return (pa.scheme.lower(), pa.netloc.lower()) == (pb.scheme.lower(), pb.netloc.lower())


def normalize_output_dir(path: str) -> str:
    if re.match(r"^[A-Za-z]:\\", path):
        drive = path[0].lower()
        remainder = path[2:].replace("\\", "/")
        remainder = remainder.lstrip("/")
        return f"/mnt/{drive}/{remainder}"
    return path


class HtmlToMarkdownParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.title = ""
        self.description = ""
        self.canonical_url = ""
        self.base_href = ""
        self._in_title = False
        self._in_pre = False
        self._pre_chunks: list[str] = []
        self._ignore_depth = 0
        self._links: list[str] = []
        self._current_href = ""
        self._chunks: list[str] = []
        self._pending_breaks = 0

    @property
    def links(self) -> list[str]:
        return self._links

    def _append_text(self, text: str) -> None:
        if self._ignore_depth > 0:
            return
        if self._in_pre:
            self._pre_chunks.append(text)
            return
        text = html.unescape(text)
        text = re.sub(r"\s+", " ", text)
        if not text.strip():
            return
        if self._pending_breaks > 0:
            self._chunks.append("\n" * self._pending_breaks)
            self._pending_breaks = 0
        self._chunks.append(text.strip())

    def _break(self, count: int = 1) -> None:
        self._pending_breaks = max(self._pending_breaks, count)

    def handle_endtag(self, tag: str) -> None:
        tag = tag.lower()
        if tag in {"script", "style", "noscript"}:
            if self._ignore_depth > 0:
                self._ignore_depth -= 1
            return
        if tag == "title":
            self._in_title = False
            return
        if tag == "pre":
            code = "".join(self._pre_chunks)
            self._pre_chunks = []
            self._in_pre = False
            if code.strip():
                self._break(2)
                self._chunks.append("```\n" + code.rstrip("\n") + "\n```")
                self._break(2)
            return
        if tag in {"p", "div", "section", "article", "header", "footer", "main"}:
            self._break(2)
        elif tag in {"li", "h1", "h2", "h3", "h4", "h5", "h6"}:
            self._break(1)
        elif tag == "a":
            self._current_href = ""

    def handle_data(self, data: str) -> None:
        if self._in_title:
            self.title += data.strip()
            return
        self._append_text(data)

    def handle_startendtag(self, tag: str, attrs: list) -> None:
        self.handle_starttag(tag, attrs)
        self.handle_endtag(tag)

    def handle_comment(self, data: str) -> None:
        return

    def handle_decl(self, decl: str) -> None:
        return

    def handle_pi(self, data: str) -> None:
        return

    def handle_starttag(self, tag: str, attrs: list) -> None:
        attrs_dict = dict(attrs)
        tag = tag.lower()
        if tag in {"script", "style", "noscript"}:
            self._ignore_depth += 1
            return
        if tag == "title":
            self._in_title = True
            return
        if tag == "base":
            href = attrs_dict.get("href", "").strip()
            if href and not self.base_href:
                self.base_href = href
            return
        if tag == "meta":
            name = attrs_dict.get("name", "").lower().strip()
            prop = attrs_dict.get("property", "").lower().strip()
            content = attrs_dict.get("content", "").strip()
            if not self.description and name == "description" and content:
                self.description = content
            elif not self.description and prop == "og:description" and content:
                self.description = content
            return
        if tag == "link":
            rel = attrs_dict.get("rel", "").lower().strip()
            href = attrs_dict.get("href", "").strip()
            if rel == "canonical" and href and not self.canonical_url:
                self.canonical_url = href
            return
        if tag == "pre":
            self._in_pre = True
            self._pre_chunks = []
            return
        if tag in {"p", "div", "section", "article", "header", "footer", "main"}:
            self._break(2)
        elif tag == "br":
            self._break(1)
        elif tag == "li":
            self._break(1)
            self._chunks.append("- ")
        elif tag in {"h1", "h2", "h3", "h4", "h5", "h6"}:
            self._break(2)
            self._chunks.append("#" * int(tag[1]) + " ")
        elif tag == "a":
            self._current_href = attrs_dict.get("href", "").strip()
            if self._current_href:
                self._links.append(self._current_href)

    def markdown(self) -> str:
        text = "".join(self._chunks)
        text = re.sub(r"\n{3,}", "\n\n", text)
        return text.strip() + ("\n" if text.strip() else "")


def fetch_url(url: str, timeout: int = 20) -> str:
    request = urllib.request.Request(
        url,
        headers={
            "User-Agent": USER_AGENT,
            "Accept": "text/html,application/xhtml+xml"
        }
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        content_type = response.headers.get_content_charset() or "utf-8"
        raw = response.read()
        return raw.decode(content_type, errors="replace")


def write_markdown(
    path: str,
    url: str,
    title: str,
    depth: int,
    markdown: str,
    description: str = "",
    canonical_url: str = "",
) -> None:
    lines = [
        "---\n",
        f"url: {url}\n",
        f"title: {title}\n",
    ]
    if canonical_url and canonical_url != url:
        lines.append(f"canonical_url: {canonical_url}\n")
    if description:
        safe_desc = description.replace("\n", " ")
        lines.append(f"description: {safe_desc}\n")
    lines += [
        f"depth: {depth}\n",
        f"crawled_at: {_dt.datetime.now(_dt.timezone.utc).isoformat()}\n",
        "---\n\n",
    ]
    document = "".join(lines)
    if title:
        document += f"# {title}\n\n"
    document += markdown
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(document)


def main() -> int:
    parser = argparse.ArgumentParser(description="Lightweight website-to-markdown crawler for ofxGgml.")
    parser.add_argument("-d", "--depth", type=int, default=2, dest="depth")
    parser.add_argument("-o", "--output", required=True, dest="output_dir")
    parser.add_argument("--render", action="store_true", dest="render_js")
    parser.add_argument(
        "--max-pages",
        type=int,
        default=0,
        dest="max_pages",
        help="Maximum number of pages to crawl (0 = unlimited).",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=20,
        dest="timeout",
        help="Per-request timeout in seconds (default: 20).",
    )
    parser.add_argument("start_url")
    args, unknown = parser.parse_known_args()

    if unknown:
        print("[Warning] Ignoring unsupported crawler arguments:", " ".join(unknown), file=sys.stderr)

    args.output_dir = normalize_output_dir(args.output_dir)
    os.makedirs(args.output_dir, exist_ok=True)
    start_url = normalize_url(args.start_url)
    queue = collections.deque([(start_url, 0)])
    visited: set[str] = set()
    queued: set[str] = {start_url}
    written = 0
    max_pages = max(0, args.max_pages)

    while queue:
        if max_pages > 0 and written >= max_pages:
            break

        url, depth = queue.popleft()
        if url in visited or depth > max(0, args.depth):
            continue
        visited.add(url)

        try:
            html_text = fetch_url(url, timeout=args.timeout)
        except (urllib.error.URLError, TimeoutError, ValueError) as exc:
            print(f"[Warning] Failed to fetch {url}: {exc}", file=sys.stderr)
            continue

        parser_obj = HtmlToMarkdownParser()
        parser_obj.feed(html_text)
        title = parser_obj.title.strip() or urllib.parse.urlsplit(url).path.strip("/") or "Untitled"
        markdown = parser_obj.markdown()
        if not markdown.strip():
            markdown = "_No textual content extracted._\n"

        # Use canonical URL for the recorded source if available
        canonical = parser_obj.canonical_url.strip()
        if canonical:
            try:
                canonical = normalize_url(urllib.parse.urljoin(url, canonical))
            except (ValueError, urllib.error.URLError) as exc:
                print(f"[Warning] Could not normalize canonical URL {canonical!r}: {exc}", file=sys.stderr)
                canonical = ""

        file_name = f"{written:03d}-{slugify(title)}.md"
        file_path = os.path.join(args.output_dir, file_name)
        write_markdown(
            file_path,
            url,
            title,
            depth,
            markdown,
            description=parser_obj.description,
            canonical_url=canonical,
        )
        written += 1

        if depth >= args.depth:
            continue

        # Resolve the base URL for relative links: prefer <base href> if present
        base_href = parser_obj.base_href.strip()
        link_base = urllib.parse.urljoin(url, base_href) if base_href else url

        for href in parser_obj.links:
            next_url = urllib.parse.urljoin(link_base, href)
            if not next_url.startswith(("http://", "https://")):
                continue
            next_url = normalize_url(next_url)
            if same_origin(start_url, next_url) and next_url not in queued:
                queued.add(next_url)
                queue.append((next_url, depth + 1))

    print(f"[Info] Crawled {written} page(s) from {start_url}")
    return 0 if written > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
