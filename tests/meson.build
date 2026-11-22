fixtures = meson.current_source_dir() / 'fixtures'
foreach name : ['flathub', 'markup', 'safety', 'search']
  test(name, executable(name + '-test', name + '_test.cpp', dependencies: [catalog_dep]))
endforeach
test('catalog', executable('catalog-test', 'catalog_test.cpp', dependencies: [catalog_dep]),
  env: {'ETAL_TEST_FIXTURES': fixtures})
test('queue', executable('queue-test', 'queue_test.cpp', dependencies: [flatpak_dep]))
test('leftover', executable('leftover-test', 'leftover_test.cpp', dependencies: [flatpak_dep]))
test('update-plan', executable('update-plan-test', 'update_plan_test.cpp', dependencies: [flatpak_dep]))
