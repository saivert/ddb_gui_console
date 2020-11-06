/*
    DeaDBeeF console UI
    Copyright (C) 2018-2020 Nicolai Syvertsen

    This software is provided 'as-is', without any express or implied
    warranty.  In no event will the authors be held liable for any damages
    arising from the use of this software.

    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.

    2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.

    3. This notice may not be removed or altered from any source distribution.
*/

#include <deadbeef/deadbeef.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <curses.h>

static DB_gui_t plugin;
DB_functions_t *deadbeef;

// Title formatting bytecode
static char *nowplaying_bc;
static char *statusbar_bc;

static int ui_running=1;

void
format_title (DB_playItem_t *it, const char *tfbc, char *out, int sz);

static void
render_title() {
    char str[200];
    DB_playItem_t *it = deadbeef->streamer_get_playing_track ();
    if (it) {
        format_title (it, nowplaying_bc, str, sizeof(str));
        deadbeef->pl_item_unref (it);
    } else {
        strcpy (str, "No title");
    }
    mvaddstr (0, 0, str);
    clrtoeol ();
}

static void
render_statusbar() {
    char str[200];
    DB_playItem_t *it = deadbeef->streamer_get_playing_track ();
    if (it) {
        format_title (it, statusbar_bc, str, sizeof(str));
        deadbeef->pl_item_unref (it);
    } else {
        strcpy (str, "Stopped");
    }
    mvaddstr (LINES - 1, 0, str);
    clrtoeol();
    snprintf (str, sizeof(str), " Vol: %.0f%% (%.2f dB)", deadbeef->volume_get_db() * 2 + 100 , deadbeef->volume_get_db());
    attron(COLOR_PAIR(2));
    mvaddstr (LINES - 1, COLS-strlen(str), str);
    attroff(COLOR_PAIR(2));
    clrtoeol();
}

static void
render_shortcuts() {
    attron(A_DIM);
    mvaddstr (LINES - 2, 0, "Keys: z = Prev, x = Play, c = Pause, v = Stop, b = Next, q = Quit");
    attroff(A_DIM);
}

static void
render_currentplaylist() {

    ddb_playlist_t *plt;
    plt = deadbeef->plt_get_curr();

    char playlist_title[256];
    deadbeef->plt_get_title(plt, playlist_title, sizeof(playlist_title));

    int max, maxx;
    getmaxyx(stdscr, max, maxx);
    mvprintw (1, 0, "Playlist: %s", playlist_title);
    clrtobot();
    int line=2;

    ddb_tf_context_t context;
    context._size = sizeof(ddb_tf_context_t);
    context.flags = 0;
    //context.it = nowplaying;
    context.plt = plt;
    context.idx = 0;
    context.id = 0;
    context.iter = PL_MAIN;
    context.update = 0;
    context.dimmed = 0;


    DB_playItem_t *item = deadbeef->plt_get_first (plt, PL_MAIN);
    if (!item) {
        if (plt)
            deadbeef->plt_unref (plt);
        return;
    }
    char * script = "%tracknumber%. %artist% - %title% /// %album%\n";
    char * code_script = deadbeef->tf_compile (script);
    int i = 0;
    char buffer[256];
    int currently_selected_idx = deadbeef->pl_get_cursor(PL_MAIN);
    do {
        context.it = item;
        deadbeef->tf_eval (&context, code_script, buffer, 250);

        if (i == currently_selected_idx)
            attron(COLOR_PAIR(3));

        //We set the playing indicator later
        //mvprintw(line++, 0, "%s%s", cur == item ? "> " : "  ",  buffer);

        mvaddnstr(line++, 2, buffer, maxx-2);
        if (i == currently_selected_idx)
            attroff(COLOR_PAIR(3));




        DB_playItem_t *new = deadbeef->pl_get_next (item, PL_MAIN);
        deadbeef->pl_item_unref (item);
        if (new) {
            i++;
            item = new;
        }
        else {
            break;
        }
    }
    while (line < max-4);


    DB_playItem_t *cur = deadbeef->streamer_get_playing_track();
    if (cur) {
        int idx=deadbeef->pl_get_idx_of(cur);
        if (idx >= 0) {
            attron(COLOR_PAIR(2));
            mvaddch(2+deadbeef->pl_get_idx_of(cur), 0, '>');
            attroff(COLOR_PAIR(2));
        }
        deadbeef->pl_item_unref (cur);
    }


    deadbeef->tf_free (code_script);
    if (plt)
        deadbeef->plt_unref (plt);

}

static void
action_next_playlist (void) {
    int tab = deadbeef->plt_get_curr_idx ();

    if (tab == deadbeef->plt_get_count ()-1) {
        tab = 0;
    }
    else {
        tab++;
    }

    deadbeef->plt_set_curr_idx (tab);
    deadbeef->conf_set_int ("playlist.current", tab);

}

static void
action_prev_playlist (void) {
    int tab = deadbeef->plt_get_curr_idx ();

    if (tab == 0) {
        tab = deadbeef->plt_get_count ()-1;
    }
    else {
        tab--;
    }

    deadbeef->plt_set_curr_idx (tab);
    deadbeef->conf_set_int ("playlist.current", tab);

}

static void
action_select_next_track (void) {
    int tab = deadbeef->pl_get_cursor(PL_MAIN);

    if (tab == deadbeef->pl_getcount(PL_MAIN)-1) {
        tab = 0;
    }
    else {
        tab++;
    }

    deadbeef->pl_set_cursor(PL_MAIN, tab);

}

static void
action_select_prev_track (void) {
    int tab = deadbeef->pl_get_cursor(PL_MAIN);

    if (tab == 0) {
        tab = deadbeef->pl_getcount(PL_MAIN)-1;
    }
    else {
        tab--;
    }

    deadbeef->pl_set_cursor(PL_MAIN, tab);

}

static int
seek_sec (float sec) {
    deadbeef->pl_lock ();
    DB_playItem_t *it = deadbeef->streamer_get_playing_track ();
    if (it) {
        float dur = deadbeef->pl_get_item_duration (it);
        if (dur > 0) {
            float pos = deadbeef->streamer_get_playpos ();
            pos += sec;
            if (pos > dur) {
                pos = dur;
            }
            if (pos < 0) {
                pos = 0;
            }
            deadbeef->sendmessage (DB_EV_SEEK, 0, (uint32_t)(pos * 1000), 0);
        }
        deadbeef->pl_item_unref (it);
    }
    deadbeef->pl_unlock ();
    return 0;
}

static int
ui_start (void) {
    const char *nowplaying_tf_default = "Now playing: %artist% - %title%";
    const char statusbar_tf[] = "$if2($strcmp(%ispaused%,),Paused | )$if2($upper(%codec%),-) |[ %playback_bitrate% kbps |][ %samplerate%Hz |][ %:BPS% bit |][ %channels% |] %playback_time% / %length%";

    nowplaying_bc = deadbeef->tf_compile (nowplaying_tf_default);
    statusbar_bc = deadbeef->tf_compile (statusbar_tf);


    initscr();
	if(has_colors() == FALSE)
	{	endwin();
		printf("Your terminal does not support color\n");
		exit(1);
	}
	start_color();
	init_pair(1, COLOR_RED, COLOR_BLACK);
    init_pair(2, COLOR_CYAN, COLOR_BLACK);
    init_pair(3, COLOR_BLACK, COLOR_WHITE);
    curs_set(0);
    //cbreak();
    //nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    halfdelay(1);
    noecho();


    //bkgd(COLOR_PAIR(1));
    //clear();

    render_title();
    render_shortcuts ();

    while (ui_running) {
        render_currentplaylist();
        render_shortcuts ();
        render_statusbar();
        int c = getch ();
        switch (c) {
            case 'z':
            deadbeef->sendmessage (DB_EV_PREV, 0, 0, 0);
            break;
            case 'x':
            case '\n':
            deadbeef->sendmessage (DB_EV_PLAY_CURRENT, 0, 0, 0);
            break;
            case 'c':
            deadbeef->sendmessage (DB_EV_PAUSE, 0, 0, 0);
            break;
            case 'v':
            deadbeef->sendmessage (DB_EV_STOP, 0, 0, 0);
            break;
            case 'b':
            deadbeef->sendmessage (DB_EV_NEXT, 0, 0, 0);
            break;
            case 'q':
            deadbeef->sendmessage (DB_EV_TERMINATE, 0, 0, 0);
            ui_running = 0;
            break;
            case '+':
            deadbeef->volume_set_db(deadbeef->volume_get_db()+5);
            break;
            case '-':
            deadbeef->volume_set_db(deadbeef->volume_get_db()-5);
            break;
            case KEY_UP:
            action_select_prev_track();
            break;
            case KEY_DOWN:
            action_select_next_track();
            break;
            case KEY_LEFT:
            action_prev_playlist();
            break;
            case KEY_RIGHT:
            action_next_playlist();
            break;
            case KEY_SLEFT:
            seek_sec (-5.f);
            break;
            case KEY_SRIGHT:
            seek_sec (5.f);
            break;
            case KEY_RESIZE:
            clear ();
            //refresh ();
            render_title ();
            render_currentplaylist ();
            render_shortcuts ();
            render_statusbar ();
            break;
        }
        //sleep (.1);

    }
    curs_set(1);
    nocbreak();
    endwin();

    return 0;
}

static int
ui_connect (void) {
    return 0;
}

static int
ui_disconnect (void) {
    return 0;
}

static int
ui_stop (void) {
    if (nowplaying_bc) {
        deadbeef->tf_free (nowplaying_bc);
        nowplaying_bc = NULL;
    }

    if (statusbar_bc) {
        deadbeef->tf_free (statusbar_bc);
        statusbar_bc = NULL;
    }

    ui_running = 0;

    return 0;
}


void
format_title (DB_playItem_t *it, const char *tfbc, char *out, int sz) {
    char str[sz];
    ddb_tf_context_t ctx = {
        ._size = sizeof (ddb_tf_context_t),
        .it = it,
        // FIXME: current playlist is not correct here.
        // need the playlist corresponding to the pointed track
        .plt = deadbeef->pl_get_playlist(it),
    };
    
    deadbeef->tf_eval (&ctx, tfbc, str, sizeof (str));
    if (ctx.plt) {
        deadbeef->plt_unref (ctx.plt);
        ctx.plt = NULL;
    }
    strcpy (out, str);
}

static int
ui_message (uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2) {

    switch (id) {
    case DB_EV_SONGSTARTED:
        {
        ddb_event_track_t *ev = (ddb_event_track_t *)ctx;
        if (ev->track) {
            render_title ();
        }

        break;
        }
#if 0
    case DB_EV_TRACKINFOCHANGED:
        {
            ddb_event_track_t *ev = (ddb_event_track_t *)ctx;
            if (ev->track) {
                char str[200];
                format_title (ev->track, nowplaying_bc, str, sizeof(str));
                printf ("%s\n", str);
            }
        }
        break;
#endif

    }

    return 0;
}

int
ui_run_dialog (ddb_dialog_t *conf, uint32_t buttons, int (*callback)(int button, void *ctx), void *ctx) {
    fprintf (stderr, "run_dialog: title=%s\n", conf->title);

    return ddb_button_cancel;
}


DB_plugin_t *
ddb_gui_console_load (DB_functions_t *api) {
    if (!isatty(STDIN_FILENO)) {
        return 0;
    }
    deadbeef = api;
    return DB_PLUGIN (&plugin);
}

// define plugin interface
static DB_gui_t plugin = {
    .plugin.api_vmajor = DB_API_VERSION_MAJOR,
    .plugin.api_vminor = DB_API_VERSION_MINOR,
    .plugin.version_major = 0,
    .plugin.version_minor = 1,
    .plugin.type = DB_PLUGIN_GUI,
    .plugin.id = "com.saivert.ddb_gui_console",
    .plugin.name = "Console user interface",
    .plugin.descr = "User interface using ncurses",
    .plugin.copyright =
        "Console user interface for DeaDBeeF Player.\n"
        "Copyright (C) 2018-2020 Nicolai Syvertsen\n"
        "\n"
        "This software is provided 'as-is', without any express or implied\n"
        "warranty.  In no event will the authors be held liable for any damages\n"
        "arising from the use of this software.\n"
        "\n"
        "Permission is granted to anyone to use this software for any purpose,\n"
        "including commercial applications, and to alter it and redistribute it\n"
        "freely, subject to the following restrictions:\n"
        "\n"
        "1. The origin of this software must not be misrepresented; you must not\n"
        " claim that you wrote the original software. If you use this software\n"
        " in a product, an acknowledgment in the product documentation would be\n"
        " appreciated but is not required.\n"
        "\n"
        "2. Altered source versions must be plainly marked as such, and must not be\n"
        " misrepresented as being the original software.\n"
        "\n"
        "3. This notice may not be removed or altered from any source distribution.\n"
        "\n"
        "\n"
        "This library is free software; you can redistribute it and/or\n"
        "modify it under the terms of the GNU Lesser General Public\n"
        "License as published by the Free Software Foundation; either\n"
        "version 2 of the License, or (at your option) any later version.\n"
        "\n"
        "This library is distributed in the hope that it will be useful,\n"
        "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
        "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU\n"
        "Lesser General Public License for more details.\n"
        "\n"
        "You should have received a copy of the GNU Lesser General Public\n"
        "License along with this library. If not, see <http://www.gnu.org/licenses/>.\n"
    ,
    .plugin.website = "http://saivert.com",
    .plugin.start = ui_start,
    .plugin.stop = ui_stop,
    .plugin.connect = ui_connect,
    .plugin.disconnect = ui_disconnect,
    .plugin.message = ui_message,
    .run_dialog = ui_run_dialog,
};