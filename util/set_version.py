#!/usr/bin/env python3
"""Put the build version into src/build/settings/base.json and print it.

On a tagged commit it is the tag (v1.5.0 -> 1.5.0), otherwise the last tag plus the commit
(1.5.0+3.gabc1234), so the AppImage, the .deb and the About box all match git.
"""
import json
import os
import re
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SETTINGS = os.path.join(ROOT, "src", "build", "settings", "base.json")


def git_version():
    try:
        d = subprocess.check_output(["git", "describe", "--tags", "--long", "--match", "v[0-9]*"],
                                    cwd=ROOT).decode().strip()
    except subprocess.CalledProcessError:
        sha = subprocess.check_output(["git", "rev-parse", "--short=7", "HEAD"], cwd=ROOT).decode().strip()
        return "0.0.0+g" + sha
    tag, n, sha = re.match(r"v(.+)-(\d+)-g([0-9a-f]+)$", d).groups()
    return tag if n == "0" else "{}+{}.g{}".format(tag, n, sha)


def main():
    version = git_version()
    with open(SETTINGS) as f:
        settings = json.load(f)
    settings["version"] = version
    with open(SETTINGS, "w") as f:
        json.dump(settings, f, indent=4)
        f.write("\n")
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a") as f:
            f.write("version={}\n".format(version))
    print(version)


if __name__ == "__main__":
    main()
