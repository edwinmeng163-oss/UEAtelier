# unreal.bp_set_component_property

**Category**: blueprint
**Title**: Set Blueprint Component Property
**Risk level**: medium

Sets an allowlisted editable property on a Blueprint component template and returns a readback value.

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
    "componentName": {
      "type": "string",
      "description": "SCS component variable name."
    },
    "propertyName": {
      "type": "string",
      "description": "Direct component template property name to set."
    },
    "value": {
      "type": "string",
      "description": "Value text imported through Unreal property serialization."
    }
  },
  "required": [
    "blueprintPath",
    "componentName",
    "propertyName",
    "value"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "blueprintPath": "/Game/__UEAtelierPlayer/BP_TestCharacter",
  "componentName": "CameraBoom",
  "propertyName": "TargetArmLength",
  "value": "400.0"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: bounded Blueprint component property write tool with preflight and postcheck evidence.
- Notes: Only direct editable scalar/object reference/math struct properties are accepted.
