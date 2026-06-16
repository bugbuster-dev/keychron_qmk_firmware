#!/usr/bin/env python3

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "release-preflight.sh"


def run(cmd, cwd, check=True, env=None):
    merged_env = os.environ.copy()
    merged_env.update({
        "GIT_AUTHOR_NAME": "Release Test",
        "GIT_AUTHOR_EMAIL": "release-test@example.invalid",
        "GIT_COMMITTER_NAME": "Release Test",
        "GIT_COMMITTER_EMAIL": "release-test@example.invalid",
    })
    if env:
        merged_env.update(env)
    return subprocess.run(cmd, cwd=cwd, env=merged_env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=check)


def init_repo(base, name):
    remote = base / f"{name}.git"
    repo = base / name
    run(["git", "init", "--bare", str(remote)], base)
    run(["git", "init", str(repo)], base)
    run(["git", "config", "user.name", "Release Test"], repo)
    run(["git", "config", "user.email", "release-test@example.invalid"], repo)
    (repo / "README.md").write_text(f"# {name}\n", encoding="utf-8")
    run(["git", "add", "README.md"], repo)
    run(["git", "commit", "-m", "initial"], repo)
    run(["git", "branch", "-M", "main"], repo)
    run(["git", "remote", "add", "origin", str(remote)], repo)
    run(["git", "push", "-u", "origin", "main"], repo)
    return repo


class ReleasePreflightTest(unittest.TestCase):
    def make_repos(self, tmp):
        firmware = init_repo(tmp, "firmware")
        tools = init_repo(tmp, "qmk-tools")
        return firmware, tools

    def run_preflight(self, firmware, tools, version="v9.9.9"):
        return run(["bash", str(SCRIPT), version], ROOT, check=False, env={"FIRMWARE_REPO": str(firmware), "QMK_TOOLS_REPO": str(tools)})

    def test_passes_with_clean_synced_repos_and_untracked_tools_files(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            firmware, tools = self.make_repos(Path(tmpdir))
            (tools / "scratch.md").write_text("local notes\n", encoding="utf-8")

            result = self.run_preflight(firmware, tools)

            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertIn("release preflight passed", result.stdout)

    def test_fails_when_qmk_tools_has_tracked_changes(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            firmware, tools = self.make_repos(Path(tmpdir))
            (tools / "README.md").write_text("changed\n", encoding="utf-8")

            result = self.run_preflight(firmware, tools)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("qmk-tools has uncommitted tracked changes", result.stdout)

    def test_fails_when_firmware_tag_already_exists(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            firmware, tools = self.make_repos(Path(tmpdir))
            run(["git", "tag", "v9.9.9"], firmware)

            result = self.run_preflight(firmware, tools)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("firmware tag already exists", result.stdout)

    def test_allows_existing_qmk_tools_tag_at_head(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            firmware, tools = self.make_repos(Path(tmpdir))
            run(["git", "tag", "v9.9.9"], tools)
            run(["git", "push", "origin", "v9.9.9"], tools)

            result = self.run_preflight(firmware, tools)

            self.assertEqual(result.returncode, 0, result.stdout)


if __name__ == "__main__":
    unittest.main()
