# Nythera

Nythera is my personal fork of [Noctalia](https://github.com/noctalia-dev/noctalia), a native Wayland desktop shell written in C++.

I started this because I wanted something a bit different from the usual desktop bars and panels. Noctalia already provides a solid foundation, so rather than starting from scratch, I decided to build on what was already there and see how far I could take it.

The general idea is a more flexible desktop shell with floating islands, dynamic layouts, screen wrapping and a darker sci-fi/fantasy aesthetic.

I'm not trying to reinvent everything Noctalia does. Most of its existing functionality is exactly what I need. I just want more freedom over how it looks and behaves.

### A Note on Development

I'll get this out of the way early: **Nythera is largely vibe coded.** 🤣

I'm learning to code, but C++ is completely outside my wheelhouse. I couldn't write this project myself, so I'm using AI tools to help turn my ideas into something that actually works.

The ideas, design direction and decisions are mine, while the implementation is largely AI-generated. I'm testing things, breaking things, figuring out what works and probably giving the AI a headache in the process.

I'm not pretending to be a C++ developer. I'm just someone who enjoys messing around with Linux, has far too many ideas, and now has the tools to actually try building them.

## What's Different?

### Dynamic Bars & Islands

One of the main things I'm working on is moving away from the traditional single desktop bar.

Nythera currently experiments with:

- **Floating Islands** — Separate sections of the bar into individual panels rather than one continuous strip.
- **Dynamic Island** — A section that changes to display relevant information depending on what's happening, such as media playback, notifications and system events.
- **Screen Wrap** — Extends the bar's appearance around the edges of the display, creating a frame that integrates with the islands.
- **Traditional Bars** — The original bar layout is still available. Not everything needs to be an island!

### Appearance

I'm going for a darker, futuristic look with some sci-fi and fantasy influences. Think dark backgrounds, subtle lighting, transparency and a bit of crimson.

That doesn't mean everyone has to use the same appearance, though. The intention is to keep things configurable rather than hardcoding everything to my preferences.

## What I'm Working Towards

There's quite a bit I'd still like to do.

- **Freeform Islands** — Being able to drag islands around the screen rather than being restricted to fixed bar positions.
- **Bar Merging** — Allowing multiple bars and islands to connect together without awkward gaps, overlapping borders or mismatched corners.
- **Better Screen Wrap Integration** — Improving how the screen frame interacts with bars and application windows, including configurable spacing.
- **More Customisation** — Greater control over island positioning, appearance and behaviour.

These are ideas I'm actively exploring, not a promise that everything will make it in.

## Existing Features

Since Nythera is based on Noctalia, it retains most of the functionality already available in the original project, including:

- Multi-monitor bars, widgets, taskbar and workspaces.
- Application launcher and dock.
- Notifications, control centre and on-screen displays.
- Media, audio, network and brightness controls.
- Wallpaper management and lock screen.
- Clipboard history and desktop widgets.
- TOML configuration, hot reload and graphical settings.
- Native Wayland integration and IPC controls.

## Compositor Support

I'm developing Nythera primarily on **Umbriel**, since that's what I use.

Noctalia already supports several Wayland compositors, including Niri, Hyprland, Sway, Mango, Labwc and others. Much of that support should carry over, although I haven't tested Nythera extensively outside my own setup.

## Building & Installation

Nythera is still in development, so I haven't put together a proper installation process yet.

For now, the build process is based on Noctalia's existing setup. See [BUILDING.md](BUILDING.md) for the current instructions.

Expect things to change, particularly configuration paths, executable names and other bits that still reference Noctalia.

## Credits

Nythera wouldn't exist without [Noctalia](https://github.com/noctalia-dev/noctalia) and the work of its developers and contributors.

The vast majority of the underlying functionality and architecture comes directly from their project. I'm building on top of that rather than pretending I've written an entire desktop shell myself.

Nythera is an independent fork and isn't affiliated with or endorsed by the Noctalia developers.

See [CREDITS.md](CREDITS.md) for additional acknowledgements and third-party dependencies.

## License

Nythera is licensed under the [MIT License](LICENSE), the same as Noctalia.

The original copyright and licence notices are retained.

---

This is mostly a personal project that I'm building because I enjoy messing around with Linux and wanted to make something a bit more my own.

I've got plenty of ideas for it. Whether I manage to implement all of them is another matter. 🤣