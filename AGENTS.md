# SRVB CLAP migration guide

## 目的

このリポジトリは、JUCEを土台にしたSRVBプラグインテンプレートを、
`CLAP + clap-wrapper + CHOC + React` 構成へ移行する途中にある。

最終的な構成は次を目指す。

- プラグインAPI: CLAP
- VST3出力: clap-wrapperのclap-first方式
- DSPと状態管理: JUCE非依存の`DspEngine`
- WebView: CHOC
- UI: React/Vite
- DSP記述: Elementary Audio
- JUCE依存: 最終段階で撤去

## 合意済みの方針

- このリポジトリはプラグインテンプレートであり、既存JUCE版の挙動を完全に再現する必要はない。
- 移行前の振る舞いを記録する互換性調査は行わない。
- Elementary Audioへのパラメータ反映方法は、現行JUCE版と同じメインスレッド経由の方式を維持する。
- サンプル精度のオートメーションやスレッド間通信の再設計は、この移行には含めず別チケットとする。
- DSPを実際に動かす専用テストは追加しない。
- その他のテスト追加も原則として行わない。必要な確認は既存コマンドによるビルド確認を中心とする。
- 実装はCLAP公式チュートリアルと`novonotes/wrac-plugin-template`を参考にし、可能な限り簡潔に保つ。
- CMake上のJUCE非依存DSPターゲット名は`dsp_engine`とする。
- 各移行段階が終わるたびに作業を止め、ユーザーへレビューを依頼する。レビュー前に次段階へ進まない。
- ユーザーから明示的に依頼されない限り、作業中の変更をcommitしない。

## 現在の状態

基準日時は2026-09-13。

### 完了: DSPと状態管理のJUCE分離

- `native/DspEngine.h`と`native/DspEngine.cpp`にElementary Runtime、QuickJS、JSON状態管理を分離した。
- CMakeターゲット`dsp_engine`を追加した。
- 既存JUCE版は`DspEngine`を所有して利用する構成へ変更した。
- この段階はcommit済み。

### 完了: GUIなしCLAP版

- `native/ClapPlugin.cpp`にCLAP entry/factoryと最小限のプラグイン実装を追加した。
- `native/ClapEntry.cpp`に共有entry pointを分離した。
- ステレオfloat32入出力、Size/Decay/Mod/Mixパラメータ、JSON state、latency、tailを公開している。
- DSP資産として`public/dsp.main.js`をプラグインbundleへ同梱する。
- パラメータイベントは既存挙動に合わせ、メインスレッドでElementaryへ反映する。
- CLAP SDKは`native/clap` submoduleとして追加済み。
- この段階は実装・ビルド・ユーザーレビュー済み。

### 先行実装済み: clap-wrapperによるVST3出力

- `native/clap-wrapper` submoduleを追加した。
- `native/cmake/Clap.cmake`でclap-wrapperのclap-first targetを構成した。
- CLAP実装を静的リンクした自己完結型VST3を生成する。実行時に`SRVB.clap`を必要としない。
- `pnpm run build-clap`でCLAPとVST3の両方を生成する。
- macOSでCLAP/VST3 bundleをad-hoc codesignする。
- ビルド成功を確認済み。成果物は次に出力される。
  - `native/build/clap/SRVB_artefacts/Release/SRVB.clap`
  - `native/build/clap/SRVB_artefacts/Release/SRVB.vst3`
- 実DAWでのVST3読み込みと音声処理はユーザーレビュー待ち。
- clap-wrapper接続は先行しているが、JUCEをまだ撤去していないため最終段階全体は未完了。

### 完了: WebViewと開発環境の移植

- `native/ClapEditor.h`と`native/ClapEditor.cpp`でCHOC WebViewをCLAP GUI extensionへ接続した。
- release buildでは`dist`のReact UIとDSP bundleをプラグインへ同梱する。
- development buildではVite dev serverを表示し、DSPのhot reloadをWebView経由でElementaryへ反映する。
- UIからのパラメータ変更はCLAP params flushでホストへ通知する。
- macOS向けRelease/DebugのCLAP/VST3ビルドは確認済み。
- 実DAWでの表示・操作を確認し、ユーザーレビュー済み。

### 未着手: JUCE撤去と最終整理

- CHOC WebView移植をレビュー後、JUCE版targetとJUCE固有コードを撤去する。
- JUCE submodule、JUCE専用CMake設定、不要になったソースとドキュメントを整理する。
- CLAPとclap-wrapper VST3を正式な標準ビルドにする。
- 最終的なビルド確認後、ユーザーへレビューを依頼する。

## 移行タスク

当初の6段階を、合意内容と現在の進捗に合わせて管理する。

1. 現行動作の記録と互換性決定: スキップ。
2. JUCEからDSPと状態管理を切り出す: 完了、commit済み。
3. GUIなしのCLAP版を成立させる: 完了、commit済み。
4. オートメーションとスレッド間通信を固める: 対象外、別チケット。
5. WebViewと開発環境をCHOC + Reactへ移植する: 完了。
6. clap-wrapperを接続しJUCEを撤去する: VST3出力のみ先行実装済み。JUCE撤去は未着手。

次に行う作業は、段階6のJUCE撤去と最終整理である。

## ビルドコマンド

依存submoduleを取得したcloneを前提とする。

```bash
pnpm install
pnpm run build-clap
```

`build`がDSP、React UI、nativeの順にビルドする。`build-clap`は`build`の完了後、JUCEを無効にして
CLAPとclap-wrapper VST3をRelease buildする。

CLAP版のdevelopment buildとVite serverは次で起動する。

```bash
pnpm run dev-clap
```

`dev-clap-native`はnativeのみビルドする。開発版にはDSPを同梱せず、UI起動時とDSP更新時に
Viteから`dsp.main.js`を取得してWebView経由で渡す。開発版の初回DSP読み込みにはUIを開く必要がある。

既存JUCE版の通常ビルドは移行完了まで残している。

```bash
pnpm run build
```

## 現在認識している制約

- CLAP版GUIは800x704固定サイズで、floating windowとresizeには対応しない。
- パラメータ更新はサンプル精度ではない。
- offline rendering時もメインスレッドのcallback schedulingに依存する。
- `reset`はElementary Runtimeの既存resetへ委譲しており、delay/tapのresetには既知の制約がある。
- tailは安全側に倒してinfiniteとして報告している。
- 上記はホストadapter移行とは分離し、このタスクでは再設計しない。

## 作業上の注意

- VST3再配置後はAudioPluginHostの「Scan for new or updated VST3 plug-ins」を実行する。登録キャッシュを直接編集しない。
- 2026-09-13の比較確認では、ready遅延を除去、Viteのscript変換を除去、IDを元の`audio.elementary.srvb`へ復帰、の順で各版を再配置・再スキャンして正常描画を確認した。これらの対策は撤去済み。
- 元のCLAP ID由来のVST3 CIDは`91F6CB342BF95DD19A93D537288CC490`で、旧JUCE版のCIDとは異なる。以前の「ID衝突が白画面原因」という断定は撤回する。白画面自体は比較中に再現しておらず、根本原因は未確定。

- 新しいテスト基盤やDSPテストを追加しないこと。
- VST3をユーザーのプラグインフォルダへ再配置するときは、既存の`SRVB.vst3`を削除してからコピーする。旧bundleは一時退避しない。
- 段階をまたぐ大きな変更を一度に行わないこと。
- 各段階の完了時には、変更内容、ビルド結果、未確認事項を簡潔に示してユーザーへレビューを依頼すること。
