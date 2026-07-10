# unreal.code_read_file

**Category**: code
**Title**: Read Code File
**Risk level**: read_only

Reads a bounded slice of a readable project code file and returns the whole-file sha256 for later edit validation.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "path": {
      "type": "string",
      "description": "Project-relative code file path to read."
    },
    "startLine": {
      "type": "number",
      "description": "1-based line number to start reading from.",
      "default": 1
    },
    "lineCount": {
      "type": "number",
      "description": "Optional number of lines to return.",
      "default": 0
    },
    "maxChars": {
      "type": "number",
      "description": "Maximum returned characters. Default 100000, hard cap 500000.",
      "default": 100000
    }
  },
  "required": [
    "path"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "path": "README.md",
  "startLine": 1,
  "lineCount": 20,
  "maxChars": 8000
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: v0.29 Code tools read-only file readback with whole-file sha.
