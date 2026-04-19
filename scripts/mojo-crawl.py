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
        self._in_title = False
        self._ignore_depth = 0
        self._links = []
        self._current_href = ""
        self._chunks = []
        self._pending_breaks = 0

    @property
    def links(self):
        return self._links

    def _append_text(self, text: str) -> None:
        if self._ignore_depth > 0:
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

    def handle_endtag(self, tag):
        tag = tag.lower()
        if tag in {"script", "style", "noscript"}:
            if self._ignore_depth > 0:
                self._ignore_depth -= 1
            return
        if tag == "title":
            self._in_title = False
            return
        if tag in {"p", "div", "section", "article", "header", "footer", "main"}:
            self._break(2)
        elif tag in {"li", "h1", "h2", "h3", "h4", "h5", "h6"}:
            self._break(1)
        elif tag == "a":
            self._current_href = ""

    def handle_data(self, data):
        if self._in_title:
            self.title += data.strip()
            return
        self._append_text(data)

    def handle_entityref(self, name):
        self._append_text(f"&{name};")

    def handle_charref(self, name):
        self._append_text(f"&#{name};")

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        self.handle_endtag(tag)

    def handle_comment(self, data):
        return

    def handle_decl(self, decl):
        return

    def handle_pi(self, data):
        return

    def handle_starttag(self, tag, attrs):
        attrs_dict = dict(attrs)
        tag = tag.lower()
        if tag in {"script", "style", "noscript"}:
            self._ignore_depth += 1
            return
        if tag == "title":
            self._in_title = True
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


def fetch_url(url: str) -> str:
    request = urllib.request.Request(
        url,
        headers={
            "User-Agent": USER_AGENT,
            "Accept": "text/html,application/xhtml+xml"
        }
    )
    with urllib.request.urlopen(request, timeout=20) as response:
        content_type = response.headers.get_content_charset() or "utf-8"
        raw = response.read()
        return raw.decode(content_type, errors="replace")


def write_markdown(path: str, url: str, title: str, depth: int, markdown: str) -> None:
    frontmatter = (
        "---\n"
        f"url: {url}\n"
        f"title: {title}\n"
        f"depth: {depth}\n"
        f"crawled_at: {_dt.datetime.now(_dt.timezone.utc).isoformat()}\n"
        "---\n\n"
    )
    document = frontmatter
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
    parser.add_argument("start_url")
    args, unknown = parser.parse_known_args()

    if unknown:
        print("[Warning] Ignoring unsupported crawler arguments:", " ".join(unknown), file=sys.stderr)

    args.output_dir = normalize_output_dir(args.output_dir)
    os.makedirs(args.output_dir, exist_ok=True)
    start_url = normalize_url(args.start_url)
    queue = collections.deque([(start_url, 0)])
    visited = set()
    written = 0

    while queue:
        url, depth = queue.popleft()
        if url in visited or depth > max(0, args.depth):
            continue
        visited.add(url)

        try:
            html_text = fetch_url(url)
        except (urllib.error.URLError, TimeoutError, ValueError) as exc:
            print(f"[Warning] Failed to fetch {url}: {exc}", file=sys.stderr)
            continue

        parser_obj = HtmlToMarkdownParser()
        parser_obj.feed(html_text)
        title = parser_obj.title.strip() or urllib.parse.urlsplit(url).path.strip("/") or "Untitled"
        markdown = parser_obj.markdown()
        if not markdown.strip():
            markdown = "_No textual content extracted._\n"

        file_name = f"{written:03d}-{slugify(title)}.md"
        file_path = os.path.join(args.output_dir, file_name)
        write_markdown(file_path, url, title, depth, markdown)
        written += 1

        if depth >= args.depth:
            continue

        for href in parser_obj.links:
            next_url = urllib.parse.urljoin(url, href)
            if not next_url.startswith(("http://", "https://")):
                continue
            next_url = normalize_url(next_url)
            if same_origin(start_url, next_url) and next_url not in visited:
                queue.append((next_url, depth + 1))

    print(f"[Info] Crawled {written} page(s) from {start_url}")
    return 0 if written > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
