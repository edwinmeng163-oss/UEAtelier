# unreal.bp_set_class_default

**Category**: blueprint
**Title**: Set Blueprint Class Default
**Risk level**: medium

Sets an allowlisted editable property on a Blueprint generated class default object and returns a readback value.

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
    "propertyName": {
      "type": "string",
      "description": "Direct class default property name to set."
    },
    "value": {
      "type": "string",
      "description": "Value text imported through Unreal property serialization."
    }
  },
  "required": [
    "blueprintPath",
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
  "propertyName": "bUseControllerRotationYaw",
  "value": "false"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: bounded Blueprint class default write tool with preflight and postcheck evidence.
- Notes: Only direct editable scalar/object reference/math struct properties are accepted.
