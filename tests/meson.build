fixtures = meson.current_source_dir() / 'fixtures'
foreach name : ['markup', 'safety', 'search']
  test(name, executable(name + '-test', name + '_test.cpp', dependencies: [catalog_dep]))
endforeach
test('catalog', executable('catalog-test', 'catalog_test.cpp', dependencies: [catalog_dep]),
  env: {'ETAL_TEST_FIXTURES': fixtures})
