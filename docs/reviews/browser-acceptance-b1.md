# Browser acceptance for ChatGPTExporter

Provenance: [Flexible Pipes work envelope #100](https://github.com/isomorphisms/flexible-pipes/issues/100).
Baseline: X6 draft `2972b9ef50c1f54efbabbb0d03d018adafc00233`. The X2–X6 source branches remain unchanged.

The separate `.github/workflows/browser-e2e.yml` runs `npm run test:e2e` on a GitHub-hosted Chromium browser. The harness:
- Builds and loads the packaged Manifest V3 extension in an isolated browser profile.
- Resolves synthetic `chatgpt.com` to local HTTPS fixture with throwaway certificate and ephemeral port; uses a fake in-memory page token, never a genuine account credential.
- Checks typed allowlisted transport, denied invalid operations, 403/429/5xx, timeout and invalid response sanitization.
- Exercises actual extension tab, dashboard workspace selection, file-directory grant, inventory, pause/resume, selected conversation capture and revalidation.
- Checks authoritative archive-tree byte hashes and writes no private archive artifacts to GitHub.
- Implements actual offset/limit paging in its fake provider so X4's extra full-page probe receives a legitimate empty page.

Acceptance claims must cite the **exact workflow run and SHA**, and distinguish synthetic browser execution from live ChatGPT account verification.

User connected account context: private `isomorphisms/memory-cane` is a durable context store (not browser auth). `dilapidated-shed/cloud-storage-api` handles authenticated Google Drive files; Drive's `ChatGPT/exports` and `ChatGPT/unpacked` remain empty as inspected. Do not invent a conversation ZIP, infer a ChatGPT session from Drive OAuth, or log/export private conversations. Real live acceptance still needs the user's logged-in browser or source export and approval of the specific import path.

This browser workflow deliberately does not execute with GitHub Actions secrets.
