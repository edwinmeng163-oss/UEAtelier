# unreal.task_atlas_delete_made_tool

**Category**: task-atlas
**Title**: Delete Task Atlas Made Tool
**Risk level**: high

Remove a Task Atlas generated user composite and reload the user registry.

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
      "description": "Generated user tool name to delete. Only tools with Task Atlas generator metadata are eligible."
    },
    "confirm": {
      "type": "boolean",
      "const": true,
      "description": "Must be true. The tool refuses deletion without explicit confirmation."
    },
    "dryRun": {
      "type": "boolean",
      "default": false,
      "description": "When true, report the target dir and reload effect without deleting. confirm=true is still required."
    }
  },
  "required": [
    "toolName",
    "confirm"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: schema-minimal_

```json
{
  "toolName": "actor-name",
  "confirm": false
}
```

## Provenance
- Source docs: Docs/TaskAtlas.md
- Reason: v0.31 Task Atlas generated composite deletion wrapper with confirm guard.
- Notes: AssistantRun approval: required for dryRun=false deletion. The wrapper refuses confirm=false and the service refuses non-Task-Atlas-generated tools.
