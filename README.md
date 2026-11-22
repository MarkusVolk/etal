# Etal

A store for Flatpak apps in the style of the Noctalia desktop: see what trends on Flathub, browse
the catalogs of your remotes by category, search them, and install, update or remove apps one after
the other while you go on browsing. It reads the appstream data
flatpak keeps on disk, so browsing and searching work offline and start fast. It draws with
Noctalia's UI library, without GTK or Qt, and follows the Noctalia colour scheme.

Etal is young. See [CREDITS.md](CREDITS.md) for what it builds on.

## Build

```sh
meson setup build --buildtype=release
meson compile -C build
ETAL_ASSETS_DIR=$PWD/lib/assets ./build/src/etal
```

Dependencies: a C++23 compiler, Meson, wayland-scanner, wayland-protocols, wayland-client,
wayland-cursor, wayland-egl, EGL and GLES, GLib/GIO, Pango, Cairo, HarfBuzz, FreeType, fontconfig,
xkbcommon, tomlplusplus, nlohmann_json, the stb headers, libflatpak, libappstream (1.0 or newer),
libcurl, and for the images librsvg, libwebp and libjxl.

## Keys

| Key | Action |
| --- | --- |
| Ctrl+F | Search |
| Escape, Alt+Left | Back from an app's page |
| Ctrl+Q, Ctrl+W | Quit |

## Updates in the background

`etal --check-updates` runs without a window: it refreshes the catalogs, looks for updates and
tells you with a notification. With "Update automatically" switched on in the settings it installs
them first, except those that ask for new permissions, and removes runtimes nothing needs any
more. A running Etal is asked to do the same in its queue. The systemd user timer that comes with
Etal runs this once a day:

```sh
systemctl --user enable --now etal-update.timer
```

## The web

The catalog comes from disk. From the web come the screenshots, and from Flathub's API the app of
the day, the Discover lists and the install counts; their answers are cached in `~/.cache/etal` and
used again when the network is away. Nothing about you is sent.

## Files

`etal file.flatpakref` and `etal app.flatpak` install what the file names, `etal name.flatpakrepo`
adds the remote under the file's name. A running Etal takes the file over. Remotes are listed, and
removed, on the Settings page.

## Installations

Etal shows the apps of every enabled remote of your user installation and of the system one.
New apps go to the user installation unless `settings.toml` says otherwise; that needs no
password. A remote whose catalog was never downloaded is fetched once at start, for the user
installation only.

## Settings

`~/.config/etal/settings.toml` is written by Etal. Defaults of your own go into `config.toml`
next to it, which Etal reads first and never writes:

```toml
[store]
installation = "user"   # or "system"
only_verified = false
only_free = false

[updates]
automatic = false
notify = true
```

## Actions over D-Bus

A running Etal takes named actions on `org.freedesktop.Application`; `etal --action NAME [PARAMETER]`
sends one, and starts Etal when it does not run:

```sh
busctl --user call de.flk.Etal /de/flk/Etal org.freedesktop.Application ActivateAction \
  'sava{sv}' search 1 s 'paint' 0
```

`search <text>`, `discover [trending|popular|recently-added|recently-updated]`, `category <AudioVideo|Development|Education|Game|Graphics|Network|Office|Science|System|Utility>`
(empty for all), `only-verified <true|false>`, `only-free <true|false>`, `reload`, `details <app id>`,
`back`, `installed`, `settings`, `check-updates`, `update-all`, `background-check`,
`remove-remote <name>` (of the user installation), and on the details page `install`, `remove [delete-data]`, `cancel`, `scroll <up|down|top>` and
`screenshot <next|previous>`.

## Colours

Etal reads `~/.config/etal/colors.toml`; `assets/templates/colors.toml` is a Noctalia template for
it, so the window follows the desktop theme.
