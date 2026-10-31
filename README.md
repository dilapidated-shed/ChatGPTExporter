# ChatGPTExporter

ChatGPTExporter is a local, resumable browser extension for archiving the ChatGPT web history available to your signed-in account. It inventories main, archived, project, shared, and explicitly selected workspace scopes before downloading conversation graphs, account artifacts, and referenced files.

Authentication stays inside the normal `chatgpt.com` page. The extension never asks for a token or cookie, has no backend or telemetry, and writes only to a directory you choose.

This is an independent community project and is not affiliated with or endorsed by OpenAI. ChatGPT's private web endpoints can change; retain raw evidence and review the compatibility notes before relying on a new release.

## What it preserves

- Every conversation found by normally terminating main, archived, project, and shared inventory chains.
- Separate histories for every explicitly selected accessible workspace.
- Nodes and branches returned by legacy graph responses, or the returned current branch from plural paginated responses. Plural termination does not prove all sibling versions were returned.
- Citations, browsing/tool/code records, Canvas content, completed deep research, unknown future content blocks, and raw provider extensions.
- Uploaded files, generated images, audio, video, inline binaries, research files, and project-level files when ChatGPT permits retrieval.
- Memories, custom instructions, settings, beta-feature settings, and sanitized workspace/session metadata as auxiliary account artifacts.
- Previous local conversations that disappear from a later remote inventory, marked absent rather than deleted.

## Install from source

Requirements are Node.js 20+ and a Chromium browser that supports Manifest V3 and the File System Access API.

```sh
git clone https://github.com/dilapidated-shed/ChatGPTExporter.git
cd ChatGPTExporter
npm ci
npm run check
npm run test:e2e
npm run build
```

Open `chrome://extensions`, enable Developer mode, choose **Load unpacked**, and select `dist/extension`. Click the extension icon to open its dashboard.

## Import existing ChatGPT exports without a ChatGPT session

This is a separate, offline **file import** mode. It can read a ChatGPT
`conversations.json` file or a ZIP containing `conversations.json` (including
numbered `conversations_0001.json` members). The source may be a local
file, a **stored file on Google Drive**, or a file in a **GitHub repository**.

It does not access ChatGPT's private web APIs or require a ChatGPT browser tab.
**The chosen source must already contain the conversations.** Memory Cane's
project/context notes are not themselves a full ChatGPT conversation export.

After `npm ci`, using a checked-out version containing the offline importer:

```sh
npm run import:offline -- --file /private/chatgpt-export.zip --output /private/chatgpt-import
npm run import:offline -- --drive-id DRIVE_FILE_ID --output /private/chatgpt-import-drive
npm run import:offline -- --github-repo OWNER/PRIVATE_REPO --github-path exports/conversations.json --github-ref main --output /private/chatgpt-import-github
```

The Drive route calls your **locally authorized**
`google-drive-files download FILE_ID` from
[`isomorphisms/cloud-storage-api`](https://github.com/isomorphisms/cloud-storage-api).
The GitHub route calls your locally authorized `gh api` for the named
repository file. Both are read-only source operations; credentials remain
in their respective CLIs, and this importer does not accept or log tokens.

Output goes **only** to a new, explicitly named local directory outside the
source repository, with original source bytes, per-conversation raw
revisions, deterministic JSON/Markdown, and
`reports/offline-import.json`. An existing output directory is refused
rather than overwritten. Files are staged privately and only published after
the entire supplied input has been validated.

The receipt explicitly records `providerSnapshot: "unknown"`,
`scope: "only_records_in_supplied_file"`, `chatgptSessionUsed: false`,
and `assetCoverage: "not_imported"`. This mode does not create a
**live-export completion marker**. It preserves the original ZIP, but this
initial importer does **not** extract or fetch ZIP attachment binaries.

Input files, remote CLI output and ZIP/ZIP64 members now stream to private
disk. There is no fixed 256 MiB input ceiling. `--max-bytes` optionally sets a
total/unpacked budget; `--max-record-bytes` bounds one conversation, with a
64 MiB default. A 260 MiB source is tested under a 96 MiB JavaScript heap.
A Drive/GitHub file containing ordinary notes rather than the
ChatGPT conversation graph schema will be rejected, not mislabeled as history.

`npm run verify:offline -- /private/chatgpt-import` independently reopens the
source, re-extracts ZIP members with CRC/identity checks, reconstructs every
expected graph and verifies raw, normalized, Markdown and index hashes. It
rejects omissions even when completion counts are forged. Historical offline
v1 receipts remain supported. Verification does not access ChatGPT or Drive.

The offline package contains a prebuilt canonical normalizer and needs Node 22
or newer at runtime, without npm packages. Run its `scripts/import-offline.mjs`
and `scripts/verify-offline.mjs` with Node. Source builds and deterministic
extension/offline ZIPs are produced by `npm run package`.

## Native Android candidate

The direct-DEX/C11 application now connects the WebView to a native capture
worker, workspace selection, pause/resume/cancel, read-only disk validation
and Storage Access Framework folder copy. It has no first-party Java/Kotlin,
Gradle application build, TypeScript runtime, SQLite or native token store.

This candidate retains **bounded main/archived graphs**, canonical C JSON,
readable Markdown, provenance receipts and an independently rebuilt index.
Full native archive-layout parity, projects/shared/assets, installed SAF qualification,
live-account and physical TAB_P10 acceptance remain incomplete. The established
development signer is distinct from a production release identity. See
[Android integration and evidence boundaries](android/CAPTURE_INTEGRATION.md).

## Export your history

1. Open a normal signed-in `https://chatgpt.com/` tab.
2. In the extension dashboard, find the tab and explicitly select the workspaces to archive.
3. Run preflight, then choose a parent directory. Each workspace receives an isolated `ChatGPTExport-<fingerprint>` directory.
4. Select inventory scopes, build the inventory, review its aggregate counts and termination evidence, and confirm it.
5. Start or resume capture. You can pause before the next request, resume, cancel safely, or rerun to retry incomplete records.
6. Review the reported terminal state and findings. The repaired audit independently validates retained raw evidence, but a local `complete` result covers only captured provider-returned conversations or current branches—not the entire provider account or all sibling versions. `conversations complete / assets partial` identifies explicit file exceptions; `incomplete` means the archive is not accepted.
7. Read `reports/validation.md` inside each workspace archive. Use **Revalidate only** to prove local files without contacting ChatGPT.

Do not run multiple exporters against the same account simultaneously. The default 250 ms delay, concurrency 1, and batch size 10 are deliberately conservative.

## Archive and import contract

Raw listing/detail/batch revisions are append-preserving under `source/`. Normalized JSON, Markdown, indexes, and reports are derived and rebuildable. Completion markers are written last and contain hashes of every required conversation artifact.

The browser archive's advertised Agent Session Archive interface is shown below.
Consumer compatibility remains **unqualified** in this integration: the maintained
adapter source/runtime could not be located for an actual run. Native disk layout
also requires consumer integration; it does not yet use the browser directory layout.

```sh
asm web-import /private/ChatGPTExport-WORKSPACE-FINGERPRINT \
  --provider chatgpt-web --account-label personal --dry-run --json
```

See [archive contract](docs/ARCHIVE_CONTRACT.md), [Architecture](docs/ARCHITECTURE.md), [web contract](docs/WEB_CONTRACT.md), [privacy model](docs/PRIVACY.md), and [troubleshooting](docs/TROUBLESHOOTING.md) for the operational details.

## Development

```sh
npm test
npm run typecheck
npm run privacy:check
npm run test:e2e
npm run package
```

The project has no runtime dependencies. Playwright, Vitest, TypeScript, and esbuild are development-only. Contributions must use synthetic fixtures and pass the staged privacy scanner; see [CONTRIBUTING.md](CONTRIBUTING.md).

## License and provenance

ChatGPTExporter is MIT licensed. Narrow storage, dashboard, and build primitives were adapted from GrokExporter, and current endpoint/content-shape research was informed by pinned MIT upstream projects. Exact revisions and accepted/rejected ideas are recorded in [UPSTREAM_RESEARCH.md](docs/UPSTREAM_RESEARCH.md).

## Private web API research reference

[The pinned ChatGPT web API reference](docs/chatgpt-web-api/README.md) records source snapshots, observed endpoint contracts, cohort-dependent behavior, and provenance. It is historical research material, **not** the maintained exporter implementation; the runnable extension remains under `src/`.
