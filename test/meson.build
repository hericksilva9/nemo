
test('Search Engine test',
  executable('test-nemo-search-engine',
    [ 'test-nemo-search-engine.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('Directory Async test',
  executable('test-nemo-directory-async',
    [ 'test-nemo-directory-async.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('Copy test',
  executable('test-nemo-copy',
    [ 'test-copy.c', 'test.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
    c_args: nemo_definitions,
  ),
  args: []
)

toolbar_layout_schemas = custom_target('toolbar-layout-test-schemas',
  input: '../libnemo-private/org.nemo.gschema.xml',
  output: 'gschemas.compiled',
  command: [ find_program('glib-compile-schemas'),
             '--targetdir', '@OUTDIR@',
             meson.project_source_root() / 'libnemo-private' ],
)

test('Toolbar Layout test',
  executable('test-nemo-toolbar-layout',
    [ 'test-nemo-toolbar-layout.c', '../src/nemo-toolbar-layout.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, json, nemo_private ],
    c_args: nemo_definitions,
  ),
  depends: toolbar_layout_schemas,
  env: {
    'GSETTINGS_SCHEMA_DIR': meson.current_build_dir(),
    'GSETTINGS_BACKEND': 'memory',
    'XDG_CONFIG_HOME': meson.current_build_dir() / 'config',
  },
  args: []
)
