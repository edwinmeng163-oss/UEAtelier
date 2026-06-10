# UEAtelier Release Notes — v0.32.1 (2026-06)

> Trilingual: [中文](#中文) · [English](#english) · [日本語](#日本語)
> Patch release: Windows Codex CLI provider + dev-host automation baseline cleanup
> Previous release: [`Docs/Release-2026-06.md`](Release-2026-06.md) (v0.32.0)

---

## 中文

### 更新摘要

v0.32.1 是 v0.32.0 的平台覆盖补丁版,两项交付:

1. **Windows 端 Codex CLI provider 解锁**(自 v0.25 起的 macOS/Linux-only 限制移除)
2. **dev-host 测试基线清理**(纯测试/资产修复,零产品行为变更)

### 工具数

**190 不变**(canonical 与 plugin 镜像 byte-equivalent;本版无工具增删)。

### Windows Codex CLI provider

`Codex`(CLI subprocess)provider 现在在 Windows 上可用:

- **直接 spawn `codex.exe`,无 shell 中介**(不经 cmd.exe)。UE Windows `CreateProc` 自动给可执行路径加引号,参数串按 MSVC CRT / `CommandLineToArgvW` 引号规则构建。
- **配置校验**:`Codex Binary Path` 必须是不带引号的绝对 `.exe` 路径。npm 的 `.cmd`/`.bat` shim 与 `WindowsApps` 商店路径会被拒绝并给出可操作的错误提示。推荐用户级安装路径 `%LOCALAPPDATA%\OpenAI\Codex\bin\<hash>\codex.exe`;`where codex` 仅作诊断用途,其返回的 shim/WindowsApps 路径不可直接填入。
- **30000 字符命令行守卫**(CreateProcessW 32767 上限之下的保守阈值);超长单条消息在 spawn 前报错。ConversationContext 照旧走 stdin。
- Chat 面板的三处 Windows 禁用层(下拉灰显、`(Windows unsupported)` 后缀、选中拒绝)全部移除。
- POSIX(macOS/Linux)路径字节级不变,零回归(git-stash 对照验证)。
- 新增跨平台纯函数测试:CRT 引号矩阵、参数顺序、命令行长度、路径谓词、CRLF JSONL 容错——Mac automation 即可守护 Windows 命令构建。

> **注意**:Windows CLI provider 为本版新增,Mac 侧已全量验证;建议 Windows 用户首次使用时按 README 配置后自验(provider 可选、简单 prompt 回复、`.cmd`/WindowsApps 路径拒绝提示)。`CodexAppServer`(Codex Desktop bridge)仍是 Windows 上的成熟路径,继续可用。

### dev-host 测试基线清理(纯测试/资产)

- 补回 `user.editor_python_runtime_info` 的 `tool.json` manifest(v0.14 首个 python-track 工具,v0.26 manifest 硬要求后成为孤儿,曾导致 dev host 上所有 reload 类测试 `ReloadRejected`)
- GateD RAG 测试改用 `sourceRoot` 隔离 fixture,不再被宿主官方文档缓存淹没
- `ExecCommandActualBashExec` 根因修复:`ProjectIntermediateDir()` 返回相对路径,bash(cwd=项目根)无法解析 → 改绝对路径;失败时 stderr 与参数前缀进入测试输出
- 过期计数断言刷新(visible 169→178、force-dry-run 26→31、allow 61→65、core 181→190)
- `CountSeparation` 改为基线相对断言(`InitialUserCount + delta`):user-tools root 按 ProjectDir 解析,dev host(自带 1 个 committed 工具)与 example host(空根)均通过

### 验证

- UE 5.6 + 5.7 双引擎构建通过
- 双 host 全量 automation 收敛:仅剩 2 个已知历史失败(`RunRecoversStale`、`PieSmoke.MapValidation`)
- 5.7 example-host smoke(tools/list + editor_status)通过
- registry / 5.6-compat validators 全绿

### 校验(Verify)

```text
UnrealMcp-v0.32.1-mac-ue56-ue57-projectroot.zip
SHA-256: 9e07cffb52beaac570207594b3df3ff2a44d6222f50e697edb88b9b768dcbecf
UnrealMcp-v0.32.1-win-ue56-ue57-projectroot.zip
SHA-256: 见 GitHub Release 资产页(Win CI 产出)
```

---

## English

### Summary

v0.32.1 is a platform-coverage patch on top of v0.32.0 with two deliveries:

1. **Windows support for the Codex CLI provider** (removes the macOS/Linux-only restriction in place since v0.25)
2. **Dev-host automation baseline cleanup** (test/asset-only, zero product-behavior changes)

### Tool count

**190, unchanged** (canonical and plugin mirror remain byte-equivalent; no tools added or removed).

### Windows Codex CLI provider

The `Codex` (CLI subprocess) provider now works on Windows:

- **Spawns `codex.exe` directly with no shell intermediary** (no cmd.exe). UE's Windows `CreateProc` auto-quotes the executable path; the argument string follows MSVC CRT / `CommandLineToArgvW` quoting rules.
- **Config validation**: `Codex Binary Path` must be an unquoted absolute `.exe` path. npm `.cmd`/`.bat` shims and `WindowsApps` store paths are rejected with actionable errors. Prefer the user-mode install at `%LOCALAPPDATA%\OpenAI\Codex\bin\<hash>\codex.exe`; `where codex` is diagnostic only — its shim/WindowsApps results cannot be used directly.
- **30000-char command-line guard** (conservative threshold under the 32767 CreateProcessW cap); over-length single messages fail before spawn. ConversationContext still travels via stdin.
- All three Windows UI gates in the Chat panel (greyed dropdown entries, the `(Windows unsupported)` suffix, selection rejection) are removed.
- The POSIX (macOS/Linux) path is byte-identical — zero regression, proven by a git-stash contrast run.
- New cross-platform pure-function tests cover the CRT quoting matrix, argument order, command-line limit, path predicates, and CRLF JSONL tolerance — Mac automation now guards Windows command construction.

> **Note**: the Windows CLI provider is new in this release and fully verified on the Mac side; Windows users are encouraged to self-verify on first use (provider selectable, a simple prompt returns a reply, `.cmd`/WindowsApps paths produce the rejection guidance). `CodexAppServer` (the Codex Desktop bridge) remains the mature, fully supported path on Windows.

### Dev-host baseline cleanup (test/asset only)

- Restored the `tool.json` manifest for `user.editor_python_runtime_info` (v0.14's first python-track tool, orphaned by the v0.26 manifest requirement; it used to make every reload-dependent test return `ReloadRejected` on the dev host)
- The GateD RAG test now isolates its fixture via `sourceRoot`, immune to host official-docs caches
- `ExecCommandActualBashExec` root cause fixed: `ProjectIntermediateDir()` returns a relative path bash (cwd = project root) cannot resolve → absolute path; bash stderr and an args prefix now surface in failure output
- Stale count expectations refreshed (visible 169→178, force-dry-run 26→31, allow 61→65, core 181→190)
- `CountSeparation` is now baseline-relative (`InitialUserCount + delta`): the user-tools root resolves per ProjectDir, so the dev host (one committed tool) and example hosts (empty root) both pass

### Verification

- UE 5.6 + 5.7 dual-engine builds pass
- Full automation on both hosts converges to the 2 known historical failures only (`RunRecoversStale`, `PieSmoke.MapValidation`)
- 5.7 example-host smoke (tools/list + editor_status) passes
- Registry / 5.6-compat validators clean

### Verify

```text
UnrealMcp-v0.32.1-mac-ue56-ue57-projectroot.zip
SHA-256: 9e07cffb52beaac570207594b3df3ff2a44d6222f50e697edb88b9b768dcbecf
UnrealMcp-v0.32.1-win-ue56-ue57-projectroot.zip
SHA-256: see the GitHub Release asset page (produced by Win CI)
```

---

## 日本語

### 更新概要

v0.32.1 は v0.32.0 のプラットフォームカバレッジ・パッチで、2 つの内容を含みます:

1. **Windows 版 Codex CLI provider の解放**(v0.25 以来の macOS/Linux 限定を解除)
2. **dev ホストのテストベースライン整理**(テスト/アセットのみ、プロダクト挙動の変更ゼロ)

### ツール数

**190(変更なし)**(canonical とプラグインミラーは byte-equivalent;ツールの追加・削除なし)。

### Windows Codex CLI provider

`Codex`(CLI subprocess)provider が Windows で利用可能になりました:

- **`codex.exe` を直接 spawn、シェル中介なし**(cmd.exe を経由しない)。UE の Windows `CreateProc` が実行ファイルパスを自動でクォートし、引数文字列は MSVC CRT / `CommandLineToArgvW` のクォート規則に従います。
- **設定検証**:`Codex Binary Path` はクォートなしの絶対 `.exe` パス必須。npm の `.cmd`/`.bat` shim と `WindowsApps` ストアパスは、対処可能なエラーメッセージ付きで拒否されます。ユーザーモードインストール `%LOCALAPPDATA%\OpenAI\Codex\bin\<hash>\codex.exe` を推奨;`where codex` は診断用であり、返ってくる shim/WindowsApps パスはそのまま使えません。
- **30000 文字のコマンドライン上限ガード**(CreateProcessW の 32767 上限に対する保守的閾値);超過する単一メッセージは spawn 前にエラーになります。ConversationContext は従来どおり stdin 経由。
- Chat パネルの 3 箇所の Windows 無効化層(ドロップダウンのグレーアウト、`(Windows unsupported)` サフィックス、選択拒否)をすべて削除。
- POSIX(macOS/Linux)経路はバイト単位で不変 — git-stash 対照実験でゼロ回帰を証明。
- クロスプラットフォームの純関数テストを新設:CRT クォート行列、引数順序、コマンドライン長、パス述語、CRLF JSONL 耐性 — Mac の automation が Windows のコマンド構築を守ります。

> **注意**:Windows CLI provider は本バージョンの新機能で、Mac 側では全面検証済みです。Windows ユーザーは初回利用時に自己確認(provider が選択可能、簡単なプロンプトに返答が返る、`.cmd`/WindowsApps パスで拒否ガイダンスが出る)を推奨します。`CodexAppServer`(Codex Desktop bridge)は引き続き Windows での成熟した経路として利用可能です。

### dev ホストのベースライン整理(テスト/アセットのみ)

- `user.editor_python_runtime_info` の `tool.json` manifest を復元(v0.14 初の python-track ツール。v0.26 の manifest 必須化で孤児となり、dev ホストの reload 系テストをすべて `ReloadRejected` にしていました)
- GateD RAG テストは `sourceRoot` で fixture を隔離し、ホストの公式ドキュメントキャッシュに影響されなくなりました
- `ExecCommandActualBashExec` の根本原因を修正:`ProjectIntermediateDir()` が相対パスを返し、bash(cwd=プロジェクトルート)が解決できなかった → 絶対パス化;失敗時に bash stderr と引数プレフィックスがテスト出力に表示されます
- 古いカウント期待値を更新(visible 169→178、force-dry-run 26→31、allow 61→65、core 181→190)
- `CountSeparation` をベースライン相対断言(`InitialUserCount + delta`)に変更:user-tools root は ProjectDir ごとに解決されるため、dev ホスト(committed ツール 1 個)と example ホスト(空ルート)の両方で成立します

### 検証

- UE 5.6 + 5.7 デュアルエンジンビルド成功
- 両ホストのフル automation が既知の 2 失敗(`RunRecoversStale`、`PieSmoke.MapValidation`)のみに収束
- 5.7 example ホストの smoke(tools/list + editor_status)成功
- registry / 5.6-compat validator 全グリーン

### Verify

```text
UnrealMcp-v0.32.1-mac-ue56-ue57-projectroot.zip
SHA-256: 9e07cffb52beaac570207594b3df3ff2a44d6222f50e697edb88b9b768dcbecf
UnrealMcp-v0.32.1-win-ue56-ue57-projectroot.zip
SHA-256: GitHub Release アセットページ参照(Win CI 生成)
```

---

## Branch / Tag History

```text
main 2a1018d  v0.32.0 ship point
main b2c5ac1  provider(codex-cli): Windows support via direct codex.exe spawn
main e779259  test: dev-host automation baseline cleanup
tag  v0.32.1  (this release)
```

Branches `fix/win-codex-cli-provider` and `fix/dev-host-baseline-cleanup` were
developed independently off `2a1018d`, each verified standalone (stash-contrast
for POSIX zero-regression; dual-host targeted + full automation for the
cleanup), then fast-forward-integrated into `main` in that order with a
merged-state re-verification (build + 12 affected tests + dual-host full
automation) before tagging.
