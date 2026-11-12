# Nythera Discord template sources

`nythera.css` is the maintained Nythera stylesheet, initially copied from Kangy's approved `nythera-system24.theme.css` (2.1.0). It retains credit to Ezequielgk for the original Nythera adaptation. Palette placeholders replace the main surface, text, accent and settings colours; fixed status colours and the existing glass gradients/effects remain intentional styling.

After editing or replacing `nythera.css`, preserve its `{{colors.*}}` placeholders and run from the repository root:

```sh
python3 tools/build-discord-template.py
python3 tools/build-discord-template.py --check
```

Commit both the source and generated `nythera.theme.css`. The latter is the shipped template input, so users need neither Python nor a network connection to apply it. Reinstall the updated assets and reapply templates to refresh the client stylesheet. Do not copy the generated client stylesheet back over the source without restoring the palette placeholders.

The generator embeds the repository's `assets/nythera-logo-flat.svg` and `assets/nythera-header-accent.svg`. Rerun it after changing either asset.

## Pinned dependencies

Unmodified upstream files are kept in `vendor/`:

- System24: <https://github.com/refact0r/system24/tree/9ce574cfaf0b1e698251d8878d98ccf1a190e94f>, `build/system24.css`, `assets/asciid.woff`, `assets/cats.gif` and `LICENSE` (stored as `system24.LICENSE`).
- Midnight: <https://github.com/refact0r/midnight-discord/tree/c8d9eac10aca1840fbf97d0175cfbd9cec17d186>, `build/midnight.css` and `LICENSE` (stored as `midnight.LICENSE`).

Both projects are by refact0r and distributed under MIT. Their licence texts ship alongside the sources. The generator concatenates Midnight, System24, then Nythera overrides; it embeds System24's font and GIF, removes remote font imports overridden by Nythera's client-font selection, and drops Midnight's unused moon icon URL. No remote imports or asset URLs remain in the generated stylesheet.

Dependency updates are deliberate: replace the pinned files and their licences, record the new revisions here, regenerate, then check the client visually. They are not fetched during builds or theme application.
