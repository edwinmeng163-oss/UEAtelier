# unreal.editor_set_map_game_mode

**Category**: editor
**Title**: Set Map GameMode
**Risk level**: medium

Sets either the current world WorldSettings GameMode override or the project default GameMode and returns readback evidence.

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
    "gameModeClassPath": {
      "type": "string",
      "description": "GameModeBase class path to set."
    },
    "scope": {
      "type": "string",
      "description": "Where to apply the GameMode.",
      "default": "worldSettingsOverride",
      "enum": [
        "worldSettingsOverride",
        "projectDefault"
      ]
    }
  },
  "required": [
    "gameModeClassPath"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "gameModeClassPath": "/Script/Engine.GameModeBase",
  "scope": "worldSettingsOverride"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: gameplay map/project setup write tool with preflight and postcheck evidence.
- Notes: worldSettingsOverride marks the current map package dirty; projectDefault writes DefaultEngine.ini.
