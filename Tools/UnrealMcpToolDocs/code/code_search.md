# unreal.code_search

**Category**: code
**Title**: Search Code
**Risk level**: read_only

Searches readable project code files using bounded literal, regex, or filename matching.

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
    "query": {
      "type": "string",
      "description": "Search query."
    },
    "mode": {
      "type": "string",
      "description": "Search mode.",
      "default": "literal",
      "enum": [
        "literal",
        "regex",
        "filename"
      ]
    },
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
    "contextLines": {
      "type": "number",
      "description": "Context lines around content matches. Default 2, hard cap 10.",
      "default": 2
    },
    "maxMatches": {
      "type": "number",
      "description": "Maximum returned matches. Default 200, hard cap 1000.",
      "default": 200
    }
  },
  "required": [
    "query"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "query": "UEAtelier",
  "mode": "literal",
  "scope": "project",
  "extensions": [
    ".md"
  ],
  "maxMatches": 10
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: v0.29 Code tools bounded in-tree code search.
