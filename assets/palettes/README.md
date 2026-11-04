# Cyberpunk Shell palettes

These Noctalia v5 palette files are stored in the repository in the format described in [the palette documentation](../../docs/user/theming/palette.mdx).

## Scarlet Protocol

`Scarlet-Protocol.json` adapts the [Cyberpunk Scarlet Protocol palette](https://cmuxthemes.com/fr/themes/cyberpunk-scarlet-protocol/) for Noctalia's shell color roles. The source is a Ghostty terminal palette, so the Noctalia surface, text, hover and outline roles are mapped to fit the same dark scarlet identity. The yellow ANSI pair is desaturated and dimmed from the source to be less glaring. Cyan and green remain in terminal syntax colors; the shell's interface accents stay in the red family.

## Umbriel Redline

`Umbriel-Redline.json` is an original Noctalia palette based on the restrained concept direction: charcoal surfaces, neutral text, and selective crimson highlights. It avoids neon cyan and keeps the terminal colors muted for extended use.

## Trying a palette

Copy either JSON file into `~/.config/noctalia/palettes/`, then select **Settings → Appearance → Theme → Custom** and choose its name. Alternatively, set the active palette in `settings.toml`:

```toml
[theme]
source = "custom"
custom_palette = "Scarlet-Protocol"
```

Use `Umbriel-Redline` for the second palette. The files only define a dark variant, which Noctalia also uses when its light mode is selected.
