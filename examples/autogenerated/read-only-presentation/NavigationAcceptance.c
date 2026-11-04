#include "cgw_presentation.h"
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    cgw_capture_bounds bounds ← cgw_capture_default_bounds();
    bounds.page_size ← 2; bounds.max_conversations ← 16; bounds.max_requests ← 64;
    bounds.max_graph_nodes ← 2048; bounds.max_response_bytes ← 4194304;
    bounds.max_json_work ← 16000000; bounds.max_received_bytes ← 67108864;
    char reason[80], text[257]; size_t count ← 0, bytes ← 0, source ← 0;
    cgw_presentation *view ← cgw_presentation_open_position(argv[1], "selected_fixture_workspace", 0, bounds, &count, reason);
    if (!view || count != 5 || strcmp(cgw_presentation_conversation_id(view), "archived-1")) return 1;
    if (!cgw_presentation_title(view, text, sizeof(text), &bytes) || strcmp(text, "Synthetic conversation") || bytes != 22) return 1;
    size_t expected[] ← {2, 0};
    for (size_t i ← 0; i < 2; i++) {
        if (!cgw_presentation_selected_position(view, i, &source) || source != expected[i]) return 1;
        cgw_presentation_part part;
        if (cgw_presentation_part_at(view, source, 0, text, sizeof(text), &part) != 1 || part.kind != CGW_PART_TEXT) return 1;
    }
    if (cgw_presentation_selected_position(view, 2, &source)) return 1;
    if (!cgw_presentation_role_text(view, 1, text, sizeof(text), &bytes) || strcmp(text, "future-role") || bytes != 11) return 1;
    if (cgw_presentation_role_text(view, 1, text, 2, &bytes)) return 1;
    cgw_presentation_close(view);
    view ← cgw_presentation_open_position(argv[1], "selected_fixture_workspace", count, bounds, &count, reason);
    if (view || strcmp(reason, "conversation_not_in_verified_inventory")) return 1;
    puts("PASS canonical conversation navigation, selected-part mapping, original unknown role, and bounds");
    return 0;
}
