# unreal.project_version_migration

**Category**: editor
**Title**: Project Version Migration
**Risk level**: high

Updates a .uproject EngineAssociation across UE 5.6, UE 5.7, and UE 5.8, reports the support tier, and lists remaining manual rebuild steps.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: core

## Input schema

```json
{
  "type": "object",
  "properties": {
    "targetEngineVersion": {
      "type": "string",
      "description": "Target EngineAssociation value. UE 5.7 and 5.8 are primary; UE 5.6 is maintenance.",
      "enum": [
        "5.6",
        "5.7",
        "5.8"
      ]
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview the EngineAssociation edit and compatibility warnings without writing the .uproject.",
      "default": true
    },
    "projectFilePath": {
      "type": "string",
      "description": "Absolute .uproject path to edit. Defaults to the current project file."
    }
  },
  "required": [
    "targetEngineVersion"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "targetEngineVersion": "5.8",
  "dryRun": true
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: v0.35 support-contract update: reversible .uproject EngineAssociation edits for primary UE 5.7/5.8 and maintenance UE 5.6.
- Notes: PIE-blocked. Default dryRun=true. Returns targetSupportTier=primary for UE 5.7/5.8 and maintenance for UE 5.6. Does not cook, regenerate project files, run UnrealVersionSelector, or rebuild binaries.
