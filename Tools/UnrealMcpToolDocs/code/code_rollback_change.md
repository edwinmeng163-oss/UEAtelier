# unreal.code_rollback_change

**Category**: code
**Title**: Rollback Code Change
**Risk level**: high

Rolls back a code edit manifest with dry-run, drift detection, lock, raw-byte restore/delete, and postcheck semantics.

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
    "editId": {
      "type": "string",
      "description": "Code edit id to roll back."
    },
    "manifestPath": {
      "type": "string",
      "description": "Project-local code edit manifest path to roll back."
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview rollback without writing files. Defaults to true.",
      "default": true
    },
    "force": {
      "type": "boolean",
      "description": "Proceed over drift when a real rollback is requested.",
      "default": false
    }
  },
  "required": [],
  "additionalProperties": false
}
```

## Usage example

_Provenance: schema-minimal_

```json
{}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: v0.29 Code tools rollback entrypoint with drift detection and backup restore.
