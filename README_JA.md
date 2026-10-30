# zmk-input-padstick

[![Test](https://github.com/amgskobo/zmk-input-padstick/actions/workflows/test.yml/badge.svg)](https://github.com/amgskobo/zmk-input-padstick/actions/workflows/test.yml)

[English](README.md)

絶対座標を報告するトラックパッドを、小さなジョイスティックのようなポインティング面として使うための ZMK input processor です。
導入・DTSの構成例は[英語ガイド](README.md#quick-start)を参照してください。

## 接触と出力の責務

元のボタンイベントは type/code/value/sync を保って後段へ渡します。
独立した `zmk-input-suppress` を全経路の末尾に配置し、抑止方針、
押下と解放の履歴、同期境界を一か所で管理します。
旧 `suppress-abs` / `suppress-btn-touch` / `suppress-btn0` は削除します。
ボタンの履歴と解放義務は共通最終段が所有し、レイヤー変更でも維持します。
PadStick は原点・端数・repeat の中止を所有します。
入力キューに X だけが入った場合は、不足する同期付き Y だけを再送し、
その組が完了するまで次の X を追加しません。指の解放・レイヤー変更・設定変更は
以後の repeat を止めますが、受理済み X のフレーム終端は完了させます。
初期化用座標は sync を保つ中立 MSC_SCAN になります。
標準includeの `input_suppress` を経路末尾から直接mask引数で指定します。

## 座標演算とリピート

数値計算は非公開の `src/padstick_math.h`、接触・原点・繰り返し・レイヤー変更時の
停止はドライバーが担当します。原点からの差分は減算前に範囲を検査し、方向への
配分は商と余りへ分けてから乗算します。製品コードは64ビット演算を使わず、
既存の入力範囲、丸め、端数、飽和を維持します。テストの独立した参照計算には
64ビット整数を残します。

保持方向の repeat は、保存済みの X/Y 座標対から距離を一度だけ計算し、両軸で共有します。
平方根の結果を保存してから上限を適用するため、マクロによる二重評価もありません。
各軸のデッドゾーン・倍率・反転・上限・端数は独立したままです。通常の ABS イベントは、
従来どおり、その時点で最後に届いた相手軸の座標を使います。

## 複数input listener

1つのpadstick processor nodeを複数のinput listenerで共有できます。contact座標、
origin、小数remainder、保持方向のrepeat work、元のinput device は
`input_device_index`ごとに独立します。設定値はnode全体で共有します。不正なruntime
indexはログを出さずに変換せず通過し、stream 0へaliasしません。

## Debug Logging

`CONFIG_LOG=y` でZephyr loggingを有効にし、
`CONFIG_ZMK_INPUT_PADSTICK_LOG_LEVEL=4` でこのmoduleの `LOG_DBG` 出力を含めます。
このmoduleのログは専用のlog levelで制御します。
ログを見るには、USB logging、RTT、UARTなどの出力backendも構成してください。

debug log は初期化と継続接触の推定を記録し、繰り返し出力の入力キュー投入失敗は警告します。算術ヘルパーはログやカーネルに依存しません。

## Configuration Reference

| Property | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `x-deadzone` | int | 48 | 原点からの移動量のうち 0 を出力する count。距離は半径で測るため、両デッドゾーンは同じ円を区切ります。 |
| `y-deadzone` | int | 48 | `x-deadzone` を参照してください。 |
| `x-scale` | int | 8 | deadzone 外の X 低速移動 scale。`256` が `1.0x`。 |
| `y-scale` | int | 8 | deadzone 外の Y 低速移動 scale。`256` が `1.0x`。 |
| `x-accel-range` | int | 464 | `x-scale` から `x-accel-scale` へ滑らかに加速する X 距離 count。 |
| `y-accel-range` | int | 464 | `y-scale` から `y-accel-scale` へ滑らかに加速する Y 距離 count。 |
| `x-accel-scale` | int | 12 | `x-accel-range` 末端で到達する X scale。`256` が `1.0x`。 |
| `y-accel-scale` | int | 12 | `y-accel-range` 末端で到達する Y scale。`256` が `1.0x`。 |
| `max-x` | int | 16 | この processor が出力する `REL_X` の絶対値上限。 |
| `max-y` | int | 16 | この processor が出力する `REL_Y` の絶対値上限。 |
| `x-center` | int | 512 | `fixed-center` が有効なときに使う X 固定中心座標。 |
| `y-center` | int | 512 | `fixed-center` が有効なときに使う Y 固定中心座標。 |
| `invert-x` | bool | false | 生成する `REL_X` の方向を反転します。 |
| `invert-y` | bool | false | 生成する `REL_Y` の方向を反転します。 |
| `fixed-center` | bool | false | タッチ位置ではなく `x-center` と `y-center` をジョイスティック原点として使います。 |

scale と accel scale は runtime で `0..4096` に clamp されます。deadzone と max は負にならないように補正されます。

`INT32_MIN` は内部の未設定座標として予約されているため、X・Y の中心には使えません。
この値は DTS のビルド時とデバイス初期化時にも、実行中の設定 API と同様に拒否されます。
`INT32_MIN+1` から `INT32_MAX` までの中心座標は使用できます。

## DYA Studio設定

`CONFIG_ZMK_INPUT_PADSTICK_CUSTOM_SETTINGS=y` をCustom Settings・Studio RPCと共に
有効にすると、`amgskobo__padstick` に各ノード15項目を公開します。デッドゾーン、
倍率、加速範囲、最大出力、中心、反転、固定中心を変更でき、保存は共通レジストリが担当します。
変更時はリピートを止めて原点・端数を取り直し、キューに入った1組の処理は維持します。
公開キー・範囲とAPIは[英語ガイド](README.md#dya-studio-settings)を参照してください。

設定グループID `amgskobo__padstick` は保存先の識別子も兼ねます。旧ローカル版の
`amgskobo__stick` からの自動移行は行いません。値を再設定するか、エクスポートした
データのグループIDを更新してからインポートしてください。
既存の保存領域は消去しません。キーと保存先全体の長さは終端文字も含めてビルド時に検査します。

## テスト

```sh
bash ./tests/run-docker.sh
bash ./tests/run-integration-docker.sh upstream
bash ./tests/run-integration-docker.sh dya
```

ホストテストは実際の非公開算術ヘッダーとドライバー全体を別々に取り込み、全ケースをASan/UBSanで実行します。fullモードでは最適化版と関数・行・分岐の完全な計測も実行します。テスト用の構造体複製や関数抽出は行いません。算術6関数とドライバー12関数をそれぞれ独立に確認します。

各variantで、2つのinput listenerが1つのpadstick nodeを共有するファームウェアfixtureをビルドし、
そのZMKでnative_simのself-testを実行します。範囲外のlistener indexが変更されずに通過すること、
2つのlistenerが互いの接触状態を上書きしないこと、元のボタンがlistenerごとに透過すること、
レイヤ変更で原点が破棄されることを確認します。`upstream` はZMK `main`、`dya` はDYA ZMK forkを
使います。GitHub Actions は pull request ごと、`main` への push、週次実行で両方を実行します。

nativeテストは実input queueへ送る繰返しペア、制御された確認応答、接触終了・待機中レイヤー変更も検証し、同じバイナリを3回実行します。
実HID・無線の混雑試験とは分けています。詳細は [テストガイド](tests/README.md) を参照してください。

## License

MIT License. 詳細は [LICENSE](LICENSE) を参照してください。
