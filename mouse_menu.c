#include <yed/plugin.h>
#include "gui.h"

static yed_plugin       *Self;
static yed_event_handler h_key;
static yed_event_handler h_mouse;
static yed_event_handler h_right_click;
static array_t           list_items;
static yed_gui_list_menu list_menu;

static int               if_dragging;
static yed_frame        *drag_frame;
static int               on_word;

/* Internal Functions */
static void _mouse_unload(yed_plugin *self);
static void _run_command(void);
static int  _on_words(yed_event *event);
static void _right_click(yed_event *event);
static void _right_click_handler(yed_event *event);
static void _add_commands(array_t commands, char* string);
static void _free_string_array(array_t *array);

/* Event Handlers */
static void _gui_key_handler(yed_event *event);
static void _gui_mouse_handler(yed_event *event);

int yed_plugin_boot(yed_plugin *self) {
    yed_plugin_request_mouse_reporting(self);

    YED_PLUG_VERSION_CHECK();
    Self = self;

/*     event handler for list menu */
    h_key.kind = EVENT_KEY_PRESSED;
    h_key.fn   = _gui_key_handler;
    yed_plugin_add_event_handler(self, h_key);

/*     event_handler for mouse interaction */
    h_mouse.kind = EVENT_KEY_PRESSED;
    h_mouse.fn   = _gui_mouse_handler;

/*     event_handler for right click */
    h_right_click.kind = EVENT_KEY_PRESSED;
    h_right_click.fn   = _right_click_handler;
    yed_plugin_add_event_handler(self, h_right_click);


    if (yed_get_var("mouse-menu-on-word") == NULL) {
        yed_set_var("mouse-menu-on-word", "Paste paste-yank-buffer");
    }

    if (yed_get_var("mouse-menu-on-selection") == NULL) {
        yed_set_var("mouse-menu-on-selection", "Copy yank-selection Delete delete-back");
    }

    if (yed_get_var("mouse-menu-on-nothing") == NULL) {
        yed_set_var("mouse-menu-on-nothing", "'Frame New' frame-new 'Frame Delete' frame-delete 'Frame Next' frame-next Quit quit");
    }

    yed_plugin_set_unload_fn(self, _mouse_unload);

    list_items = array_make(char *);
    yed_gui_init_list_menu(&list_menu, list_items);
    list_menu.base.is_up = 0;

    return 0;
}

static void _add_commands(array_t commands, char *string) {
    char *item;

    if((array_len(commands) % 2) == 0) {
        for(int i=0; i<array_len(commands); i++) {
            if((i % 2) == 0) {
                item = strdup(*(char **)array_item(commands, i)); array_push(list_items, item);
            }
        }
    }else {
        yed_cerr("%s too few arguments", string);
    }
}

static void _right_click(yed_event* event) {
    yed_frame *frame;
    int has_selection;
    char *item;
    array_t commands;
    int row, col;

    list_menu.base.is_up = 1;

    frame = yed_gui_find_frame(event);
    if (frame != NULL && frame->buffer != NULL) {
        has_selection = frame->buffer->has_selection;
    }else {
        if_dragging = 0;
        YEXE("select-off");
        has_selection = 0;
    }

    _free_string_array(&list_items);

    if (!has_selection) {
        if (_on_words(event)) {
            on_word = 1;
            commands = sh_split(yed_get_var("mouse-menu-on-word"));
            _add_commands(commands, "mouse-menu-on-word");
        }else {
            on_word = 0;
            commands = sh_split(yed_get_var("mouse-menu-on-nothing"));
            _add_commands(commands, "mouse-menu-on-nothing");
        }
    }else {
        on_word = 0;
        commands = sh_split(yed_get_var("mouse-menu-on-selection"));
        _add_commands(commands, "mouse-menu-on-selection");
    }

    row = 10;
    col = 10;

    if (frame != NULL) {
        yed_activate_frame(frame);
        if(frame->buffer != NULL) {
            if (!has_selection) {
                yed_set_cursor_within_frame(frame,
                    MOUSE_ROW(event->key) - frame->top + frame->buffer_y_offset + 1,
                    MOUSE_COL(event->key) - frame->left + frame->buffer_x_offset - frame->gutter_width + 1);
                row = frame->cur_y;
                col = MOUSE_COL(event->key);
            }else{
                row = frame->cur_y;
                col = frame->cur_x;
            }
        }else{
            row = MOUSE_ROW(event->key);
            col = MOUSE_COL(event->key);
        }
    }else{
        row = MOUSE_ROW(event->key);
        col = MOUSE_COL(event->key);
    }
    _free_string_array(&commands);

    if (list_menu.base.is_up) {
        yed_delete_event_handler(h_mouse);
    }
    yed_gui_kill(&list_menu);
    yed_gui_init_list_menu(&list_menu, list_items);

    list_menu.base.top  = row;
    list_menu.base.left = col;

    yed_gui_draw(&list_menu);
    yed_plugin_add_event_handler(Self, h_mouse);

    event->cancel = 1;
}

static void _right_click_handler(yed_event *event) {
    if (!IS_MOUSE(event->key)) { return; }

    if (MOUSE_KIND(event->key) == MOUSE_RELEASE) {
        if (MOUSE_BUTTON(event->key) == MOUSE_BUTTON_RIGHT) {
            _right_click(event);
        }
    }
}

static void _gui_key_handler(yed_event *event) {
    int ret = 0;
    ret = yed_gui_key_pressed(event, &list_menu);
    if (ret) {
        _run_command();
    }

    if (!list_menu.base.is_up) {
        yed_delete_event_handler(h_mouse);
    }
}

static void _gui_mouse_handler(yed_event *event) {
    yed_gui_mouse_pressed(event, &list_menu);

    if (!list_menu.base.is_up) {
        yed_delete_event_handler(h_mouse);
    }
}

static void _mouse_unload(yed_plugin *self) {
}

static int _on_words(yed_event *event) {
    int row, col;
    yed_line *line;
    yed_frame *frame;

    frame = ys->active_frame;
    if(frame == NULL
    || frame->buffer == NULL) {
        return 0;
    }

    row = (MOUSE_ROW(event->key) - frame->top  + frame->buffer_y_offset + 1);
    col = (MOUSE_COL(event->key) - frame->left + frame->buffer_x_offset - frame->gutter_width + 1);

    if(bucket_array_len(frame->buffer->lines) < row) {
        return 0;
    }
    line = yed_buff_get_line(frame->buffer, row);
    if((line->visual_width + 1) >= col) {
        return 1;
    }else{
        return 0;
    }
}

static void _run_command(void) {
    array_t commands, tmp_split;
    yed_frame *frame;

    frame = ys->active_frame;

    if(frame == NULL || frame->buffer == NULL) {
        goto no_buffer_1;
    }

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
    tmp_split = sh_split(*(char **)array_item(commands, (list_menu.selection*2)+1));
    yed_execute_command_from_split(tmp_split);

    _free_string_array(&tmp_split);
    _free_string_array(&commands);
}

static void _free_string_array(array_t *array) {
    while(array_len(*array) > 0) {
        free(*(char **)array_last(*array));
        array_pop(*array);
    }
}
