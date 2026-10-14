# siraht/ChatGPTExporter issue #5

Source: https://github.com/siraht/ChatGPTExporter/issues/5

Title: Capture fails with HTTP 404 after ChatGPT moved reads to plural paginated endpoint

Opened: 2026-09-16

State: open

## Reported working web-app read

`GET /backend-api/conversations/{conversation_id}?include_has_versions=true&num_turns=10`

## Reported legacy exporter reads returning 404

`POST /backend-api/conversations/batch`

`GET /backend-api/conversation/{conversation_id}`

## Reported pagination contract

The plural endpoint returns a flat `messages[]` payload plus `page_info`.

For long conversations the report says to:

1. Fetch `/backend-api/conversations/{id}?include_has_versions=true&num_turns=<bounded hint>`.
2. While `page_info.has_previous_page === true`, follow `page_info.start_cursor`.
3. Fetch older pages from `/backend-api/conversations/{id}/messages?before=<cursor>&include_has_versions=true&num_turns=<bounded hint>`.
4. Reject repeated or missing cursors.
5. Deduplicate stable message IDs at page boundaries and verify conversation identity.
6. Normalize only what was actually observed; do not invent sibling/version branches.
7. Use the legacy singular endpoint only as a compatibility fallback when the plural endpoint itself returns 404.

The issue explicitly warns that changing only the URL while keeping `num_turns=10` can silently export only the newest portion of a long conversation.
