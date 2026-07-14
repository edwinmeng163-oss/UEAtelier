# unreal.user_registry_introspect

**Category**: self-extension
**Title**: Introspect User Registry
**Risk level**: read_only

Inspect user registry state, generated source metadata, handler hashes, lifecycle state, and rejection reasons without exposing private captured args.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "includeToolJson": {
      "type": "boolean",
      "description": "Include sanitized tool.json snippets. Do not include private captured args.",
      "default": false
    },
    "includePythonSha": {
      "type": "boolean",
      "description": "Include python handler SHA-256 where available.",
      "default": true
    },
    "toolName": {
      "type": "string",
      "description": "Optional exact user tool name filter. Empty means all.",
      "default": ""
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
- Source docs: Docs/SelfExtensionPipeline.md
- Reason: v0.31 visible read-only user registry introspection wrapper over TaskAtlasService::IntrospectUserRegistry.
- Notes: AssistantRun approval: not_required. This tool is read-only and sanitizes generated metadata; it never returns private captured args or Python source.
