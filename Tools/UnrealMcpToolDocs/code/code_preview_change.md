# unreal.code_preview_change

**Category**: code
**Title**: Preview Code Change
**Risk level**: low

Previews structured project code edits with sha, path-policy, byte-exact match, and expected post-edit hash checks.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: true
- Postcheck support: false
- Test coverage: missing

## Input schema

```json
{
  "type": "object",
  "properties": {
    "edits": {
      "type": "array",
      "description": "Structured text edits to preview.",
      "items": {
        "type": "object",
        "properties": {
          "path": {
            "type": "string",
            "description": "Project-relative file path to edit."
          },
          "expectedSha256": {
            "type": "string",
            "description": "Expected whole-file sha256 from code_read_file."
          },
          "operation": {
            "type": "string",
            "description": "Structured edit operation.",
            "enum": [
              "replace_exact",
              "insert_before",
              "insert_after",
              "create_file"
            ]
          },
          "oldText": {
            "type": "string",
            "description": "Exact existing text for replace_exact."
          },
          "newText": {
            "type": "string",
            "description": "New text to insert, replace, or create."
          },
          "anchorText": {
            "type": "string",
            "description": "Exact anchor text for insert_before or insert_after."
          }
        },
        "required": [
          "path",
          "expectedSha256",
          "operation"
        ],
        "additionalProperties": false
      }
    }
  },
  "required": [
    "edits"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: schema-minimal_

```json
{
  "edits": []
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: v0.29 Code tools preview entrypoint with byte-exact path and sha preflight.
