# Windows Packaging Guide for UnrealMcp Releases

> **Update (post-v0.22.0)**: The Windows source-only zip is now produced automatically by `.github/workflows/win-release-package.yml` on every `v*.*.*` tag push, then attached to the matching GitHub release via `gh release upload --clobber`. **No manual Windows packaging is required for new tags going forward.** The workflow runs on a `windows-2022` GitHub Actions runner so the zip retains `Compress-Archive`'s backslash entry paths (the canonical Windows-tested artifact shape). If the GitHub Actions run fails for any reason, the steps in this guide are the manual fallback.
>
> **Pre-tag candidate mode (v0.35+)**: manually dispatch the workflow against the candidate branch with the `tag` input left empty. It packages the event's exact commit and uploads `UnrealMcp-v<VersionName>-win-zip` as a 30-day Actions artifact, but does not create or modify a GitHub release. Supplying a tag keeps release upload behavior and fails closed if the tag version differs from `UnrealMcp.uplugin`; historical mismatched/hyphenated tags are not guaranteed automated backfill targets.
>
> **⚠️ Timing-race caveat (seen on v0.26.0)**: the attach step is **best-effort** — it does `gh release view <tag>` and skips upload if the release doesn't exist yet. Tag-push triggers CI **immediately**, but PM usually creates the GitHub release ~1 minute later (after the Mac zip builds). So the CI attach almost always loses the race: the Win zip lands only as a **30-day workflow artifact**, NOT on the release. **PM must back-fill it** — see § 3.2 below. Two ways to avoid the race next time: (a) `gh release create <tag>` **before** `git push origin <tag>` so the release exists when CI checks, or (b) re-run the workflow via `workflow_dispatch` (with the `tag` input) after the release exists.
>
> The guide below stays canonical for: (1) the current-source manual fallback
> if the workflow fails, (2) back-filling a CI artifact after a timing race,
> and (3) the rare full-experience zip with prebuilt Win64 binaries. Manual
> packaging of an older tag must use that tag's own engine suffix and script;
> the workflow discovers that historical suffix automatically.

This guide tells a Windows collaborator how to produce a `UnrealMcp-vX.Y.Z-win-*.zip` and attach it to an existing GitHub release. PM does the Mac zip and creates the release; this side packages the Windows companion.

> **v0.35 support contract:** UE 5.7 and UE 5.8 are the primary targets and
> share `Examples/UEvolveExample57` as their build host. UE 5.6 is a
> maintenance compatibility canary. The current source artifact is named
> `UnrealMcp-vX.Y.Z-win-ue57-ue58-projectroot.zip`.

> Audience: a Windows machine with UE 5.7 and UE 5.8 installed for primary
> release verification, with the UEAtelier repository cloned.

---

## 1. One-time setup

### 1.1 Required software

| Tool | Version | Why |
|------|---------|-----|
| Unreal Engine | 5.7 and 5.8 (latest patches); 5.6 optional maintenance canary | Build the plugin binaries |
| Visual Studio | 2022 with "Game development with C++" workload | UBT toolchain |
| Git for Windows | Latest | Clone + fetch tags |
| PowerShell | 7+ (`pwsh`) — Windows PowerShell 5.1 also works | Run the packaging script |
| Python | 3.10+ | Run validators (`validate_tool_registry.py`, `verify_package_integrity.py`) |
| GitHub CLI | Latest (`gh --version`) | Upload zip to existing release |

### 1.2 First-time auth

```powershell
gh auth login
# Choose GitHub.com -> HTTPS -> authenticate browser
# Pick scopes: repo (read + write release assets)
```

Verify you can see the project's releases:

```powershell
gh release list --repo edwinmeng163-oss/UEAtelier --limit 5
```

### 1.3 Clone (only once)

```powershell
cd C:\src        # or wherever you keep code
git clone https://github.com/edwinmeng163-oss/UEAtelier.git
cd UEAtelier
```

If you already have it, just `git fetch --tags origin`.

---

## 2. Per-version workflow

For each version that needs a Win zip, repeat sections 2.1 → 2.5.

### 2.1 Sync to the version tag

```powershell
git fetch --tags origin
git checkout v0.35.0          # replace with the version you're packaging
```

Verify:

```powershell
# Should print "0.35.0"
Select-String -Path Plugins\UnrealMcp\UnrealMcp.uplugin -Pattern '"VersionName"'
```

### 2.2 Clean any stale build artifacts

```powershell
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue Plugins\UnrealMcp\Binaries
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue Plugins\UnrealMcp\Intermediate
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue Examples\UEvolveExample57\Binaries
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue Examples\UEvolveExample57\Intermediate
```

This prevents an older dylib from shadowing fresh UBT output (the Mac side has hit this exact trap multiple times).

### 2.3 Build the plugin against the example host

For v0.35, build the shared example host with both primary engines. Start with
UE 5.7:

```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" `
    MyProjectEditor `
    Win64 `
    Development `
    -project="$(Resolve-Path .\Examples\UEvolveExample57\UEvolveExample57.uproject)" `
    -WaitMutex
```

Then remove the host/plugin binaries and intermediates as in § 2.2 and repeat
the command with the UE 5.8 `Build.bat` path. Both builds must end with
`Build successful`. A UE 5.6 build is an optional maintenance canary, not a
substitute for either primary build.

**Important:** the build target is `MyProjectEditor`, NOT `UEvolveExample57Editor`. The folder name is `UEvolveExample57` but the internal module + target name is `MyProject*` (this trips up first-time packagers).

After build, verify the dll exists:

```powershell
Get-Item Examples\UEvolveExample57\Binaries\Win64\UnrealEditor-UnrealMcp.dll
Get-Item Examples\UEvolveExample57\Binaries\Win64\UnrealEditor.modules
```

If you're packaging the **full-experience** zip (with prebuilt binaries bundled), you need both files in a directory that the script can scan — see section 2.5.

### 2.4 Run validators (sanity check before packaging)

```powershell
python Tools\validate_tool_registry.py
python Tools\check_ue56_compat.py
```

Expected output:
- `validate_tool_registry.py` ends with `issueCount: 0` and `toolCount: <NNN>` matching the version (190 for v0.35.0)
- `check_ue56_compat.py` ends with `0 errors, 0 warnings`

If either fails, **stop** and report back — the tag should have been clean.

### 2.5 Package the zip

There are two flavors:

#### Flavor A: source-only (recommended for parity with Mac releases)

This matches the current Mac zip — pure source, the Win user builds locally on first launch. **This is what we ship now.**

```powershell
pwsh -File Tools\package_plugin.ps1 -Version 0.35.0
```

Output:
```
Zip path: Saved\UnrealMcp\Packages\UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip
Zip size: ~1 MiB
SHA-256: <hash>
```

**Note** (script fix landed after v0.19.1): On clean checkouts the script automatically falls back from `Tools\UnrealMcpToolScaffolds\<id>` (the developer working dir, gitignored) to `Tools\UnrealMcpToolScaffoldStarters\<id>` (the canonical committed copy). No manual staging copy needed. If you're packaging from `v0.19.1` exactly, see §5 "Scaffold path fallback" for the workaround.

**Path separators**: The Win zip is produced by PowerShell's `Compress-Archive`, which uses `\` (backslash) entry paths. This is fine on Windows — Windows zip tools, Unreal's `IPlatformFile` extraction, `Expand-Archive`, and 7-Zip all handle it. PM's Mac-side `verify_package_integrity.py` now normalizes `\` → `/` during extraction so the same zip verifies cleanly cross-platform without needing to repackage. **Do not rewrite the zip to forward slashes** — the Win-tested artifact is the canonical Win release.

#### Flavor B: full-experience (with prebuilt Win64 binaries)

This bundles the freshly built `UnrealEditor-UnrealMcp.dll` so end-users don't need to compile. Use this only when explicitly requested — it makes the zip much larger and Mac users won't get a parallel artifact.

```powershell
pwsh -File Tools\package_plugin.ps1 `
    -Version 0.35.0 `
    -FullExperience `
    -PrebuiltBinariesPath (Resolve-Path .\Examples\UEvolveExample57) `
    -EngineTag ue57
```

For UE 5.8 binaries, change `-EngineTag ue58` and use a UE 5.8 build. UE 5.6
full-experience artifacts are maintenance-only and use `ue56`.

### 2.6 Verify the produced zip

```powershell
python Tools\verify_package_integrity.py `
    --zip Saved\UnrealMcp\Packages\UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip `
    --mode source `
    --strict
```

Expected: every check `[PASS]`, overall `Overall: pass (0 blocking, 0 warnings)`.

If you used `--full-experience`, change `--mode full-win`.

Note the sha256 — both the script output AND the `.sha256` sidecar should match.

---

## 3. Upload to the existing GitHub release

The Mac zip is already attached to each release. You add the Win zip + sha256 sidecar:

```powershell
gh release upload v0.35.0 `
    --repo edwinmeng163-oss/UEAtelier `
    Saved\UnrealMcp\Packages\UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip `
    Saved\UnrealMcp\Packages\UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip.sha256
```

Verify online:

```powershell
gh release view v0.35.0 --repo edwinmeng163-oss/UEAtelier
```

You should see 4 assets attached (2 Mac + 2 Win).

### 3.1 If the release already has a Win asset (re-upload)

`gh release upload` refuses to overwrite by default. If you need to replace:

```powershell
gh release upload v0.35.0 `
    --repo edwinmeng163-oss/UEAtelier `
    --clobber `
    Saved\UnrealMcp\Packages\UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip `
    Saved\UnrealMcp\Packages\UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip.sha256
```

### 3.2 Back-fill the CI-built Win zip from the workflow artifact (PM, from Mac)

When the `win-release-package.yml` CI ran but lost the timing race (see the caveat at the top — the Win zip is a workflow artifact, not on the release), **PM can attach it from a Mac without a Windows machine** — the CI already built + verified the zip on `windows-2022`:

```bash
# 1. Find the CI run for the tag (look for "Windows Release Package" / the tag name)
gh run list --repo edwinmeng163-oss/UEAtelier --limit 5

# 2. Download the artifact (name is UnrealMcp-<tag>-win-zip)
cd /tmp && rm -rf winbf && mkdir winbf && cd winbf
gh run download <run-id> --repo edwinmeng163-oss/UEAtelier --name "UnrealMcp-v0.35.0-win-zip"

# 3. Verify hash. The CI sidecar has CRLF line endings, so `shasum -c` FAILS spuriously —
#    strip CR and compare manually instead:
SIDE=$(tr -d '\r' < UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip.sha256 | awk '{print $1}')
COMP=$(shasum -a 256 UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip | awk '{print $1}')
[ "$SIDE" = "$COMP" ] && echo "MATCH" || echo "MISMATCH"

# 4. (Optional but recommended) extract-verify it's clean — CI builds from the committed
#    tag tree, so untracked working-tree cruft can't leak in (unlike the local Mac packager;
#    see Docs/BuildAndPackagingPitfalls.md § 9).

# 5. Attach to the release
gh release upload v0.35.0 --repo edwinmeng163-oss/UEAtelier --clobber \
  UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip \
  UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip.sha256
```

The CI-built Win zip is the canonical Windows artifact (backslash entry paths, built on `windows-2022`). Prefer back-filling it over a manual Windows rebuild — they produce the same shape, and the CI one is already verified.

---

## 4. Batch current release tags

For multiple v0.35 source-release tags in one manual fallback session:

```powershell
$versions = @("0.35.0") # Add later v0.35.x versions as needed.

foreach ($v in $versions) {
    Write-Host "=== Packaging v$v ===" -ForegroundColor Cyan

    git checkout "v$v"

    # Clean
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue Plugins\UnrealMcp\Binaries, Plugins\UnrealMcp\Intermediate
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue Examples\UEvolveExample57\Binaries, Examples\UEvolveExample57\Intermediate

    # Build (only needed for full-experience; skip for source-only)
    # & "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" `
    #     MyProjectEditor Win64 Development `
    #     -project="$(Resolve-Path .\Examples\UEvolveExample57\UEvolveExample57.uproject)" `
    #     -WaitMutex
    # if ($LASTEXITCODE -ne 0) { throw "Build failed for v$v" }

    # Validators
    python Tools\validate_tool_registry.py
    if ($LASTEXITCODE -ne 0) { throw "validate_tool_registry failed for v$v" }

    # Package source-only
    pwsh -File Tools\package_plugin.ps1 -Version $v
    if ($LASTEXITCODE -ne 0) { throw "package_plugin failed for v$v" }

    # Verify
    $zip = "Saved\UnrealMcp\Packages\UnrealMcp-v$v-win-ue57-ue58-projectroot.zip"
    python Tools\verify_package_integrity.py --zip $zip --mode source --strict
    if ($LASTEXITCODE -ne 0) { throw "verify failed for v$v" }

    # Upload
    gh release upload "v$v" --repo edwinmeng163-oss/UEAtelier $zip "$zip.sha256"

    Write-Host "=== v$v done ===" -ForegroundColor Green
}
```

Return to `main` when finished:

```powershell
git checkout main
```

---

## 5. Common pitfalls

### Scaffold path fallback (needed for v0.19.1 and older)

If you're packaging exactly `v0.19.1` (or any earlier tag) and hit:

```
Error: Missing directory: <repo>\Tools\UnrealMcpToolScaffolds\fps_bootstrap
```

stage the canonical starters into the working-copy location before packaging:

```powershell
New-Item -ItemType Directory -Force -Path Tools\UnrealMcpToolScaffolds | Out-Null
Copy-Item Tools\UnrealMcpToolScaffoldStarters\fps_bootstrap Tools\UnrealMcpToolScaffolds\fps_bootstrap -Recurse -Force
Copy-Item Tools\UnrealMcpToolScaffoldStarters\verify_input_drives_pawn Tools\UnrealMcpToolScaffolds\verify_input_drives_pawn -Recurse -Force
```

After packaging, remove `Tools\UnrealMcpToolScaffolds` to keep the worktree clean.

The script-side fallback landed on `main` after v0.19.1, so all future tags packaged from `main` won't need this step.



### Git checkout from worktree paths
Don't run `git checkout` from inside `.claude\worktrees\` — that's a developer artifact, not the canonical project root. Always run from the repository root (`C:\src\UEAtelier` or wherever you cloned).

### Long path issues
Windows default MAX_PATH is 260 characters. UE's Intermediate directory generates deep paths. If you hit `path too long` errors:

```powershell
# As Administrator, once per machine:
Set-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" -Name "LongPathsEnabled" -Value 1
# Then restart PowerShell.
```

Also ensure git long-paths support:
```powershell
git config --global core.longpaths true
```

### Encoding gotcha
The packaging script and validators expect UTF-8. If your PowerShell session has a non-UTF-8 default:

```powershell
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8
```

(Add to your PowerShell profile for permanent.)

### Git symlinks
The repo uses a Windows-compatible "Schemas mirror" pattern where some files appear as small stubs on Windows by default. The verifier checks this:

```
[PASS] schema_alias_not_stub: Package-critical real files are not Windows git-symlink stubs.
```

If this fails, run:

```powershell
git config core.symlinks true
git checkout -- Schemas/  Plugins/UnrealMcp/Resources/ToolRegistry/
```

(Requires Developer Mode or running git from elevated shell.)

### Build target name mismatch
`MyProjectEditor` is the canonical target — the folder is `UEvolveExample57` but the internal UE module is `MyProject`. If you try `UEvolveExample57Editor`, UBT will helpfully create a `.Target.cs` to satisfy your bad command, which is then noise to delete.

---

## 6. Reporting back to PM (Claude)

After uploading, send PM a short status:

- Versions packaged: `v0.X.Y`, `v0.A.B`, ...
- All zips uploaded to GitHub releases: yes / no (which failed and why)
- Any validator or build warnings to flag
- SHA-256 of each zip (so PM can cross-check)

PM updates `~/.hermes/memories/MEMORY.md` and any "Windows release status" lines in the release notes if needed.

---

## 7. References

- Mac counterpart script: `Tools/package_plugin.sh`
- Verifier: `Tools/verify_package_integrity.py`
- Registry validator: `Tools/validate_tool_registry.py`
- Engine compat validator: `Tools/check_ue56_compat.py`
- Install guide (end-user facing, lives inside the zip): `Tools/PackagingResources/INSTALL.md`
- Project conventions: `AGENTS.md`, `Tools/codex-prompt-header.md`

For questions or odd errors, ping PM (Claude) with the command you ran and the full error output — do not improvise unknown fixes on release artifacts.
