#!/usr/bin/env python3
"""Isolated integration checks: python3 tests/discord_template_test.py [nythera-binary]."""

import base64
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else ROOT / "build-debug/nythera"
TEMPLATES = ROOT / "assets/templates"


class DiscordTemplateTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="nythera-discord-test-")
        self.addCleanup(self.temp.cleanup)
        self.home = Path(self.temp.name)
        self.config = self.home / "relocated-config"
        self.env = dict(os.environ, HOME=str(self.home), NYTHERA_ASSETS_DIR=str(ROOT / "assets"))
        for name in ("CONFIG", "STATE", "DATA", "CACHE"):
            self.env[f"XDG_{name}_HOME"] = str(self.home / f"relocated-{name.lower()}")
            self.env.pop(f"NYTHERA_{name}_HOME", None)
        self.env["XDG_CONFIG_HOME"] = str(self.config)
        self.entry = tomllib.loads((TEMPLATES / "builtin.toml").read_text())["templates"]["discord"]

    def run_cli(self, *args):
        result = subprocess.run([str(BINARY), "theme", *args], env=self.env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr[-3000:])
        return result.stdout

    def manifest(self):
        path = self.home / "template.toml"
        entry = dict(self.entry, input_path=str(TEMPLATES / "discord/nythera.theme.css"))
        # Only the apply-service uses undo hooks; don't need them for CLI rendering.
        entry.pop("undo_hook")
        path.write_text("[templates.discord]\n" + "\n".join(f"{k} = {json.dumps(v)}" for k, v in entry.items()))
        return path

    def render(self, mode="dark"):
        palette = self.home / "palette.json"
        roles = {"surface": "#101116", "on_surface": "#e5e5e5", "surface_variant": "#1a1b21",
                 "primary": "#e41951", "secondary": "#b9415b", "tertiary": "#a64b55",
                 "outline": "#454149", "on_surface_variant": "#b9b2b6"}
        palette.write_text(json.dumps({"dark": roles, "light": dict(roles, primary="#123456", surface="#fefefe")}))
        self.run_cli("--theme-json", str(palette), "--config", str(self.manifest()), "--default-mode", mode)

    def destinations(self):
        return [Path(p.replace("$XDG_CONFIG_HOME", str(self.config)).replace("~", str(self.home), 1))
                for p in self.entry["output_path"]]

    def test_catalog_does_not_apply_template(self):
        self.assertIn("Discord / Vesktop", self.run_cli("--list-templates"))
        # Listing does not apply the integration or create client directories.
        self.assertFalse(any(p.exists() for p in self.destinations()))

    def test_offline_bundle_is_current(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/build-discord-template.py"), "--check"], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_render_gates_missing_clients_and_updates_palette(self):
        self.render()
        self.assertFalse(any(p.exists() for p in self.destinations()))
        native = self.destinations()[0]
        native.parent.parent.mkdir(parents=True)
        self.render()
        self.assertTrue(native.exists())
        self.assertFalse(any(p.exists() for p in self.destinations()[1:]))
        css = native.read_text()
        self.assertIn("--accent-1: #e41951", css)
        self.assertIn("--ny-settings-surface: 16,17,22", css)
        self.assertNotIn("{{", css)
        self.assertNotRegex(css, r"@import\b|url\(\s*['\"]?(?:https?:)?//")
        self.assertIn("Copyright (c) 2025 refact0r", css)
        for mime, payload in re.findall(r"data:([^;]+);base64,([A-Za-z0-9+/=]+)", css):
            self.assertTrue(base64.b64decode(payload, validate=True), mime)
        self.render("light")
        self.assertIn("--accent-1: #123456", native.read_text())
        self.assertIn("--bg-1: #fefefe", native.read_text())

    def test_all_destinations_and_repeatable_cleanup_preserve_other_files(self):
        destinations = self.destinations()
        for path in destinations:
            path.parent.parent.mkdir(parents=True, exist_ok=True)
        self.render()
        for path in destinations:
            self.assertTrue(path.exists(), path)
            (path.parent / "my-theme.css").write_text("keep")
            (path.parent.parent / "settings.json").write_text('{"enabledThemes":["nythera.theme.css"]}')
        for _ in range(2):
            result = subprocess.run(["bash", str(TEMPLATES / "discord/undo.sh")], env=self.env, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
        for path in destinations:
            self.assertFalse(path.exists())
            self.assertEqual((path.parent / "my-theme.css").read_text(), "keep")
            self.assertIn("nythera.theme.css", (path.parent.parent / "settings.json").read_text())


if __name__ == "__main__":
    unittest.main()
