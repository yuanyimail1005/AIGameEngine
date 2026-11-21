#!/usr/bin/env python3
"""Regenerates the command reference section of docs/AIControl.md from the editor's `list_commands` output.

Usage: python3 Tools/gen_command_docs.py
The section between the BEGIN/END markers in docs/AIControl.md is replaced; the rest of the file is kept.
"""

import json
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(__file__))
from ember_ai import REPO_ROOT, find_editor  # noqa: E402

DOC = os.path.join(REPO_ROOT, "docs", "AIControl.md")
BEGIN = "<!-- BEGIN GENERATED COMMAND REFERENCE -->"
END = "<!-- END GENERATED COMMAND REFERENCE -->"
CATEGORY_ORDER = ["General", "Project", "Scene", "Entity", "Component", "Prefab", "Asset", "Play", "Render", "Build"]


def main():
    with tempfile.NamedTemporaryFile("w", suffix=".jsonl", delete=False) as batch:
        batch.write('{"id": 1, "method": "list_commands"}\n')
        batch_path = batch.name
    try:
        output = subprocess.run([find_editor(), "--batch", batch_path], capture_output=True, text=True, check=True).stdout
    finally:
        os.remove(batch_path)
    response = next(json.loads(line) for line in output.splitlines() if line.startswith("{"))
    commands = response["result"]

    lines = [BEGIN, "", f"{len(commands)} commands. Parameters marked *optional* may be omitted.", ""]
    categories = sorted({c["category"] for c in commands}, key=lambda c: CATEGORY_ORDER.index(c) if c in CATEGORY_ORDER else 99)
    for category in categories:
        lines += [f"### {category}", ""]
        for command in sorted((c for c in commands if c["category"] == category), key=lambda c: c["name"]):
            lines.append(f"#### `{command['name']}`")
            lines.append("")
            lines.append(command["description"])
            if command["params"]:
                lines.append("")
                lines.append("| Parameter | Description |")
                lines.append("|---|---|")
                for name, description in command["params"].items():
                    text = description.replace("|", "\\|").replace(", optional", " *(optional)*").replace("optional - ", "*optional* - ")
                    lines.append(f"| `{name}` | {text} |")
            lines.append("")
    lines.append(END)

    with open(DOC) as f:
        document = f.read()
    start, end = document.index(BEGIN), document.index(END) + len(END)
    document = document[:start] + "\n".join(lines) + document[end:]
    with open(DOC, "w") as f:
        f.write(document)
    print(f"Updated {DOC} ({len(commands)} commands)")


if __name__ == "__main__":
    main()
