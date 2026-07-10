# unreal.mcp_user_tool_smoke

**Category**: self-extension
**Title**: Smoke-test User Tool
**Risk level**: low

Invoke a loaded user Python tool with minimal/dryRun args under a bounded timeout. Reports smoke_passed | smoke_failed structured lifecycle. Assistant may only claim a user tool is callable after smoke_passed confirms successful invocation.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: true
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: core

## Input schema

```json
{
  "type": "object",
  "properties": {
    "toolName": {
      "type": "string",
      "description": "User tool name to smoke-test (e.g. 'user.my_python_tool')."
    },
    "dryRunArgs": {
      "type": "string",
      "description": "Optional JSON object (encoded as a string) of args passed to the tool's execute(args). Example: '{\"dryRun\": true}'. Defaults to {\"dryRun\": true}. dryRun is always forced true for smoke."
    },
    "timeoutSeconds": {
      "type": "number",
      "description": "Bounded execution timeout (seconds). NOTE: timeout DETECTS late completion but may NOT INTERRUPT wedged Python execution; see v0.26 known limit.",
      "default": 10.0
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
- Source docs: Tools/UnrealMcpSkills/mcp-self-extension/SKILL.md
- Reason: Explicit registry: user-extension smoke verification. Required v0.26 control tool to confirm a hot-loaded user Python tool is actually callable (lifecycle.smoke_passed) before AI claims tool works.
- Notes: Known v0.26 limit: timeout detects late completion but may not interrupt wedged in-process UE Python execution; Python work may continue until it returns or the editor restarts.
