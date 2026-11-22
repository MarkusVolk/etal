app_sources = files(
  'app/application.cpp',
  'app/instance_service.cpp',
  'app/main_loop.cpp',
  'app/palette_watcher.cpp',
  'app/store_window.cpp',
  'event_loop/glib_poll_source.cpp',
  'fs/directory_watch.cpp',
  'main.cpp',
  'platform/clipboard.cpp',
  'platform/desktop_integration.cpp',
  'platform/launch_activation.cpp',
  'ui/app_tile.cpp',
  'ui/palette_file.cpp',
)

executable('etal',
  app_sources + protocol_sources + protocol_headers + [build_info],
  include_directories: src_inc,
  dependencies: [
    flatpak_dep, noctalia_ui_dep, noctalia_ui_codecs_dep, gio, gio_unix, tomlplusplus,
    dependency('wayland-client'), dependency('wayland-cursor'), dependency('threads'),
  ],
  install: true,
)
