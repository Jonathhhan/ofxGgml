# ofxGgmlWebScrapingExample

A focused openFrameworks example for website crawling and Markdown extraction with `ofxGgml`.

## What This Example Shows

- Crawling a URL with `ofxGgmlWebCrawler`
- Switching between static HTML crawling and JavaScript-rendered crawling
- Inspecting crawled pages, source URLs, and generated Markdown
- Saving crawl outputs to `bin/data/crawler-output/`

## Requirements

1. `ofxGgml` installed and built
2. No text model is required for the core crawl demo
3. Optional: install Mojo if you want JavaScript-rendered crawling

## Setup

### 1. Install the Addon

```bash
cd ~/openFrameworks/addons
git clone https://github.com/Jonathhhan/ofxGgml.git
cd ofxGgml
./scripts/setup_linux_macos.sh  # or setup_windows.bat
```

### 2. Optional: Install Mojo for JS-heavy sites

- Windows: `scripts/install-mojo.bat`
- PowerShell: `scripts/install-mojo.ps1`
- Without Mojo, the example still works well for static HTML pages

### 3. Generate the Project

1. Open the openFrameworks Project Generator
2. Import `ofxGgmlWebScrapingExample`
3. Make sure `ofxGgml` is in the addons list
4. Click **Generate**
5. Build and run

## Usage

1. Launch the example
2. Type a URL such as `https://example.com`
3. Press `Enter` to crawl
4. Use `Up` / `Down` to browse crawled documents
5. Press `J` to toggle JavaScript rendering for the next crawl

## Keyboard Shortcuts

- `Enter` - crawl the current URL
- `Up` / `Down` - select a crawled document
- `[` / `]` - decrease / increase crawl depth
- `J` - toggle JavaScript rendering
- `C` - clear the current results
- `Q` - quit

## Notes

- Static sites usually work with the built-in native HTML path
- JavaScript-heavy sites may need Mojo
- Crawl logs and generated Markdown previews are shown directly in the app

## Next Steps

- Use `ofxGgmlEasy::crawlAndSummarize(...)` if you want to pair crawling with an LLM summary
- Use `ofxGgmlEasy::findCitations(...)` for crawler-backed source extraction
- Check `docs/features/WORKFLOWS.md` for the broader research workflow APIs
