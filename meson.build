project('ddb_gui_console', 'c', version : '0.1',
        default_options: [
          'buildtype=debugoptimized',
          'warning_level=1',
        ],
        license: 'GPLv2.0+')

debug_cflags = []
buildtype = get_option('buildtype')
if buildtype.startswith('debug')
  debug_cflags += '-DCONSOLEUI_DEBUG'
endif

add_project_arguments(debug_cflags, language: 'c')

ncurses_dep = dependency('ncurses')
shared_library(meson.project_name(), ['consoleui.c'],  dependencies: [ncurses_dep], name_prefix: '')
