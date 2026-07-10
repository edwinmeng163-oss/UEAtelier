# Packaging And Build Agent Guide

Read this when work touches UBT builds, release zips, install/deployment,
Windows packaging, verifier failures, or cross-platform build pitfalls. Deep
references are [BuildAndPackagingPitfalls](../BuildAndPackagingPitfalls.md),
[WindowsPackaging](../WindowsPackaging.md),
[Stage2WindowsVerify](../Stage2WindowsVerify.md), and
[DeploymentTroubleshooting](../DeploymentTroubleshooting.md).

## v0.35 Support Contract

The v0.35 source line has two primary release targets: UE 5.7 and UE 5.8.
UE 5.6 remains a maintenance compile canary and is not a primary package gate.
Use `Examples/UEvolveExample57` as the shared 5.7/5.8 build host; do not add a
duplicate UE 5.8 content project.

Development source evidence and a package-only workflow are not enough to call
UE 5.8 publicly supported. Before tagging v0.35, run real UBT against both
primary engines and complete strict zip integrity, fresh-project install,
endpoint smoke, and MCP SDK conformance on the `ue57-ue58` artifacts. The
latest public release remains v0.34.0 until those gates finish.

## Build Commands

macOS UE 5.7 primary-host build example:

```bash
REPO="$(git rev-parse --show-toplevel)"
"/Users/Shared/Epic Games/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh" \
  MyProjectEditor Mac Development \
  -Project="$REPO/Examples/UEvolveExample57/UEvolveExample57.uproject" \
  -WaitMutex
```

Repeat the same host under real UE 5.8, changing only the engine path:

```bash
REPO="$(git rev-parse --show-toplevel)"
"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" \
  MyProjectEditor Mac Development \
  -Project="$REPO/Examples/UEvolveExample57/UEvolveExample57.uproject" \
  -WaitMutex
```

Windows UE 5.7 primary-host build example:

```powershell
& "E:\3D_SOFTWARE\UE_5.7\Engine\Build\BatchFiles\Build.bat" `
  MyProjectEditor Win64 Development `
  "-Project=C:\Work\UEAtelier\Examples\UEvolveExample57\UEvolveExample57.uproject" `
  -WaitMutex
```

Run the equivalent Windows cell with `UE_5.8` before release. A source scan or
successful zip construction does not replace either real Windows UBT cell.

Before compiling, close Unreal Editor or disable Live Coding. If Editor is
open, macOS/Windows builds may fail due to locked binaries or Live Coding
locks.

## Pre-commit And CI Checks

```bash
python3 Tools/validate_tool_registry.py
python3 Tools/check_ue56_compat.py
python3 Tools/verify_package_integrity.py --root . --mode source --repo-root .
```

The historically named `check_ue56_compat.py` keeps the UE 5.6 maintenance
floor and central compatibility-shim discipline intact. It is a static gate,
not a substitute for the UE 5.7/5.8 primary UBT matrix.

Run the RAG eval when docs, recommendation, or knowledge indexing changed:

```text
/tool unreal.knowledge_eval_run {"evalPath":"Tools/UnrealMcpKnowledge/Evals","includeDetails":false}
```

If ToolRegistry changes, also keep the plugin resource registry mirror
byte-for-byte identical.

## Install And Deployment

The plugin is normally installed into a target project as a project-level
plugin.

Automated installer:

```bash
python3 Tools/install_unrealmcp_to_project.py --project "/path/to/YourProject/YourProject.uproject" --dry-run
python3 Tools/install_unrealmcp_to_project.py --project "/path/to/YourProject/YourProject.uproject"
```

For v0.35 the installer classifies `EngineAssociation` 5.7 and 5.8 as
primary, warns for maintenance-only 5.6, and fails closed for missing or
unverified versions unless the operator explicitly passes
`--allow-unverified-engine`.

Manual install copies:

```text
Plugins/UnrealMcp -> <TargetProject>/Plugins/UnrealMcp
Tools             -> <TargetProject>/Tools
Schemas           -> <TargetProject>/Schemas
Docs              -> <TargetProject>/Docs
```

Then enable `UnrealMcp` and `PythonScriptPlugin` in the target `.uproject`,
close Unreal Editor, build the project, and open the project. The MCP endpoint
exists only while Unreal Editor is open and the plugin is loaded.

Avoid having both project-level and engine-level copies of `UnrealMcp`; stale
or locked binaries cause confusing Windows failures.

## Pitfalls Index

Read [BuildAndPackagingPitfalls](../BuildAndPackagingPitfalls.md) before
authoring a new release chunk. It indexes:

1. Unity-build symbol collisions.
2. Multi-engine host verification (v0.35: primary UE 5.7/5.8 plus the UE 5.6 maintenance canary).
3. Build target name traps.
4. Stale plugin dylib shadow.
5. Cross-platform zip path separator handling.
6. Scaffold source path fallback.
7. Codex dispatch CLI hygiene.
8. Hermes coordinator hygiene.
9. End-to-end verification at integration boundaries.
10. Codex spec deviation policy.
11. Editor-load warnings versus UBT errors.
12. Mac-only build verify missing MSVC-promoted-to-error warnings.

Unity-build hygiene rule: the UnrealMcp module has `bUseUnity = false` because
per-file anonymous namespaces with same-named helpers collided under UBT unity
build. Do not define generic cross-file helpers in anonymous namespaces. Put
cross-file helpers at `namespace UnrealMcp` scope in exactly one file and
forward-declare them at the same scope.

## Windows Packaging

Windows source-only zips are produced automatically by:

```text
.github/workflows/win-release-package.yml
```

on every `v*.*.*` tag push and attached to the matching GitHub release. The
workflow runs on a `windows-2022` GitHub Actions runner so the zip retains
PowerShell `Compress-Archive` backslash entry paths. This is the canonical
Windows-tested artifact shape.

The workflow does not install Unreal Engine and does not run UBT. Its success
proves the source-only Windows artifact shape and strict package integrity, not
UE 5.7 or UE 5.8 engine compatibility. Real Windows UBT for both primary
engines and clean-project endpoint/conformance smoke must finish before the
release tag is pushed.

For a pre-tag candidate, dispatch the workflow against the pushed candidate
branch and leave the `tag` input empty:

```bash
gh workflow run win-release-package.yml \
  --repo edwinmeng163-oss/UEAtelier \
  --ref <candidate-branch>
```

Blank-tag dispatch checks out the event's exact `GITHUB_SHA`, validates the
descriptor version, and uploads only the 30-day Actions artifact. It does not
attach to or create a GitHub release. A non-empty `tag` input retains the
release/backfill behavior and must match `UnrealMcp.uplugin` `VersionName`.

Manual fallback is documented in [WindowsPackaging](../WindowsPackaging.md).
The Windows collaborator should:

1. Sync to the release-candidate commit before the public tag is pushed.
2. Clean stale plugin/example binaries and intermediates.
3. Build `MyProjectEditor` against `Examples/UEvolveExample57` with UE 5.7.
4. Clean cross-engine intermediates, then build the same host with UE 5.8.
5. Run registry and UE 5.6 maintenance-compatibility validators.
6. Run the full primary-engine safety/automation release matrix.
7. Run `Tools/package_plugin.ps1`; the source artifact name is
   `UnrealMcp-v<version>-win-ue57-ue58-projectroot.zip`.
8. Verify with `Tools/verify_package_integrity.py --strict`.
9. Extract into clean UE 5.7 and UE 5.8 projects and run endpoint smoke plus
   MCP SDK conformance.
10. Upload the zip and `.sha256` sidecar to the existing release.

Do not repackage a Windows-tested zip on macOS just to satisfy a local verifier.
Fix the verifier or verification context instead.

## Stage 2 Mac Projectroot Zip E2E

Every v0.35 projectroot zip must be e2e-tested under both primary engines
before tag-publish. Run the sequence separately for UE 5.7 and UE 5.8:

1. Create a fresh `TP_Blank` test project, for example
   `/tmp/UEvolveMacZipTest57` or `/tmp/UEvolveMacZipTest58`.
2. Extract the zip at project root, not under `Plugins/`.
3. UBT-build the editor module against the matching local UE 5.7 or UE 5.8
   install. A real UE 5.8 UBT build is mandatory; validators alone do not count.
4. Launch editor with `-nullrhi -unattended`; poll for `LogUnrealMcp:`
   listening and `Engine is initialized`.
5. Confirm port 8765 is bound.
6. Run `bun install --cwd Tools/UnrealMcpCodexBridge` once per machine, then
   run `bun run --cwd Tools/UnrealMcpCodexBridge test-sdk-conformance.ts`
   against the running editor endpoint.
7. Run smoke calls for `tools/list`,
   `unreal.editor.python_runtime_info`, and
   `unreal.mcp_apply_scaffold` dry run for `unreal.fps.bootstrap`.
8. Kill the editor, wait for port 8765 to free, and remove the temp project and
   log.

If the scaffold dry run fails on a missing scaffold file, suspect a packager
gap before opening an applier bug.

## Stale Plugin-level Binary Trap

After plugin code changes and before any example-host smoke, remove stale
plugin-level binaries:

```bash
rm -rf Plugins/UnrealMcp/Binaries Plugins/UnrealMcp/Intermediate
```

Then rebuild the example host. Fresh example-host binaries should live under
the example host `Binaries/` directory. If the plugin-level dylib/dll still
exists, repeat cleanup.

## Projectroot Zip Overlay Invariants

Every Mac/Windows source-only or Windows full-experience projectroot zip must
ship:

```text
<UserProject>/Plugins/UnrealMcp/
<UserProject>/Tools/UnrealMcpToolRegistry/
<UserProject>/Tools/unreal_mcp_fetch_docs.py
<UserProject>/Tools/install_unrealmcp_to_project.py
<UserProject>/Tools/UnrealMcpPyTools/
<UserProject>/Tools/UnrealMcpToolScaffoldStarters/
<UserProject>/Tools/UnrealMcpToolScaffolds/
  fps_bootstrap/
  verify_input_drives_pawn/
<UserProject>/Tools/UnrealMcpSkills/
<UserProject>/Tools/UnrealMcpKnowledge/
<UserProject>/Tools/UnrealMcpTests/
<UserProject>/Tools/UnrealMcpCodexBridge/
<UserProject>/Docs/FIRST_LAUNCH.md
```

`UnrealMcpToolScaffoldStarters` and `UnrealMcpToolScaffolds` are not the same.
Starters are templates cloned by `unreal.scaffold_mcp_tool`; scaffolds are the
pre-staged working copies read by `unreal.mcp_apply_scaffold`. Both must
coexist.

`Tools/package_plugin.sh` and `Tools/package_plugin.ps1` enforce these
invariants. The top-level fetcher and installer are real files, not symlinks;
the packaged knowledge README and Python tests depend on them. If a new
top-level overlay subtree or required file is added, add both the copy step and
the matching assertion in both packagers.

## Release Publish Flow

Build the Mac candidate from the exact committed revision in a clean detached
worktree so untracked local files cannot leak into the archive:

```bash
release_sha="$(git rev-parse HEAD)"
package_worktree="$(mktemp -d /tmp/ueatelier-package.XXXXXX)"
git worktree add --detach "$package_worktree" "$release_sha"
(cd "$package_worktree" && bash Tools/package_plugin.sh --version <ver> --output <absolute-output-dir>)
git worktree remove "$package_worktree"
```

After a tag-moving fix:

```bash
bash Tools/package_plugin.sh --version <ver>
# Complete the UE 5.7/5.8 UBT and package E2E matrix before moving the tag.
git tag -d <tag> && git tag <tag> <newSha>
git push origin <branch>
git push --force origin <tag>
gh release delete-asset <tag> <old-asset-name> --yes --repo <repo>
gh release delete-asset <tag> <old-asset-name>.sha256 --yes --repo <repo>
gh release upload <tag> <new-zip> <new-zip>.sha256 --repo <repo>
gh release edit <tag> --notes-file <updated-body.md> --repo <repo>
```

The source-only packager emits
`UnrealMcp-v<ver>-mac-ue57-ue58-projectroot.zip`. Always update both the
filename and SHA in release notes. Do not move the tag until the UE 5.7/5.8
UBT and package E2E matrix is complete.
