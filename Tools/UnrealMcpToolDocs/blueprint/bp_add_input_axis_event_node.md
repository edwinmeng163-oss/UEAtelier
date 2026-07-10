# unreal.bp_add_input_axis_event_node

**Category**: blueprint
**Title**: Add Input Axis Event Node
**Risk level**: medium

Adds a legacy InputAxis event node to a Blueprint EventGraph. The graphName field is accepted for forward compatibility, but only EventGraph is supported in this release.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: true
- Postcheck support: true
- Test coverage: core

## Input schema

```json
{
  "type": "object",
  "properties": {
    "blueprintPath": {
      "type": "string",
      "description": "Blueprint asset path to edit."
    },
    "graphName": {
      "type": "string",
      "description": "Target graph name. Defaults to EventGraph; only EventGraph is supported in this release.",
      "default": "EventGraph"
    },
    "axisName": {
      "type": "string",
      "description": "Legacy input axis mapping name."
    },
    "x": {
      "type": "number",
      "description": "Graph X position.",
      "default": 0
    },
    "y": {
      "type": "number",
      "description": "Graph Y position.",
      "default": 0
    }
  },
  "required": [
    "blueprintPath",
    "axisName"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "blueprintPath": "/Game/__UEAtelierPlayer/BP_TestCharacter",
  "graphName": "EventGraph",
  "axisName": "MoveForward",
  "x": 0,
  "y": 0
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: Blueprint gameplay node write tool with preflight and postcheck evidence.
- Notes: EventGraph-only for v0.28; compile/save is performed by separate Blueprint save tools.
