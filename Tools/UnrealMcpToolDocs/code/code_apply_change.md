# unreal.code_apply_change

**Category**: code
**Title**: Apply Code Change
**Risk level**: high

Applies a previously previewed code change with dry-run, backup, lock, manifest, raw-byte write, and postcheck semantics.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: true
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: missing

## Input schema

```json
{
  "type": "object",
  "properties": {
    "previewId": {
      "type": "string",
      "description": "Preview id returned by unreal.code_preview_change."
    },
    "dryRun": {
      "type": "boolean",
      "description": "Re-validate without writing user files. Defaults to true.",
      "default": true
    },
    "confirmHighRisk": {
      "type": "boolean",
      "description": "Required for high-risk paths on real apply.",
      "default": false
    },
    "expectedSha256PerFile": {
      "type": "object",
      "properties": {},
      "required": [],
      "additionalProperties": false
    }
  },
  "required": [
    "previewId"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: schema-minimal_

```json
{
  "previewId": "<string>"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: v0.29 Code tools apply entrypoint with locked backup, manifest, and rollback-ready writes.
