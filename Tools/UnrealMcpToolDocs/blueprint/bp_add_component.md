# unreal.bp_add_component

**Category**: blueprint
**Title**: Add Blueprint Component
**Risk level**: medium

Adds a component to a Blueprint SimpleConstructionScript and optionally attaches it under a named parent component.

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
    "componentClass": {
      "type": "string",
      "description": "Component class path, for example /Script/Engine.SpringArmComponent."
    },
    "componentName": {
      "type": "string",
      "description": "New component variable name."
    },
    "attachParentComponentName": {
      "type": "string",
      "description": "Optional SCS or native scene component name to attach under. Empty uses the Blueprint root or DefaultSceneRoot.",
      "default": ""
    }
  },
  "required": [
    "blueprintPath",
    "componentClass",
    "componentName"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "blueprintPath": "/Game/__UEAtelierPlayer/BP_TestCharacter",
  "componentClass": "/Script/Engine.SpringArmComponent",
  "componentName": "CameraBoom",
  "attachParentComponentName": ""
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: Blueprint component authoring write tool with preflight and postcheck evidence.
- Notes: Compile/save is performed by separate Blueprint save tools.
