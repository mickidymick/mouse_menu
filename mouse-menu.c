#include <yed/plugin.h>

static char *mouse_buttons[] = {
    "LEFT",
    "MIDDLE",
    "RIGHT",
    "WHEEL UP",
    "WHEEL DOWN",
};

static char *mouse_actions[] = {
    "PRESS",
    "RELEASE",
    "DRAG",
};
typedef struct {
   yed_frame *frame;
   array_t    strings;
   array_t    dds;
   int        start_len;
   int        is_up;
   int        row;
   int        selection;
   int        size;
   int        cursor_row;
   int        cursor_col;
   int        width;
   int        top_row;
   int        bottom_row;
   int        row_left;
} mouse_popup_t;

static int               if_dragging;
static yed_frame        *drag_frame;
static mouse_popup_t     popup;
static array_t           popup_items;
static int               popup_is_up;
static yed_event_handler h_key;
static yed_event_handler m_key;
static yed_event_handler p_update;
static int               on_word;
static int               on_nothing_row;
static int               on_nothing_col;

/* Internal Functions */
static void       mouse(yed_event* event);
static void       mouse_unload(yed_plugin *self);
static yed_frame *find_frame(yed_event* event);
static int        yed_cell_is_in_frame_mouse(int row, int col, yed_frame *frame);
static void       draw_popup(void);
static void       start_popup(yed_frame *frame, int start_len, array_t strings);
static void       kill_popup(void);
static void       run_command(yed_frame *frame);
static int        on_words(yed_event *event, yed_frame *frame);

/* Event Handlers */
static void right_click(yed_event* event);
static void key_handler(yed_event *event);
static void mouse_handler(yed_event *event);
static void popup_update(yed_event *event);

int yed_plugin_boot(yed_plugin *self) {
    yed_plugin_request_mouse_reporting(self);
    yed_event_handler mouse_eh;

    YED_PLUG_VERSION_CHECK();

    if_dragging = 0;
    popup_items = array_make(char *);

    mouse_eh.kind = EVENT_KEY_PRESSED;
    mouse_eh.fn   = mouse;
    yed_plugin_add_event_handler(self, mouse_eh);

    h_key.kind = EVENT_KEY_PRESSED;
    h_key.fn   = key_handler;
    yed_plugin_add_event_handler(self, h_key);

    m_key.kind = EVENT_PLUGIN_MESSAGE;
    m_key.fn   = mouse_handler;
    yed_plugin_add_event_handler(self, m_key);

    p_update.kind = EVENT_FRAME_PRE_UPDATE;
    p_update.fn   = popup_update;
    yed_plugin_add_event_handler(self, p_update);

    if (yed_get_var("mouse-menu-on-word") == NULL) {
        yed_set_var("mouse-menu-on-word", "Paste paste-yank-buffer");
    }

    if (yed_get_var("mouse-menu-on-selection") == NULL) {
        yed_set_var("mouse-menu-on-selection", "Copy yank-selection");
    }

    if (yed_get_var("mouse-menu-on-nothing") == NULL) {
        yed_set_var("mouse-menu-on-nothing", "'Frame New' frame-new 'Frame Delete' frame-delete 'Frame Next' frame-next");
    }

    yed_plugin_set_unload_fn(self, mouse_unload);

    return 0;
}

int yed_cell_is_in_frame_mouse(int row, int col, yed_frame *frame) {
    return    (row >= frame->top  && row <= frame->top  + frame->height - 1)
           && (col >= frame->left && col <= frame->left + frame->width  - 1);
}

//see if cursor is inside of active frame
//in reverse order find first one that the cursor is inside
yed_frame *find_frame(yed_event* event) {
    yed_frame **frame_it;

    if (yed_cell_is_in_frame_mouse(MOUSE_ROW(event->key), MOUSE_COL(event->key), ys->active_frame)) {
        return ys->active_frame;
    }

    array_rtraverse(ys->frames, frame_it) {
        if (yed_cell_is_in_frame_mouse(MOUSE_ROW(event->key), MOUSE_COL(event->key), (*frame_it))) {
            return (*frame_it);
        }
    }

    return NULL;
}

static void mouse_handler(yed_event *event) {
    yed_line  *line;
    yed_frame *frame;
    int        word_len;
    int        word_start;
    int        key;

    frame = ys->active_frame;
    if(frame == NULL) {
        return;
    }

    if(!strcmp(event->plugin_message.message_id, "mouse-left-click")) {
        LOG_FN_ENTER();
        yed_log("    \n");
        yed_log("row:%d <= bot_row:%d\n", MOUSE_ROW(event->key), popup.bottom_row);
        yed_log("row:%d >= top_row:%d\n", MOUSE_ROW(event->key), popup.top_row);
        yed_log("col:%d >= row_lef:%d\n", MOUSE_COL(event->key), popup.row_left);
        yed_log("col:%d <= row_rig:%d\n", MOUSE_COL(event->key), popup.row_left + popup.width + 1);
        LOG_EXIT();
        if (popup.is_up) {
            if ((MOUSE_ROW(event->key) <= popup.bottom_row) &&
                (MOUSE_ROW(event->key) >= popup.top_row + 1) &&
                (MOUSE_COL(event->key) >= popup.row_left - 1) &&
                (MOUSE_COL(event->key) <= (popup.row_left + popup.width))) {
                popup.selection = (MOUSE_ROW(event->key) - (popup.top_row + 1));
                popup.frame = frame;
                draw_popup();
                key = ENTER;
                yed_feed_keys(1, &key);
                event->cancel = 1;
                return;
            } else {
                kill_popup();
                return;
            }
        }
    }else if(!strcmp(event->plugin_message.message_id, "mouse-left-drag")) {
        if (popup.is_up) {
            event->cancel = 1;
            return;
        }
    }else if(!strcmp(event->plugin_message.message_id, "mouse-scroll-up")) {
        if (popup.is_up) {
            event->cancel = 1;
            return;
        }
    }else if(!strcmp(event->plugin_message.message_id, "mouse-scroll-down")) {
        if (popup.is_up) {
            event->cancel = 1;
            return;
        }
    }else {
        return;
    }
}

static void popup_update(yed_event *event) {
    yed_direct_draw_t **dit;
    if(popup.is_up) {
        array_traverse(popup.dds, dit) {
            (*dit)->dirty = 1;
        }
    }
}

static void key_handler(yed_event *event) {
    yed_line  *line;
    yed_frame *frame;
    int        word_len;
    int        word_start;

    frame = ys->active_frame;

    if (frame == NULL) { return; }

    if (ys->interactive_command != NULL) { return; }

    if(!popup.is_up) {return;}

    if (event->key == ESC) {
        kill_popup();
        popup.is_up = 0;
    }else if(event->key == ENTER) {
        kill_popup();
        popup.is_up = 0;
        event->cancel = 1;
        if(popup.selection >= 0) {
            run_command(frame);
        }
    }else if(event->key == ARROW_UP || event->key == SHIFT_TAB) {
        event->cancel = 1;
        if(popup.selection > 0) {
            popup.selection -= 1;
        }else{
            popup.selection = popup.size-1;
        }
        draw_popup();
    }else if(event->key == ARROW_DOWN || event->key == TAB) {
        event->cancel = 1;
        if(popup.selection < popup.size-1) {
            popup.selection += 1;
        }else {
            popup.selection = 0;
        }
        draw_popup();
    }
}

static void do_popup() {
    popup.size = array_len(popup_items);

    if (ys->active_frame->cur_y + popup.size >= ys->active_frame->top + ys->active_frame->height) {
        popup.row = ys->active_frame->cur_y - popup.size - 1;
    } else {
        popup.row = ys->active_frame->cur_y;
    }
    popup.cursor_col = ys->active_frame->cursor_col;

    start_popup(ys->active_frame, array_len(popup_items), popup_items);
    popup.is_up = 1;
}

static void right_click(yed_event* event) {
    yed_frame *frame;
    yed_event  mouse_left_click_event;
    char *item;
    array_t commands;

    memset(&mouse_left_click_event, 0, sizeof(mouse_left_click_event));
    mouse_left_click_event.kind                      = EVENT_PLUGIN_MESSAGE;
    mouse_left_click_event.plugin_message.message_id = "mouse-right-click";
    mouse_left_click_event.plugin_message.plugin_id  = "mouse";
    mouse_left_click_event.key                       = event->key;
    yed_trigger_event(&mouse_left_click_event);

    if(mouse_left_click_event.cancel) {
        return;
    }
    frame = find_frame(event);
    if (frame == NULL) {
        if_dragging = 0;
        YEXE("select-off");
        return;
    }

    while(array_len(popup_items) > 0) {
        array_pop(popup_items);
    }

    if(frame->buffer == NULL) {
        goto no_buff;
    }

/*     create the menu for no selection */
    if (!frame->buffer->has_selection) {
        if(on_words(event, frame)) {
            on_word = 1;
            commands = sh_split(yed_get_var("mouse-menu-on-word"));
            if((array_len(commands) % 2) == 0) {
                for(int i=0; i<array_len(commands); i++) {
                    if((i % 2) == 0) {
                        item = strdup(*(char **)array_item(commands, i)); array_push(popup_items, item);
                    }
                }
            }else{
                yed_cerr("mouse-menu-on-word too few arguments");
            }
            yed_activate_frame(frame);
            yed_set_cursor_within_frame(frame, MOUSE_ROW(event->key) - frame->top + frame->buffer_y_offset + 1,
                                                MOUSE_COL(event->key) - frame->left + frame->buffer_x_offset - frame->gutter_width + 1);
        }else{
no_buff:;
            on_word = 0;
            commands = sh_split(yed_get_var("mouse-menu-on-nothing"));
            if((array_len(commands) % 2) == 0) {
                for(int i=0; i<array_len(commands); i++) {
                    if((i % 2) == 0) {
                        item = strdup(*(char **)array_item(commands, i)); array_push(popup_items, item);
                    }
                }
            }else{
                yed_cerr("mouse-menu-on-nothing too few arguments");
            }
            yed_activate_frame(frame);

            on_nothing_row = (MOUSE_ROW(event->key) - frame->top + frame->buffer_y_offset + 1);
            on_nothing_col = (MOUSE_COL(event->key) - frame->left + frame->buffer_x_offset - frame->gutter_width + 1);
        }
/*     create the menu for selection */
    }else{
        on_word = 0;
        commands = sh_split(yed_get_var("mouse-menu-on-selection"));
        if((array_len(commands) % 2) == 0) {
            for(int i=0; i<array_len(commands); i++) {
                if((i % 2) == 0) {
                    item = strdup(*(char **)array_item(commands, i)); array_push(popup_items, item);
                }
            }
        }else{
            yed_cerr("mouse-menu-on-selection too few arguments");
        }
        yed_activate_frame(frame);
    }
    free_string_array(commands);

    do_popup();
    event->cancel = 1;
}

static void mouse(yed_event* event) {
    if(ys->active_frame == NULL) {
        return;
    }

    LOG_FN_ENTER();
    if (IS_MOUSE(event->key)) {
/*         yed_log("MOUSE: %s %s %d %d\n", */
/*                 mouse_buttons[MOUSE_BUTTON(event->key)], */
/*                 mouse_actions[MOUSE_KIND(event->key)], */
/*                 MOUSE_ROW(event->key), */
/*                 MOUSE_COL(event->key)); */

        switch (MOUSE_BUTTON(event->key)) {
            case MOUSE_BUTTON_RIGHT:
                if (MOUSE_KIND(event->key) == MOUSE_PRESS) {
                    right_click(event);
                }
                break;
        }
    }
    LOG_EXIT();
}

static void mouse_unload(yed_plugin *self) {
}

static void kill_popup(void) {
    yed_direct_draw_t **dd;

    if (!popup.is_up) { return; }

    free_string_array(popup.strings);

    array_traverse(popup.dds, dd) {
        yed_kill_direct_draw(*dd);
    }

    array_free(popup.dds);

    popup.frame = NULL;

    popup.is_up = 0;
}

static void draw_popup(void) {
    yed_direct_draw_t **dd_it;
    yed_attrs           active;
    yed_attrs           assoc;
    yed_attrs           merged;
    yed_attrs           merged_inv;
    char              **it;
    int                 max_width;
    int                 has_left_space;
    int                 i;
    char                buff[512];
    yed_direct_draw_t  *dd;
    int                 first;

    array_traverse(popup.dds, dd_it) {
        yed_kill_direct_draw(*dd_it);
    }
    array_free(popup.dds);

    popup.dds = array_make(yed_direct_draw_t*);

    active = yed_active_style_get_active();
    assoc  = yed_active_style_get_associate();
    merged = active;
    yed_combine_attrs(&merged, &assoc);
    merged_inv = merged;
    merged_inv.flags ^= ATTR_INVERSE;

    max_width = 0;
    array_traverse(popup.strings, it) {
        max_width = MAX(max_width, strlen(*it));
    }
    popup.width = max_width;

    i              = 1;
    has_left_space = popup.frame->cur_x > popup.frame->left;
    popup.top_row = popup.row;
    popup.bottom_row = 0;
    popup.row_left = 0;
    first = 0;

    array_traverse(popup.strings, it) {
        snprintf(buff, sizeof(buff), "%s%*s ", has_left_space ? " " : "", -max_width, *it);
        LOG_FN_ENTER();
/*         on words and with selection */
        if(on_word || ((popup.frame->buffer != NULL) && popup.frame->buffer->has_selection)) {
            dd = yed_direct_draw(popup.row + i,
                                 popup.frame->left + popup.cursor_col + popup.frame->gutter_width - 1 - popup.frame->buffer_x_offset,
                                 i == popup.selection + 1 ? merged_inv : merged,
                                 buff);
/*         on background */
        }else{
            dd = yed_direct_draw(popup.frame->top + on_nothing_row + i - popup.frame->buffer_y_offset - 1,
                                 popup.frame->left + on_nothing_col + popup.frame->gutter_width - 1 - popup.frame->buffer_x_offset,
                                 i == popup.selection + 1 ? merged_inv : merged,
                                 buff);
        }
        array_push(popup.dds, dd);
        i += 1;
        LOG_EXIT();
    }

    if(on_word || ((popup.frame->buffer != NULL) && popup.frame->buffer->has_selection)) {
        popup.bottom_row = popup.row + i - 1;
        popup.row_left   = popup.frame->left + popup.cursor_col + popup.frame->gutter_width - 1 - popup.frame->buffer_x_offset;
    }else{
        popup.top_row    = popup.frame->top + on_nothing_row - popup.frame->buffer_y_offset - 1;
        popup.bottom_row = popup.frame->top + on_nothing_row + i - 1 - popup.frame->buffer_y_offset;
        popup.row_left   = popup.frame->left + on_nothing_col + popup.frame->gutter_width - popup.frame->buffer_x_offset;
        LOG_FN_ENTER();
        yed_log("on_nothing:%d", on_nothing_col);
/*         yed_log("top:%d\n", popup.top_row); */
/*         yed_log("bottom:%d\n", popup.bottom_row); */
/*         yed_log("left:%d\n", popup.row_left); */
/*         yed_log("width:%d\n", popup.width); */
        yed_log("gutter:%d", popup.frame->gutter_width);
        yed_log("offset:%d", popup.frame->buffer_x_offset);
        LOG_EXIT();
    }
}

static int on_words(yed_event *event, yed_frame *frame) {
    int row, col;
    yed_line *line;
    int last_cursor_row;

    row = (MOUSE_ROW(event->key) - frame->top + frame->buffer_y_offset + 1);
    col = (MOUSE_COL(event->key) - frame->left + frame->buffer_x_offset - frame->gutter_width + 1);

    last_cursor_row = bucket_array_len(frame->buffer->lines);

    if(last_cursor_row < row) {
        return 0;
    }
    line = yed_buff_get_line(frame->buffer, row);
    if((line->visual_width + 1) >= col) {
        return 1;
    }else{
        return 0;
    }
}

static void run_command(yed_frame *frame) {
    array_t commands, tmp_split;

    if(frame->buffer == NULL) {
        goto no_buffer_1;
    }

    LOG_FN_ENTER();
    if (!frame->buffer->has_selection) {
        if(on_word) {
            commands = sh_split(yed_get_var("mouse-menu-on-word"));
        }else{
no_buffer_1:;
            commands = sh_split(yed_get_var("mouse-menu-on-nothing"));
        }
    }else{
        commands = sh_split(yed_get_var("mouse-menu-on-selection"));
    }
    yed_log("%s", *(char **)array_item(commands, (popup.selection*2)+1));
    LOG_EXIT();
    tmp_split = sh_split(*(char **)array_item(commands, (popup.selection*2)+1));
    yed_execute_command_from_split(tmp_split);
    free_string_array(tmp_split);
    free_string_array(commands);
}

static void start_popup(yed_frame *frame, int start_len, array_t strings) {
    kill_popup();

    popup.frame     = frame;
    popup.strings   = copy_string_array(strings);
    popup.dds       = array_make(yed_direct_draw_t*);
    popup.start_len = start_len;
    popup.selection = -1;

    draw_popup();

    popup.is_up = 1;
}
