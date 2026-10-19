# LordOS documentation

LordOS is a free GNU/Linux distribution based on Debian 13, with its own desktop environment:
a display server and Wayland compositor written in C, its own interface engine, and native apps.
This folder documents how it works, how to build it and how to verify it. Start with the
[paper](PAPER.md) for the big picture, or [ARCHITECTURE.md](ARCHITECTURE.md) for the technical map.

Website: [lordos.org](https://lordos.org) · Brand kit: [LORDOS-BRAND.md](LORDOS-BRAND.md)

## Start here

| Document | What it covers |
|---|---|
| [PAPER.md](PAPER.md) | The project paper: vision, architecture, security model, performance, roadmap |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Components, processes, dependencies and platforms |
| [ROADMAP.md](ROADMAP.md) | Milestones, what is done and what comes next |
| [DESIGN.md](DESIGN.md) | Design guide for LordOS apps |
| [LORDOS-BRAND.md](LORDOS-BRAND.md) | Logo, colours, typography and the Lumo mascot |

## Install and build

| Document | What it covers |
|---|---|
| [ISO.md](ISO.md) | The live and installable ISO for 64-bit PCs: building, booting, final test results |
| [RASPBERRY.md](RASPBERRY.md) | The Raspberry Pi 5 image, our kernel with Landlock, boot switch files |
| [INSTALL-DEBIAN.md](INSTALL-DEBIAN.md) | Installing LordOS on an existing Debian 13 with one command |
| [VM-SETUP.md](VM-SETUP.md) | The development VM, step by step |
| [RELEASING.md](RELEASING.md) | How a release is made: changelog, tag, build, sign, SBOM and the GitHub release notes |
| [../CHANGELOG.md](../CHANGELOG.md) | What changed in each version (also the body of each GitHub release) |

## Desktop and display

| Document | What it covers |
|---|---|
| [WAYLAND.md](WAYLAND.md) | Linux apps (GTK, Qt, browsers) on our Wayland compositor |
| [WINDOWS.md](WINDOWS.md) | Moving and resizing windows, cursors |
| [DISPLAYS.md](DISPLAYS.md) | Several displays and HiDPI |
| [GPU.md](GPU.md) | GPU composition, dmabuf and the CPU path |
| [DOCK.md](DOCK.md) | Dock and taskbar |
| [SPOTLIGHT.md](SPOTLIGHT.md) | Search and the advanced clipboard |
| [TRACKPAD.md](TRACKPAD.md) | Touchpads, gestures and touchscreens |
| [SESSIONS.md](SESSIONS.md) | Login screen, sessions, user switching, boot splash hand-off |
| [REMOTE.md](REMOTE.md) | Remote desktop on the LAN (off by default, port 5673) |
| [LANGUAGES.md](LANGUAGES.md) | Languages, regions, complex scripts, input methods, error messages |

## Privacy and security

| Document | What it covers |
|---|---|
| [PRIVACY.md](PRIVACY.md) | App isolation, permissions, and what is isolated today |
| [FIREWALL.md](FIREWALL.md) | Per-app firewall, restrictions and domain rules |
| [SIGNING.md](SIGNING.md) | Package signing, trust levels and release signing |
| [IMMUTABLE.md](IMMUTABLE.md) | Design study: an immutable edition with A/B system images (0.8.2, experimental prototype) |
| [SECURITY-REVIEW-0.8.0.md](SECURITY-REVIEW-0.8.0.md) | Security review before 0.8.0-beta: every finding and its status |
| [PASSWORDS.md](PASSWORDS.md) | Passwords and the system keyring |
| [COMPLIANCE.md](COMPLIANCE.md) | Licence compliance, the SBOM, and the standards our processes align with |
| [../SECURITY.md](../SECURITY.md) | How to report a vulnerability |
| [../CONTRIBUTING.md](../CONTRIBUTING.md), [../CODE_OF_CONDUCT.md](../CODE_OF_CONDUCT.md) | How to contribute, and the code of conduct |

## Apps

| Document | What it covers |
|---|---|
| [NATIVE-APPS.md](NATIVE-APPS.md) | Installable native apps (`.lpk`) |
| [PACKAGES.md](PACKAGES.md) | Debian packages and the Store |
| [WASM.md](WASM.md) | WebAssembly apps |
| [FILES.md](FILES.md) | Files |
| [TERMINAL.md](TERMINAL.md) | Terminal |
| [EDITOR.md](EDITOR.md) | Text Editor |
| [NOTES.md](NOTES.md) | Notes |
| [DOCUMENTS.md](DOCUMENTS.md) | PDFs, archives, fonts and emoji |
| [PRINTING.md](PRINTING.md) | Printing and scanning |
| [MEDIA.md](MEDIA.md) | Photos, Player, Screenshots, Camera and Recorder |
| [MAIL.md](MAIL.md) | Mail, Calendar, Contacts and Reminders |
| [MONITOR.md](MONITOR.md) | Monitor: usage, network map, firewall, cooling |
| [SYSTEM.md](SYSTEM.md) | Disks, Backups, Logs, Clock and Help |

## System and hardware

| Document | What it covers |
|---|---|
| [SETTINGS.md](SETTINGS.md) | Settings hardware pages: displays, Bluetooth, energy, cooling, notifications, date and time, default apps |
| [NETWORK.md](NETWORK.md) | Networking and VPNs |
| [SOUND.md](SOUND.md) | Sound (PipeWire) |
| [FANS.md](FANS.md) | Fan control on the Raspberry Pi 5 and PCs |

## Images and assets

| Folder | What it holds |
|---|---|
| [screenshots/](screenshots/) | Curated English screenshots, and `gallery.json` for the website gallery |
| [brand/](brand/) | Logo, icons, mascot and colours (see [brand/README.md](brand/README.md)) |
| [boot/](boot/) | Boot menu and splash screenshots |
| [iso-final/](iso-final/) | Screenshots of the final ISO test |
| [polish/](polish/) | Before and after screenshots of the visual polish pass |
| [packs/](packs/) | Sample language pack and script test text |

## Research notes

| Document | What it covers |
|---|---|
| [HANDWIRED_NOTES.md](HANDWIRED_NOTES.md) | Research notes on a related UI project |
