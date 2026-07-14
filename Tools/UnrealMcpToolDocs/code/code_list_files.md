# unreal.code_list_files

**Category**: code
**Title**: List Code Files
**Risk level**: read_only

Lists readable project code files within bounded scopes while excluding runtime, generated, Saved, and Content directories.

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
    "scope": {
      "type": "string",
      "description": "Readable code scope to scan.",
      "default": "project",
      "enum": [
        "project",
        "source",
        "plugins",
        "user_tools",
        "python_tools"
      ]
    },
    "extensions": {
      "type": "array",
      "description": "Optional extension filter, for example .cpp or .py.",
      "items": {
        "type": "string"
      }
    },
    "glob": {
      "type": "string",
      "description": "Optional project-relative wildcard filter."
    },
    "maxResults": {
      "type": "number",
      "description": "Maximum returned file records. Default 500, hard cap 2000.",
      "default": 500
    }
  },
  "required": [],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "scope": "project",
  "extensions": [
    ".md",
    ".json"
  ],
  "maxResults": 25
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: v0.29 Code tools read-only file discovery.
