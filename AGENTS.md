# AGENTS.md

This file provides guidance to coding agents working in this repository.

## Project

Nemo is the Cinnamon desktop's file manager — a C/GTK3 fork of GNOME Files (Nautilus 3.4). It also
draws and manages the Cinnamon desktop (icons on the root window). Build system is Meson; packaging
is Debian.

## Build / test / run

```bash
meson setup builddir                  # add --buildtype=debug for DEBUG()/frame pointers
ninja -C builddir
meson test -C builddir                # all tests
meson test -C builddir 'Copy test'    # one test ('Search Engine test', 'Directory Async test', 'Copy test')
sudo ninja -C builddir install
```

Packaging build (what CI and Mint use — note it *disables* `meson test`):

```bash
dpkg-buildpackage -us -uc             # see debian/rules for the configure flags
```

Useful meson options (see `meson_options.txt`): `-Dtracker=true|auto`, `-Dgtk_layer_shell=true`
(Wayland desktop), `-Dexif`, `-Dxmp`, `-Dselinux`, `-Dempty_view`, `-Dgtk_doc=true`,
`-Ddeprecated_warnings=true`. Debian builds with `debugoptimized`, `gtk_doc=true`,
`gtk_layer_shell=true`.

Running a build in place needs the GSettings schema compiled and visible:
`GSETTINGS_SCHEMA_DIR=builddir/libnemo-private ./builddir/src/nemo` (the schema source is
`libnemo-private/org.nemo.gschema.xml`). Nemo is a GApplication singleton — `nemo --quit` before
relaunching. There is no `nemo --check`: `nemo-main-application.c` defines `NEMO_OMIT_SELF_CHECK`
unconditionally, which compiles out both the option and the suites in
`src/nemo-self-check-functions.c`. See `test/AGENTS.md` before writing a test.

### Debugging

Debug output is gated by topic flags: `NEMO_DEBUG=Actions,Window nemo --debug`
(`NEMO_DEBUG=help` lists topics; the flag list lives in `libnemo-private/nemo-debug.[ch]`, used via
`#define DEBUG_FLAG NEMO_DEBUG_X` + `DEBUG(...)` at the top of a .c file).

### Translations

There is no `po/` directory — translations come from the `cinnamon-translations` repo. `./makepot`
regenerates `nemo.pot` from C sources, Glade files, actions and the polkit policy;
`./generate_additional_file` regenerates the translated `.desktop` and mime `.xml` files (requires
`mintcommon`). Run these when user-visible strings change.

## Architecture

### Binaries (`src/`)

`NemoApplication` (`nemo-application.c`) is an abstract GtkApplication base with two concrete
subclasses, each producing its own binary from the same shared `nemoCommon_sources`:

- **`nemo`** — `NemoMainApplication` (`nemo-main-application.c`, `nemo-main.c`): windows, CLI option
  parsing, D-Bus (`org.freedesktop.FileManager1`), mount/unmount handling.
- **`nemo-desktop`** — `NemoDesktopApplication` (`nemo-desktop-application.c`): the desktop-icon
  windows. `NemoDesktopManager` owns one desktop window per monitor and coordinates with Cinnamon
  over D-Bus (`data/org.Cinnamon.xml`). Wayland desktop windows use gtk-layer-shell.

Smaller helper binaries: `nemo-connect-server`, `nemo-open-with`, `nemo-autorun-software`, and the
libexec `nemo-extensions-list`.

### Window → pane → slot → view

`NemoWindow` holds one or more `NemoWindowPane` (split view); each pane holds `NemoWindowSlot`s
(tabs). A slot owns a `NemoView` for its location. Views are not hardcoded: each view module calls
`nemo_view_factory_register()` at init with a `NemoViewInfo` (id, labels, `create`, `supports_uri`),
and `nemo-window-manage-views.c` picks and instantiates one per location. Registered views: icon,
compact (both `nemo-icon-view.c`), list (`nemo-list-view.c`), desktop icon / desktop icon grid, and
optionally empty view. `nemo-window-manage-views.c` is the location-change state machine — go there
for navigation, view switching and load failures, not to `nemo-window.c`.

### File/directory model (`libnemo-private/`)

`NemoFile` and `NemoDirectory` are cached, ref-counted, *asynchronous* wrappers over GIO. Nothing
blocks: callers request attributes with `nemo_file_call_when_ready()` /
`nemo_directory_call_when_ready()` (with a `NemoFileAttributes` mask) or subscribe with
`nemo_file_monitor_add()` / `nemo_directory_file_monitor_add()`, and must pair every call with its
cancel/remove. `nemo-directory-async.c` is the engine that satisfies those requests; subclasses
(`nemo-vfs-directory.c`, `nemo-search-directory.c`, `nemo-desktop-directory.c`,
`nemo-merged-directory.c`) specialize where files come from. File operations (copy/move/trash/link)
live in `nemo-file-operations.c` and are made undoable via `nemo-file-undo-manager.c` /
`nemo-file-undo-operations.c`.

### Other subsystems

- **`eel/`** — inherited GTK/GLib utility layer (static lib, `G_LOG_DOMAIN="Eel"`), including
  `eel-canvas.c`, the canvas the icon views are built on.
- **`libnemo-private/`** — static lib with the model, preferences, actions, search, DnD, icon
  container. Everything in `src/` links it.
- **`libnemo-extension/`** — the *public* shared library and its GIR/typelib (`Nemo-3.0`). This is a
  stable ABI: bump `nemo_extension_current`/`_revision` in `meson.build` per the comment there,
  never break interfaces, and keep `debian/libnemo-extension1.symbols` in sync. Extensions are
  GTypeModules loaded from `NEMO_EXTENSIONDIR` (`<libdir>/nemo/extensions-3.0`, deliberately frozen
  at "3.0") by `libnemo-private/nemo-module.c`, which looks for `nemo_module_initialize`,
  `nemo_module_list_types`, `nemo_module_shutdown`. Providers: menu, column, info, property page,
  location widget, name-and-desc.
- **Actions** — user-defined menu entries from `*.nemo_action` keyfiles in
  `$XDG_DATA_DIRS/nemo/actions` and `~/.local/share/nemo/actions`. `NemoActionManager` loads and
  watches them plus the JSON layout file; `NemoAction` (a GtkAction subclass) does token
  substitution and condition evaluation. Format reference: `files/usr/share/nemo/action-info.md`.
  `action-layout-editor/` is a separate Python/GTK app for arranging them.
- **Search** — `NemoSearchEngine` fans out to `nemo-search-engine-advanced.c` (the default, in-tree)
  and optionally `nemo-search-engine-tracker.c`. Full-text search of non-plain-text formats goes
  through *search helpers*: `*.nemo_search_helper` keyfiles in `<datadir>/nemo/search-helpers` that
  name an extractor printing text to stdout (`search-helpers/README.md`).
- **Preferences** — GSettings only. Schemas in `libnemo-private/org.nemo.gschema.xml` (`org.nemo`,
  `org.nemo.preferences`, `.icon-view`, `.list-view`, `.desktop`, `.window-state`, `.plugins`, …);
  access through the global `GSettings *nemo_preferences` etc. and the `NEMO_PREFERENCES_*` key
  macros in `nemo-global-preferences.h`. Add a key to the schema *and* a macro.
- **UI resources** — `gresources/` holds the GtkUIManager XML menu definitions (`*-ui.xml`), Glade
  files and CSS, compiled into the binary. Menus are still GtkAction/GtkUIManager-based, not GMenu.
- **D-Bus interfaces** are generated at build time by `gdbus-codegen` from `data/dbus-interfaces.xml`
  (org.Nemo), `data/freedesktop-dbus-interfaces.xml` and `data/org.Cinnamon.xml`.
- **`files/usr/share/nemo/`** is installed verbatim via `install_subdir` — drop shipped actions and
  docs there, not into `data/`.

## Conventions

- Indentation is inconsistent by era: files inherited from Nautilus use 8-wide tabs (see the
  `-*- Mode: C ... -*-` modeline headers), newer Nemo code uses 4 spaces. Match the file you're in.
- Types are `Nemo*`, functions `nemo_<object>_<verb>()`, one GObject per `.c`/`.h` pair named after
  it. Newer objects use `G_DECLARE_FINAL_TYPE`; older ones use the hand-written macro boilerplate.
- Commit subjects are `<file or area>: <Sentence-case description>.` — e.g.
  `nemo-view.c: Don't disable update interval progression while loading.`,
  `window: Open favorites:///folder at its real location.` Version-bump commits are bare
  (`6.7.7-unstable`).
- `-Werror=implicit-function-declaration` is always on; deprecation warnings are off by default
  (this is a GTK3 codebase that intentionally uses deprecated APIs such as GtkAction/GtkUIManager —
  don't "modernize" them incidentally).
- CI (`.github/workflows/build.yml`) builds against `linuxmint/xapp` and
  `linuxmint/cinnamon-desktop` from master and runs codespell.
