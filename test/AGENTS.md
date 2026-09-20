# AGENTS.md — test/

Rules for this directory. See the root `AGENTS.md` for the project as a whole.

## `meson test` does not pass here, and that is pre-existing

Three of the programs registered as tests are not tests. They predate this
file, they fail on a clean checkout of `master`, and **they are not to be
"fixed" as a side effect of unrelated work** — removing or rewriting them is a
deliberate decision, not cleanup.

| Registered test | What it actually is | Result |
| --- | --- | --- |
| `Copy test` | `test-copy.c`, a CLI tool needing `<sources...> <dest dir>`; registered with `args: []` | FAIL, exit 1, prints its usage |
| `Search Engine test` | `test-nemo-search-engine.c`, opens a GTK window and runs `gtk_main()` | TIMEOUT at 30s |
| `Directory Async test` | `test-nemo-directory-async.c`, same shape | TIMEOUT at 30s |

So a green run is currently `Ok: 2` (`Eel test`, `Toolbar Layout test`) with one
failure and two timeouts. **Before claiming a change broke the suite, check the
result against that baseline.** To see only what is meant to pass:

```bash
meson test -C builddir 'Eel test' 'Toolbar Layout test'
```

`Eel test` is `eel/check-program.c`, registered from `eel/meson.build`, and runs
the `EEL_CHECK_*` self-check framework in `eel/eel-self-checks.c`.

## The self-check framework is not available outside `eel/`

`src/nemo-self-check-functions.[ch]` looks like the place to add a self check,
but it is dead code and will not compile if used: it calls
`NEMO_SELF_CHECK_FUNCTION_PROTOTYPE` and `NEMO_CALL_SELF_CHECK_FUNCTION`, which
are defined nowhere (the real macros are the `EEL_` ones). It goes unnoticed
because `nemo-main-application.c` `#define`s `NEMO_OMIT_SELF_CHECK`
unconditionally, so the whole framework is compiled out and `nemo --check` does
not exist, whatever the root `AGENTS.md` says.

**Write new tests as standalone GTest programs in this directory.**

## Writing a new test

`test-nemo-toolbar-layout.c` is the pattern to copy: `g_test_init`,
`g_test_add_func`, one `test()` entry in `meson.build`, exercising the public
API only.

A test that touches GSettings must not touch the user's real settings, and must
not depend on the *installed* schema, which is generally an older Nemo than the
tree (a key present here but missing there **aborts the process** on first
read). Compile the schema into the build directory and point the test at it:

```meson
toolbar_layout_schemas = custom_target('toolbar-layout-test-schemas',
  input: '../libnemo-private/org.nemo.gschema.xml',
  output: 'gschemas.compiled',
  command: [ find_program('glib-compile-schemas'),
             '--targetdir', '@OUTDIR@',
             meson.project_source_root() / 'libnemo-private' ],
)
```

and give the `test()` `depends:` that target plus
`env: { 'GSETTINGS_SCHEMA_DIR': meson.current_build_dir(), 'GSETTINGS_BACKEND': 'memory', 'XDG_CONFIG_HOME': meson.current_build_dir() / 'config' }`.
The memory backend keeps writes out of dconf; `XDG_CONFIG_HOME` keeps files out
of `~/.config/nemo`. That config directory **survives between runs**, so a test
that depends on a file being absent has to remove it in `main()`.

Do not call `nemo_global_preferences_init()` from a test: it opens a dozen
schemas, including `org.cinnamon.*` and `org.gnome.*`, which need not be
installed. Assign the one global you need instead —
`nemo_preferences = g_settings_new ("org.nemo.preferences")`.

Prefer tests that need no display, so they run headless.

## A new test is not done until it has been seen to fail

Introduce the bug the test is meant to catch, confirm that test and no other
reports it, then revert. A test that has only ever passed has not been shown to
test anything.
