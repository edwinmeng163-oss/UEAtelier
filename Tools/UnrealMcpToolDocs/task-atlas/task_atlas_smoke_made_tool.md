# unreal.task_atlas_smoke_made_tool

**Category**: task-atlas
**Title**: Smoke Task Atlas Made Tool
**Risk level**: high

Run or preview a smoke test for a generated composite user tool and record failure diagnostics without deleting the tool.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: true
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "toolName": {
      "type": "string",
      "minLength": 6,
      "pattern": "^user\\.[A-Za-z0-9_]+$",
      "description": "Generated composite user tool to smoke."
    },
    "dryRun": {
      "type": "boolean",
      "default": true,
      "description": "When true, validate presence and show smoke args without executing the user tool."
    },
    "acceptChangedHashes": {
      "type": "boolean",
      "default": false,
      "description": "Forwarded to user registry reload only when a real pre-smoke reload is required. Default must remain false."
    }
  },
  "required": [
    "toolName"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: schema-minimal_

```json
{
  "toolName": "actor-name"
}
```

## Provenance
- Source docs: Docs/TaskAtlas.md
- Reason: v0.31 Task Atlas generated composite smoke wrapper over TaskAtlasService::SmokeMadeTool.
- Notes: AssistantRun approval: required for dryRun=false because real smoke can execute user Python and can update generated failure markers.
