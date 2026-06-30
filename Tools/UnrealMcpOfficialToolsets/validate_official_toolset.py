"""Structural validator for UEAtelier official ToolsetRegistry wrappers."""

from __future__ import annotations

import ast
from pathlib import Path
import sys
from typing import Iterable


ALLOWED_UNREAL_CALLS = {
    "unreal.uclass",
    "unreal.UnrealMcpCallTool.call_tool",
}

ALLOWED_UNREAL_REFERENCES = ALLOWED_UNREAL_CALLS | {
    "unreal.ToolsetDefinition",
    "unreal.UnrealMcpCallTool",
}


def _call_path(node: ast.AST) -> str:
    parts: list[str] = []
    cursor = node
    while isinstance(cursor, ast.Attribute):
        parts.append(cursor.attr)
        cursor = cursor.value
    if isinstance(cursor, ast.Name):
        parts.append(cursor.id)
    else:
        return ""
    return ".".join(reversed(parts))


def validate_source(source: str, *, source_name: str = "<source>") -> list[str]:
    """Return structural delegation-rule violations found in source."""
    try:
        tree = ast.parse(source, filename=source_name)
    except SyntaxError as exc:
        return [f"{source_name}:{exc.lineno}: syntax_error: {exc.msg}"]

    issues: list[str] = []
    for node in ast.walk(tree):
        if isinstance(node, ast.ImportFrom) and node.module == "unreal":
            issues.append(
                f"{source_name}:{node.lineno}: direct_unreal_import: "
                "import the unreal module only, then delegate through UnrealMcpCallTool"
            )
        elif isinstance(node, ast.Call):
            path = _call_path(node.func)
            if path.startswith("unreal.") and path not in ALLOWED_UNREAL_CALLS:
                issues.append(
                    f"{source_name}:{node.lineno}: direct_unreal_call: "
                    f"{path} is not an allowed UEAtelier delegation call"
                )
        elif isinstance(node, ast.Attribute):
            path = _call_path(node)
            if path.startswith("unreal.") and path not in ALLOWED_UNREAL_REFERENCES:
                issues.append(
                    f"{source_name}:{node.lineno}: direct_unreal_reference: "
                    f"{path} is not an allowed UEAtelier delegation reference"
                )
    return issues


def validate_file(path: Path) -> list[str]:
    return validate_source(path.read_text(encoding="utf-8"), source_name=str(path))


def main(argv: Iterable[str] | None = None) -> int:
    args = list(argv if argv is not None else sys.argv[1:])
    if not args:
        args = [str(Path(__file__).with_name("ueatelier_official_toolset.py"))]

    issues: list[str] = []
    for raw_path in args:
        issues.extend(validate_file(Path(raw_path)))

    if issues:
        for issue in issues:
            print(issue, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
