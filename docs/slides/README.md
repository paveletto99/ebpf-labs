# eBPF: Programming the Linux Kernel: slides and live demo

A 30-minute Linux Day talk (25 minutes including a 5-minute live demo, then 5 minutes of questions)
for a mixed audience with no previous eBPF knowledge. Written with [Marp](https://marp.app/) and Markdown.
The main deliverable is a PDF.

| Path | Purpose |
| --- | --- |
| [slides.md](slides.md) | The presentation (15 slides plus one backup slide). Presenter notes and references are in HTML comments. |
| [assets/](assets) | Local assets: [architecture.svg](assets/architecture.svg) (load time versus run time). No internet is needed to present. |
| [demo/app.py](demo/app.py) | Demo application that looks stuck at startup. |
| [demo/missing-files.bt](demo/missing-files.bt) | bpftrace script that shows the failing `openat` calls. |
| [demo/README.md](demo/README.md) | Exact demo runbook: preflight, steps, reset, troubleshooting, fallback. |
| [docs/speaker-guide.md](docs/speaker-guide.md) | Timings, speaking notes, demo script, fallback, answers to likely questions. |

## Requirements

- **Node.js 18 or newer** (developed with Node.js 24) and npm. This is for building and previewing the slides only.
- **A Chromium-based browser or Firefox on the machine that exports.** Marp CLI uses it to render the PDF
  (Google Chrome, Chromium, Microsoft Edge, or Firefox). If it is installed in a non-standard place, set `CHROME_PATH`:
  `CHROME_PATH=/path/to/chrome npm run build`.
- **For the live demo:** Linux, Python 3, bpftrace (tested with v0.23.2), `sudo`, and syscall tracepoints. See [demo/README.md](demo/README.md).
- The build commands use `mkdir`, `rm`, and `cp` (Linux or macOS shell).

## Install

```bash
cd docs/slides
npm ci
```

`npm ci` installs the exact versions from `package-lock.json`, including `@marp-team/marp-cli` (pinned).

## Preview and build

```bash
npm run preview      # serve locally with Marp's live preview (http://localhost:8080; set PORT=… if busy)
npm run build        # dist/slides.pdf
npm run build:html   # dist/slides.html (copies assets to dist/assets)
npm run build:print-html  # dist/slides-print.html, a plain page for browser Print → PDF (no browser needed to build)
```

### Error: "No suitable browser found"

Marp CLI needs a browser it can control to render the PDF. It looks for Chrome, Chromium, Edge, or Firefox
installed on the same machine (or in the same WSL distribution) where you run `npm run build`.

- **Debian/Ubuntu (including WSL):** `sudo apt install chromium` (on Ubuntu without the snap, install `google-chrome-stable` instead), then rerun `npm run build`.
- **Browser in a non-standard path:** `CHROME_PATH=/path/to/chromium npm run build`.
- **WSL note:** a Windows Chrome or Edge is **not** usable from WSL. Marp cannot control it, and pointing `CHROME_PATH` at `chrome.exe` fails. Install a Linux browser inside WSL, or run the build from Windows.
- **Stopgap without a Marp-controllable browser:** `npm run build:print-html` needs no browser. Open `dist/slides-print.html` in any browser, then *Print → Save as PDF* (paper: custom 1280×720 px or the page size the browser offers, margins none, background graphics on). Check that the code blocks are visible. This is not the supported export path.

Generated output goes to `dist/` (ignored by Git; the directory is created automatically).
`--allow-local-files` is enabled because the deck loads its own diagram from `assets/` and Marp needs it
to embed local images in the PDF. It also lets the deck's Markdown reference any local file, so only build
decks from this trusted repository. `--html` is enabled for the small layout blocks in `slides.md`.

Present from the PDF, or open `dist/slides.html` in a browser. To keep the HTML working, keep `dist/assets/` beside it.

## Rehearse and reset the demo

Full steps and expected output: [demo/README.md](demo/README.md). In short:

```bash
cd docs/slides/demo
sudo -v                                   # authenticate sudo before going on stage
python3 app.py                            # Terminal A: note the PID it prints
sudo bpftrace missing-files.bt <PID>      # Terminal B: shows ENOENT for config.json; Ctrl+C to stop
echo '{"message": "Hello, Linux Day!"}' > config.json   # Terminal A then prints "Ready!"
rm -f -- ./config.json                    # reset: removes only the generated config file
```

## Known limitations

- The tracer covers the `openat` system call only, not every way to open a file.
- Tracer output shown in the docs and on the backup slide is **illustrative**, not a captured run. Capture a real run on the presentation laptop (see [demo/README.md](demo/README.md)) and keep a recording as a fallback.
- bpftrace needs root and syscall tracepoints; restricted containers, kernel lockdown, or kernels without `CONFIG_FTRACE_SYSCALLS` can prevent the demo.
- Slide counts and timings are in the [speaker guide](docs/speaker-guide.md); rehearse with a timer.
- Replace `[Speaker name]` on slide 1 before presenting.

## Validation status

Checked on a Debian-based WSL2 host (kernel 6.6.87.2-microsoft-standard-WSL2, Node.js 24, Python 3.13, bpftrace v0.23.2):

- Verified: `app.py` prints `Starting…` and its PID, retries once per second while `config.json` is missing, prints `Ready!` and the configuration after the file is created, and exits with status 0.
- Verified: the Marp HTML build; all 16 slides were rendered to PDF with Chrome and inspected for clipping and legibility.
- **Not verified:** the bpftrace tracer was not run. Running it needs root, and no sudo password was available in the session. The tracepoint field names (`filename`, `ret`) come from the kernel source and bpftrace documentation, not from a run on this host. Run the preflight in [demo/README.md](demo/README.md) on the presentation laptop.
- **Not verified:** `npm run build` (Marp's own PDF export) because this host has no Linux browser Marp can drive. The Windows Chrome that is installed cannot be controlled from WSL by Marp. Run `npm run build` on the presentation laptop.
