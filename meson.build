project('ddb_gui_console', 'c', version : '0.1')
newt_dep = dependency('libnewt')
shared_library(meson.project_name(), ['consoleui.c'],  dependencies: [newt_dep], name_prefix: '')
