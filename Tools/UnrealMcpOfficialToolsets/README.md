# UEAtelier Official Toolsets

This directory contains the UE 5.8+ official ToolsetRegistry adapter proof.
Official toolsets are discovery and invocation wrappers only. Every exposed
capability must delegate to `unreal.UnrealMcpCallTool.call_tool` so UEAtelier
policy, dry-run forcing, denial, ActivityLog audit, redaction, backup/rollback,
and postcheck behavior remain authoritative.

Run the local structural tests with:

```bash
python3 -m unittest discover Tools/UnrealMcpOfficialToolsets/tests
```
