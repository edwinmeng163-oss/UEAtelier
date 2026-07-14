# Unreal MCP Projectroot Install

## Source-Only Projectroot Zip (v0.35 development: UE 5.7 / 5.8)

### English

Use this mode for the v0.35 development candidates `UnrealMcp-v0.35.0-mac-ue57-ue58-projectroot.zip` and `UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip`.
Extract the source-only projectroot zip into your Unreal project root, next to `<YourProject>.uproject`; do not extract it under `Plugins/`.
It creates `Plugins/UnrealMcp/` (including `Plugins/UnrealMcp/INSTALL.md`), project-level `Tools/`, and `Docs/FIRST_LAUNCH.md`.
The package does not include prebuilt plugin binaries; Unreal Build Tool compiles UnrealMcp on first editor launch or explicit editor-target build.
For the 5-step first launch flow, read `Docs/FIRST_LAUNCH.md` after extraction.
UE 5.7 and UE 5.8 are the primary v0.35 targets. UE 5.6 remains a maintenance compatibility target and is intentionally absent from the candidate filename.

### 中文

此模式适用于 v0.35 开发候选包 `UnrealMcp-v0.35.0-mac-ue57-ue58-projectroot.zip` 和 `UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip`。
请把 source-only projectroot zip 解压到 Unreal 项目根目录，也就是 `<YourProject>.uproject` 旁边；不要解压到 `Plugins/` 目录下。
它会创建 `Plugins/UnrealMcp/`（包含 `Plugins/UnrealMcp/INSTALL.md`）、项目级 `Tools/` 和 `Docs/FIRST_LAUNCH.md`。
包内不包含预构建插件二进制；Unreal Build Tool 会在首次启动编辑器或显式构建 editor target 时编译 UnrealMcp。
解压后，请阅读 `Docs/FIRST_LAUNCH.md` 中的 5 步首次启动流程。
UE 5.7 和 UE 5.8 是 v0.35 的主要目标。UE 5.6 仅保留为维护兼容目标，因此不会出现在候选包文件名中。

### 日本語

この方式は v0.35 開発候補の `UnrealMcp-v0.35.0-mac-ue57-ue58-projectroot.zip` と `UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip` 用です。
source-only projectroot zip は `<YourProject>.uproject` と同じ Unreal プロジェクトルートへ展開してください。`Plugins/` の下へは展開しません。
展開すると `Plugins/UnrealMcp/`（`Plugins/UnrealMcp/INSTALL.md` を含む）、プロジェクトレベルの `Tools/`、`Docs/FIRST_LAUNCH.md` が作られます。
このパッケージに事前ビルド済みのプラグインバイナリは含まれません。Unreal Build Tool が初回エディタ起動時、または editor target の明示的なビルド時に UnrealMcp をコンパイルします。
展開後、初回起動の 5 ステップは `Docs/FIRST_LAUNCH.md` を参照してください。
UE 5.7 と UE 5.8 が v0.35 の主要ターゲットです。UE 5.6 は保守互換ターゲットとして残るため、候補パッケージ名には含めません。

## English

### Prerequisites

- Unreal Editor 5.7 or 5.8 on macOS or Windows for the primary v0.35 path. UE 5.6 is maintenance-only.
- macOS: Xcode 26.x, or another Xcode version compatible with your Unreal install.
- Windows: Visual Studio 2022 with the "Game Development with C++" workload, Windows 10/11 SDK, and .NET 6.0+ SDK.
- The built-in `PythonScriptPlugin` enabled in the project. Unreal Engine 5.x ships with this plugin.
- Close Unreal Editor before copying or replacing the plugin folder.

### Install Modes

This section is for the source-only `*-projectroot.zip` package. Source-only packages
are now project-root overlays: extract the zip into your Unreal project root,
next to `<YourProject>.uproject`; do not extract it under `Plugins/` or an
engine install. It creates:

```text
<UserProject>/Plugins/UnrealMcp/
<UserProject>/Tools/UnrealMcpPyTools/
<UserProject>/Tools/UnrealMcpToolScaffoldStarters/
<UserProject>/Tools/UnrealMcpToolScaffolds/
<UserProject>/Tools/UnrealMcpToolRegistry/
<UserProject>/Tools/unreal_mcp_fetch_docs.py
<UserProject>/Tools/install_unrealmcp_to_project.py
<UserProject>/Tools/UnrealMcpSkills/
<UserProject>/Tools/UnrealMcpKnowledge/
<UserProject>/Tools/UnrealMcpTests/
<UserProject>/Tools/UnrealMcpCodexBridge/
<UserProject>/Docs/FIRST_LAUNCH.md
```

This keeps Unreal MCP scoped to one project and ships the complete project-root
overlay that runtime tools expect: Python handlers, starter scaffold templates,
the writable registry and schema, skills, knowledge sources/evals, test fixtures,
Codex bridge source, and first-launch docs. Tools added through
`mcp_apply_scaffold` live in that project tree, which is easier to review, back
up, and remove. The source-only bridge tree intentionally excludes
`node_modules/` and `runtime/`; install Bun on the machine if you use the bridge.
The logical plugin, Tools, and Docs paths are the same on both OSes; the real
`<UserProject>` root differs by machine.

Engine plugin placement is advanced on both macOS and Windows and is not a direct
unzip target for this package:

```text
<UE Install>/Engine/Plugins/UnrealMcp/
```

If you manually copy the plugin there, also keep or copy the full project-root
`Tools/` overlay into each Unreal project root, because Python dispatch,
scaffold apply/import, skills, knowledge, tests, and bridge-source tools resolve
assets from `<ProjectDir>/Tools/...`.
Engine placement makes Unreal MCP available to every project using that engine
install. Self-extension writes to the engine plugin path, so it needs write
permission. Team-shared engine installs can also drift across projects when one
project adds tools. The logical engine plugin path is the same on both OSes; the
real `<UE Install>` root is under the platform-specific Unreal install directory.

If you build from the command line before first launch, UBT uses different entry scripts per OS.

macOS:

```bash
"<UE Install>/Engine/Build/BatchFiles/Mac/Build.sh" \
  YourProjectEditor Mac Development \
  -Project="/path/to/UserProject/UserProject.uproject" \
  -WaitMutex
```

Windows PowerShell:

```powershell
& "<UE Install>\Engine\Build\BatchFiles\Build.bat" `
  YourProjectEditor Win64 Development `
  "-Project=C:\Path\To\UserProject\UserProject.uproject" `
  -WaitMutex
```

### First Launch

On first editor launch, Unreal Build Tool compiles the plugin against your local Unreal binary. This can take about 30 seconds to 15 minutes depending on your build cache and machine.

### Source-Only Drop-in Notes

- Workbench `Run Core Tests`, `Run RAG Evals`, `unreal.knowledge_search`, `unreal.knowledge_eval_run`, and `unreal.mcp_run_test_suite` use the included `Tools/UnrealMcpTests/` and `Tools/UnrealMcpKnowledge/` trees.
- `unreal.mcp_apply_scaffold` and `unreal.tools.import_package` use the included writable `Tools/UnrealMcpToolRegistry/tools.json` and `schema.json` at the project root. Keep this registry with the rest of the `Tools/` overlay.
- `Tools/UnrealMcpCodexBridge/` is source-only in this package. It does not include `node_modules/` or a bundled runtime; install Bun on macOS/Windows yourself before using bridge scripts.
- Cross-developer tool transfer uses `unreal.tools.export_package` to create `Saved/UnrealMcp/Packages/*.zip`. Do not commit scaffold drafts as the transfer format.

### Current Package Scope

The v0.35.0 `ue57-ue58` files described here are development-candidate source-only projectroot overlays for macOS and Windows. UE 5.7 and UE 5.8 are the primary targets; UE 5.6 remains a maintenance compile canary. The registry has 190 registered MCP tools; AI-facing `tools/list` shows 178 visible tools. Linux is not a published package target.

This document does not announce a v0.35 public release. The latest public release remains v0.34.0. Before UE 5.8 is described as publicly supported, the candidate must pass real UE 5.8 UBT plus strict zip integrity, fresh-project install, endpoint smoke, and MCP SDK conformance. Windows UBT cells for both UE 5.7 and UE 5.8 are also required; the package-only GitHub workflow is artifact-shape evidence, not engine-compatibility evidence. See `Docs/Development-0.35.md` for the complete remaining release gates.

### Help

For the development contract, start with `Docs/Development-0.35.md`. For the latest public-release record, read `Docs/Release-2026-07b.md`, then `Plugins/UnrealMcp/README.md`. Historical release notes remain under `Docs/Release-*.md`. Report bugs at `https://github.com/edwinmeng163-oss/UEAtelier/issues`.

### Verify The Package

Run the matching check next to the zip and sidecar file.

Mac package:

```bash
shasum -a 256 -c UnrealMcp-v0.35.0-mac-ue57-ue58-projectroot.zip.sha256
```

Windows CI package:

```powershell
Get-FileHash -Algorithm SHA256 UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip
Get-Content UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip.sha256
```

Compare the `Get-FileHash` value with the hash in the `.sha256` sidecar.

## 中文

### 前提条件

- v0.35 主要路径需要 macOS 或 Windows 上的 Unreal Editor 5.7 或 5.8。UE 5.6 仅为维护目标。
- macOS：Xcode 26.x，或与你的 Unreal 安装兼容的其他 Xcode 版本。
- Windows：Visual Studio 2022，并安装 "Game Development with C++" 工作负载、Windows 10/11 SDK 和 .NET 6.0+ SDK。
- 项目中启用内置的 `PythonScriptPlugin`。Unreal Engine 5.x 自带这个插件。
- 复制或替换插件文件夹前，请先关闭 Unreal Editor。

### 安装模式

本节适用于 source-only 的 `*-projectroot.zip` 包。source-only 包现在也是项目根目录 overlay：
请解压到 Unreal 项目根目录，也就是 `<YourProject>.uproject` 旁边；不要解压到 `Plugins/` 或引擎安装目录下。它会创建：

```text
<UserProject>/Plugins/UnrealMcp/
<UserProject>/Tools/UnrealMcpPyTools/
<UserProject>/Tools/UnrealMcpToolScaffoldStarters/
<UserProject>/Tools/UnrealMcpToolScaffolds/
<UserProject>/Tools/UnrealMcpToolRegistry/
<UserProject>/Tools/unreal_mcp_fetch_docs.py
<UserProject>/Tools/install_unrealmcp_to_project.py
<UserProject>/Tools/UnrealMcpSkills/
<UserProject>/Tools/UnrealMcpKnowledge/
<UserProject>/Tools/UnrealMcpTests/
<UserProject>/Tools/UnrealMcpCodexBridge/
<UserProject>/Docs/FIRST_LAUNCH.md
```

这会把 Unreal MCP 限定在单个项目内，并随包提供运行时工具期望的完整项目根目录
overlay：Python handlers、scaffold starter 模板、可写 registry 和 schema、skills、
knowledge sources/evals、test fixtures、Codex bridge 源码以及首次启动文档。通过
`mcp_apply_scaffold` 添加的工具也会写入该项目目录，便于审查、备份和移除。
source-only bridge 目录会刻意排除 `node_modules/` 和 `runtime/`；如果要使用 bridge，
请在对应机器上自行安装 Bun。两个 OS 使用相同的逻辑插件、Tools 和 Docs 路径；实际的
`<UserProject>` 根目录取决于具体机器。

macOS 和 Windows 的引擎级插件位置都属于高级用法，而且这个包不能直接解压到该位置：

```text
<UE Install>/Engine/Plugins/UnrealMcp/
```

如果你手动把插件复制到这里，仍需要在每个 Unreal 项目根目录保留或复制完整的项目根目录
`Tools/` overlay，因为 Python dispatch、scaffold apply/import、skills、knowledge、
tests 和 bridge-source 工具都会从 `<ProjectDir>/Tools/...` 解析资源。
引擎级安装会让使用该引擎安装的所有项目都能使用 Unreal MCP。自扩展会写入引擎插件路径，因此需要写入权限。团队共享的引擎安装也可能因为某个项目添加工具而导致跨项目漂移。两个 OS 使用相同的逻辑引擎插件路径；实际的 `<UE Install>` 根目录位于对应平台的 Unreal 安装目录下。

如果你在首次启动前从命令行构建，UBT 在不同 OS 上使用不同入口脚本。

macOS：

```bash
"<UE Install>/Engine/Build/BatchFiles/Mac/Build.sh" \
  YourProjectEditor Mac Development \
  -Project="/path/to/UserProject/UserProject.uproject" \
  -WaitMutex
```

Windows PowerShell：

```powershell
& "<UE Install>\Engine\Build\BatchFiles\Build.bat" `
  YourProjectEditor Win64 Development `
  "-Project=C:\Path\To\UserProject\UserProject.uproject" `
  -WaitMutex
```

### 首次启动

首次启动编辑器时，Unreal Build Tool 会针对你的本地 Unreal 二进制编译插件。根据构建缓存和机器性能，这通常需要约 30 秒到 15 分钟。

### Source-Only Drop-in 说明

- Workbench 的 `Run Core Tests`、`Run RAG Evals`、`unreal.knowledge_search`、`unreal.knowledge_eval_run` 和 `unreal.mcp_run_test_suite` 会使用包内的 `Tools/UnrealMcpTests/` 与 `Tools/UnrealMcpKnowledge/` 目录。
- `unreal.mcp_apply_scaffold` 和 `unreal.tools.import_package` 会使用项目根目录下随包提供的可写 `Tools/UnrealMcpToolRegistry/tools.json` 与 `schema.json`。请让这个 registry 跟随其余 `Tools/` overlay 一起保留。
- 此包中的 `Tools/UnrealMcpCodexBridge/` 只包含源码，不包含 `node_modules/` 或捆绑 runtime；使用 bridge 脚本前请在 macOS/Windows 上自行安装 Bun。
- 跨开发者工具转移使用 `unreal.tools.export_package` 生成 `Saved/UnrealMcp/Packages/*.zip`。不要把 scaffold 草稿提交为转移格式。

### 当前包范围

这里描述的 v0.35.0 `ue57-ue58` 文件是面向 macOS 和 Windows 的开发候选 source-only projectroot overlay。UE 5.7 与 UE 5.8 是主要目标；UE 5.6 仅保留为维护编译 canary。registry 中有 190 个已注册 MCP tools；AI-facing `tools/list` 显示 178 个 visible tools。Linux 不是已发布的包目标。

本文档不代表 v0.35 已公开发布；最新公开版本仍是 v0.34.0。在公开宣称支持 UE 5.8 前，候选包必须通过真实 UE 5.8 UBT、严格 zip 完整性检查、全新项目安装、endpoint smoke 和 MCP SDK conformance。Windows 上 UE 5.7 与 UE 5.8 的 UBT cell 也必须通过；仅打包的 GitHub workflow 只能证明 artifact shape，不能证明引擎兼容性。完整剩余发布门请参阅 `Docs/Development-0.35.md`。

### 获取帮助

开发契约请先阅读 `Docs/Development-0.35.md`。最新公开发布记录见 `Docs/Release-2026-07b.md`，然后阅读 `Plugins/UnrealMcp/README.md`；历史发布记录仍保存在 `Docs/Release-*.md`。Bug 请在 `https://github.com/edwinmeng163-oss/UEAtelier/issues` 报告。

### 验证包

在 zip 和 sidecar 文件旁运行对应的检查。

Mac 包：

```bash
shasum -a 256 -c UnrealMcp-v0.35.0-mac-ue57-ue58-projectroot.zip.sha256
```

Windows CI 包：

```powershell
Get-FileHash -Algorithm SHA256 UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip
Get-Content UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip.sha256
```

将 `Get-FileHash` 的值与 `.sha256` sidecar 中的 hash 对比。

## 日本語

### 前提条件

- v0.35 の主要経路では、macOS または Windows 上の Unreal Editor 5.7 または 5.8 が必要です。UE 5.6 は保守専用です。
- macOS: Xcode 26.x、または利用中の Unreal インストールと互換性のある Xcode。
- Windows: Visual Studio 2022 に "Game Development with C++" ワークロード、Windows 10/11 SDK、.NET 6.0+ SDK をインストールしてください。
- プロジェクトで組み込みの `PythonScriptPlugin` を有効にしてください。Unreal Engine 5.x にはこのプラグインが同梱されています。
- プラグインフォルダをコピーまたは置き換える前に Unreal Editor を閉じてください。

### インストール方式

このセクションは source-only の `*-projectroot.zip` パッケージ向けです。
source-only パッケージもプロジェクトルート overlay になりました。zip は
`<YourProject>.uproject` と同じ Unreal プロジェクトルートへ展開してください。`Plugins/` やエンジンインストール配下へは展開しないでください。展開すると次が作られます。

```text
<UserProject>/Plugins/UnrealMcp/
<UserProject>/Tools/UnrealMcpPyTools/
<UserProject>/Tools/UnrealMcpToolScaffoldStarters/
<UserProject>/Tools/UnrealMcpToolScaffolds/
<UserProject>/Tools/UnrealMcpToolRegistry/
<UserProject>/Tools/unreal_mcp_fetch_docs.py
<UserProject>/Tools/install_unrealmcp_to_project.py
<UserProject>/Tools/UnrealMcpSkills/
<UserProject>/Tools/UnrealMcpKnowledge/
<UserProject>/Tools/UnrealMcpTests/
<UserProject>/Tools/UnrealMcpCodexBridge/
<UserProject>/Docs/FIRST_LAUNCH.md
```

この方式では Unreal MCP が 1 つのプロジェクトに限定され、runtime tools が期待する完全なプロジェクトルート
overlay も同梱されます。内容は Python handlers、scaffold starter テンプレート、書き込み可能な registry と
schema、skills、knowledge sources/evals、test fixtures、Codex bridge ソース、初回起動ドキュメントです。
`mcp_apply_scaffold` で追加したツールもそのプロジェクトツリー内に置かれるため、レビュー、バックアップ、削除がしやすくなります。
source-only bridge ツリーでは意図的に `node_modules/` と `runtime/` を除外しています。bridge を使う場合は、対象マシンで Bun を別途インストールしてください。
論理的なプラグイン、Tools、Docs のパスは両 OS で同じで、実際の `<UserProject>` ルートはマシンごとに異なります。

macOS と Windows のどちらでも、エンジンプラグインとしての配置は上級者向けで、このパッケージの直接の展開先ではありません。

```text
<UE Install>/Engine/Plugins/UnrealMcp/
```

手動でプラグインをここへコピーする場合も、各 Unreal プロジェクトルートに完全なプロジェクトルート
`Tools/` overlay を残すかコピーしてください。Python dispatch、scaffold apply/import、skills、knowledge、
tests、bridge-source tools は `<ProjectDir>/Tools/...` からリソースを解決します。
エンジン配置では、そのエンジンインストールを使うすべてのプロジェクトで Unreal MCP を利用できます。自拡張はエンジンプラグインのパスへ書き込むため、書き込み権限が必要です。チーム共有のエンジンインストールでは、あるプロジェクトで追加したツールが他のプロジェクトにも影響し、差分が広がることがあります。論理的なエンジンプラグインパスは両 OS で同じで、実際の `<UE Install>` ルートは各プラットフォーム固有の Unreal インストールディレクトリ配下です。

初回起動前にコマンドラインでビルドする場合、UBT は OS ごとに異なる入口スクリプトを使います。

macOS:

```bash
"<UE Install>/Engine/Build/BatchFiles/Mac/Build.sh" \
  YourProjectEditor Mac Development \
  -Project="/path/to/UserProject/UserProject.uproject" \
  -WaitMutex
```

Windows PowerShell:

```powershell
& "<UE Install>\Engine\Build\BatchFiles\Build.bat" `
  YourProjectEditor Win64 Development `
  "-Project=C:\Path\To\UserProject\UserProject.uproject" `
  -WaitMutex
```

### 初回起動

初回のエディタ起動時に、Unreal Build Tool がローカルの Unreal バイナリに合わせてプラグインをコンパイルします。ビルドキャッシュとマシン性能により、約 30 秒から 15 分かかります。

### Source-Only Drop-in の注意

- Workbench の `Run Core Tests`、`Run RAG Evals`、`unreal.knowledge_search`、`unreal.knowledge_eval_run`、`unreal.mcp_run_test_suite` は、同梱の `Tools/UnrealMcpTests/` と `Tools/UnrealMcpKnowledge/` ツリーを使用します。
- `unreal.mcp_apply_scaffold` と `unreal.tools.import_package` は、プロジェクトルートに同梱された書き込み可能な `Tools/UnrealMcpToolRegistry/tools.json` と `schema.json` を使用します。この registry は他の `Tools/` overlay と一緒に保持してください。
- このパッケージの `Tools/UnrealMcpCodexBridge/` は source-only です。`node_modules/` や同梱 runtime は含まれません。bridge scripts を使う前に macOS/Windows で Bun を別途インストールしてください。
- 開発者間のツール共有は、`unreal.tools.export_package` で `Saved/UnrealMcp/Packages/*.zip` を作成して行います。scaffold ドラフトを転送形式としてコミットしないでください。

### 現在のパッケージ範囲

ここで説明する v0.35.0 `ue57-ue58` ファイルは、macOS と Windows 向けの開発候補 source-only projectroot overlay です。UE 5.7 と UE 5.8 が主要ターゲットで、UE 5.6 は保守コンパイル canary として残ります。registry には 190 個の registered MCP tools があり、AI-facing `tools/list` には 178 個の visible tools が表示されます。Linux は公開パッケージ対象ではありません。

この文書は v0.35 の公開リリースを告知するものではありません。最新の公開リリースは引き続き v0.34.0 です。UE 5.8 を公開サポートと記載する前に、候補パッケージは実際の UE 5.8 UBT、厳密な zip 整合性確認、新規プロジェクトへのインストール、endpoint smoke、MCP SDK conformance を通過する必要があります。Windows の UE 5.7 と UE 5.8 の UBT cell も必須であり、パッケージ作成のみを行う GitHub workflow は artifact shape の証拠であって、エンジン互換性の証拠ではありません。残りの全リリースゲートは `Docs/Development-0.35.md` を参照してください。

### ヘルプ

開発契約は `Docs/Development-0.35.md` から確認してください。最新の公開リリース記録は `Docs/Release-2026-07b.md`、続いて `Plugins/UnrealMcp/README.md` を参照してください。過去のリリース記録は `Docs/Release-*.md` に残っています。バグは `https://github.com/edwinmeng163-oss/UEAtelier/issues` で報告してください。

### パッケージの検証

zip と sidecar ファイルの横で、該当する確認を実行してください。

Mac パッケージ:

```bash
shasum -a 256 -c UnrealMcp-v0.35.0-mac-ue57-ue58-projectroot.zip.sha256
```

Windows CI パッケージ:

```powershell
Get-FileHash -Algorithm SHA256 UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip
Get-Content UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip.sha256
```

`Get-FileHash` の値を `.sha256` sidecar 内の hash と比較してください。
