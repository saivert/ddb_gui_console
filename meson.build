project('ddb_gui_console', 'c', version : '0.1')
ncurses_dep = dependency('ncurses')
shared_library(meson.project_name(), ['consoleui.c'],  dependencies: [ncurses_dep], name_prefix: '')
