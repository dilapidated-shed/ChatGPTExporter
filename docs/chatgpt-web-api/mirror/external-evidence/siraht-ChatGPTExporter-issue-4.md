# siraht/ChatGPTExporter issue #4

Source: https://github.com/siraht/ChatGPTExporter/issues/4

Title: Projects inventory fails with project_page_cursor is invalid

Opened: 2026-09-03

State: open

## Body

Build inventory succeeds when Projects is unchecked.

When Projects is checked, Build inventory fails with:

`project_page_cursor is invalid.`

Environment:
- Windows 11
- Chrome 152
- ChatGPTExporter 0.1.6
- ChatGPT Edu / SSO workspace

Troubleshooting reported by the issue author:
- Non-Projects backup completed successfully: 454 fetched, 0 failed.
- Main History cannot be unchecked, but all optional scopes except Projects were unchecked.
- With Main History + Projects checked, the same error occurs.
- `git pull` reported already up to date.
- `npm ci` completed.
- `npm run check` passed:
  - 19 test files passed
  - 83 tests passed
  - Privacy check passed for 72 tracked/unignored files
  - Build completed
- The unpacked extension was reloaded and the ChatGPT tab refreshed.
- The same failure occurred with a short archive path.

Expected: Projects inventory completes or identifies the failing project/page.

Actual: inventory stops with `project_page_cursor is invalid.`
