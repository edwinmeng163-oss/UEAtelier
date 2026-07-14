# unreal.actor_set_auto_possess

**Category**: actors
**Title**: Set Actor Auto Possess
**Risk level**: medium

Sets a Pawn actor's Auto Possess Player setting in the current editor world and returns readback evidence.

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
    "actorName": {
      "type": "string",
      "description": "Actor label, object name, or unique actor path to edit."
    },
    "autoPossessPlayer": {
      "type": "string",
      "description": "AutoPossessPlayer value to set on the Pawn.",
      "default": "Player0",
      "enum": [
        "Disabled",
        "Player0",
        "Player1",
        "Player2",
        "Player3"
      ]
    }
  },
  "required": [
    "actorName"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "actorName": "UEvolveMcpTest_PlayerCharacter",
  "autoPossessPlayer": "Player0"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: current-level actor gameplay setup write tool with preflight and postcheck evidence.
- Notes: Only Pawn actors expose AutoPossessPlayer.
