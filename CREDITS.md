# Credits

Etal is built on the UI library of [Noctalia](https://github.com/noctalia-dev/noctalia) and
borrows application plumbing from [Luciole](https://github.com/noctalia-dev/luciole) by way of
[Plume](https://github.com/MarkusVolk/plume). Noctalia and Luciole are MIT licensed, Copyright (c)
2026 noctalia-dev.

- `lib/` is an unchanged copy of Noctalia's `lib/` with `third_party/wuffs/` for the codec pack,
  refreshed with `tools/sync-lib.sh`. Changes to it belong upstream.
- `src/net/http_client.*` and `src/net/http_client_poll_source.h` are copied from Noctalia's
  `src/net/`.
- From Luciole, adapted: `src/app/main_loop.*`, `src/app/palette_watcher.*`, `src/event_loop/`,
  `src/fs/directory_watch.*`, `src/fs/gio_ptr.h`, `src/fs/xdg_dirs.*`,
  `src/platform/desktop_integration.*`, `src/platform/launch_activation.*`, `src/ui/palette_file.*`.
- The ideas of a fast Flathub-first store with a queue of transactions come from
  [Bazaar](https://gitlab.gnome.org/World/bazaar), those of the context tiles and background
  updates from [GNOME Software](https://gitlab.gnome.org/GNOME/gnome-software). No code is taken
  from either.
- `lib/assets/fonts/` carries the Tabler icon font; its license is
  `lib/assets/fonts/tabler-icons-license.txt`. Wuffs is under `lib/third_party/wuffs/`, Apache-2.0
  or MIT.
