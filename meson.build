project('etal', ['c', 'cpp'],
  version: files('VERSION'),
  meson_version: '>=1.1',
  default_options: [
    'cpp_std=c++23',
    'cpp_extensions=false',
    'warning_level=3',
    'b_ndebug=if-release',
  ],
)

cpp = meson.get_compiler('cpp')

wayland_protocols = dependency('wayland-protocols', version: '>=1.49')
scanner = find_program('wayland-scanner')
protocol_root = wayland_protocols.get_variable('pkgdatadir')
protocol_sources = []
protocol_headers = []
foreach protocol : [
  ['xdg-decoration-unstable-v1', 'unstable/xdg-decoration/xdg-decoration-unstable-v1.xml'],
  ['xdg-foreign-unstable-v2', 'unstable/xdg-foreign/xdg-foreign-unstable-v2.xml'],
  ['primary-selection-unstable-v1', 'unstable/primary-selection/primary-selection-unstable-v1.xml'],
]
  header = custom_target(protocol[0] + '-header',
    input: protocol_root / protocol[1],
    output: protocol[0] + '-client-protocol.h',
    command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  source = custom_target(protocol[0] + '-source',
    input: protocol_root / protocol[1],
    output: protocol[0] + '-protocol.c',
    command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
    depends: header,
  )
  protocol_sources += source
  protocol_headers += header
endforeach

glib = dependency('glib-2.0', version: '>=2.76')
gio = dependency('gio-2.0', version: '>=2.76')
gio_unix = dependency('gio-unix-2.0', version: '>=2.76')
tomlplusplus = dependency('tomlplusplus', version: '>=3.4')
appstream = dependency('appstream', version: '>=1.0')
flatpak = dependency('flatpak', version: '>=1.14')
curl = dependency('libcurl')
nlohmann_json = dependency('nlohmann_json')

if cpp.get_define('_LIBCPP_VERSION', prefix: '#include <version>') != ''
  add_project_arguments('-fexperimental-library', language: 'cpp')
  add_project_link_arguments('-lc++experimental', language: 'cpp')
endif

if get_option('buildtype') == 'release'
  add_project_arguments('-ffunction-sections', '-fdata-sections', language: 'cpp')
  add_project_link_arguments('-Wl,--gc-sections', '-Wl,--as-needed', language: 'cpp')
else
  add_project_arguments('-D_GLIBCXX_ASSERTIONS', language: 'cpp')
endif

noctalia_ui_build_codecs = true
subdir('lib')

build_config = configuration_data()
build_config.set_quoted('ETAL_VERSION', meson.project_version())
build_config.set_quoted('ETAL_BUILD_REVISION', get_option('build_revision'))
build_config.set_quoted('ETAL_INSTALL_ASSETSDIR', get_option('prefix') / get_option('datadir') / 'etal/assets')
build_info = configure_file(
  input: 'src/app/build_info.h.in',
  output: 'etal_build_info.h',
  configuration: build_config,
)
src_inc = include_directories('src')

# The catalog knows nothing about the toolkit or about flatpak: the tests read fixtures with it.
catalog_lib = static_library('etal-catalog',
  files(
    'src/app/settings.cpp',
    'src/catalog/catalog.cpp',
    'src/catalog/categories.cpp',
    'src/catalog/markup.cpp',
    'src/catalog/safety.cpp',
    'src/catalog/search.cpp',
    'src/fs/xdg_dirs.cpp',
    'src/net/flathub_parse.cpp',
  ),
  include_directories: src_inc,
  dependencies: [glib, appstream, tomlplusplus, nlohmann_json],
)
catalog_dep = declare_dependency(link_with: catalog_lib, include_directories: src_inc, dependencies: [glib, appstream, tomlplusplus])

# What libflatpak knows about the installations.
flatpak_lib = static_library('etal-flatpak',
  files(
    'src/flatpak/installations.cpp',
    'src/flatpak/transactions.cpp',
    'src/flatpak/update_plan.cpp',
    'src/queue/transaction_queue.cpp',
  ),
  include_directories: src_inc,
  dependencies: [catalog_dep, flatpak, gio],
)
flatpak_dep = declare_dependency(link_with: flatpak_lib, include_directories: src_inc, dependencies: [catalog_dep, flatpak, gio])

subdir('src')

tests_opt = get_option('tests')
if tests_opt.enabled() or (tests_opt.auto() and get_option('buildtype') == 'debug')
  subdir('tests')
endif

install_subdir(noctalia_ui_assets_dir / 'fonts', install_dir: get_option('datadir') / 'etal/assets')
install_data('assets/templates/colors.toml', install_dir: get_option('datadir') / 'etal/templates')
install_data('assets/desktop/de.flk.Etal.desktop', install_dir: get_option('datadir') / 'applications')
install_data('assets/icons/de.flk.Etal.svg', install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps')
systemd_user_dir = get_option('prefix') / 'lib/systemd/user'
configure_file(
  input: 'data/systemd/etal-update.service.in',
  output: 'etal-update.service',
  configuration: {'bindir': get_option('prefix') / get_option('bindir')},
  install_dir: systemd_user_dir,
)
install_data('data/systemd/etal-update.timer', install_dir: systemd_user_dir)
install_data('LICENSE', install_dir: get_option('datadir') / 'licenses/etal')
