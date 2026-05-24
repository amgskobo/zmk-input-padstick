# zmk-input-padstick

[English](README.md)

絶対座標を報告するトラックパッドを、小さなジョイスティックのようなポインティング面として使うための ZMK input processor です。

`zmk,input-processor-padstick` は、`INPUT_BTN_TOUCH` 後に最初に届いた完全な絶対座標レポートフレームを捨て、その後の絶対座標レポートを `INPUT_REL_X` / `INPUT_REL_Y` に変換します。デフォルトでは、その次に届いた `INPUT_ABS_X` と `INPUT_ABS_Y` を一時的なタッチ原点として保存します。`fixed-center` を有効にした場合は、設定した `x-center` / `y-center` 座標を原点として使います。

## Features

- **原点ベースの移動**: タッチした位置を一時的なジョイスティック中心として扱います。
- **固定中心モード**: タッチ位置ではなく、設定したトラックパッド中心座標を使えます。
- **デッドゾーン**: タッチ直後の座標ぶれをポインタ移動にしません。
- **滑らかな加速**: 時間ではなく距離に応じて、低速用 scale から加速後 scale へ整数演算だけで滑らかに遷移します。
- **サブピクセル蓄積**: 軸ごとに小数相当の REL count を保持し、低感度でも小さな動きを捨てません。
- **イベント抑制**: 変換後に元の ABS、`BTN_TOUCH`、`BTN_0` を必要に応じて消費できます。
- **分割キーボード対応**: split build では central 側だけで有効になります。

## Installation

ZMK 設定リポジトリの `config/west.yml` に、このモジュールを追加してください。

```yaml
manifest:
  remotes:
    - name: amgskobo
      url-base: https://github.com/amgskobo
  projects:
    - name: zmk-input-padstick
      remote: amgskobo
      revision: main
```

## Quick Start

### 1. DTS Include

シールドの `.overlay` または `.zmk.dts` で標準ヘルパーを include します。

```dts
#include <zmk-input-padstick/input_processor_padstick.dtsi>
```

### 2. Configuration Example

**Note**: DeviceTree で compatible が有効になると、`CONFIG_ZMK_POINTING` が有効な構成では Kconfig default により `CONFIG_ZMK_INPUT_PROCESSOR_PADSTICK` も自動的に有効になります。

```dts
/* padstick processor の設定 */
&padstick {
    x-deadzone = <48>;
    y-deadzone = <48>;
    x-scale = <8>;
    y-scale = <8>;
    x-accel-range = <464>;
    y-accel-range = <464>;
    x-accel-scale = <12>;
    y-accel-scale = <12>;
    max-x = <16>;
    max-y = <16>;
    fixed-center;
    x-center = <512>;
    y-center = <512>;
    suppress-abs;
    suppress-btn-touch;
};

/* トラックパッドの入力パイプラインへ padstick を追加 */
&trackpad_listener {
    input-processors = <&padstick>;
};
```

デフォルト値は 1024 x 1024 の絶対座標トラックパッドを基準にしています。48 count の deadzone でタッチ原点周辺に小さな遊びを作り、中心から端までの残り約 464 count を滑らかな加速範囲として使います。

### 3. Motion Semantics

- `BTN_TOUCH` press でタッチ原点とサブピクセル remainder をリセットします。
- touch 後の最初の完全な `ABS_X` / `ABS_Y` フレームは原点安定用として抑制します。
- デフォルトのタッチ原点モードでは、その次の `ABS_X` と `ABS_Y` が原点座標になります。
- `fixed-center` では、毎回 `x-center` と `y-center` が原点座標になります。
- `x-deadzone` / `y-deadzone` 内の動きは 0 を出力し、その軸の remainder をクリアします。
- deadzone 外の動きは fixed point scale で変換します。`256` が `1.0x` です。
- 加速は `x-scale` / `y-scale` から `x-accel-scale` / `y-accel-scale` へ、`x-accel-range` / `y-accel-range` の距離内で滑らかに増えます。
- 小数相当の出力は軸ごとに蓄積されます。たとえば `x-scale = <8>` の場合、deadzone 外の 1 count 入力が繰り返されると、32 回に 1 回 `REL_X = 1` が出ます。
- 出力は `max-x` / `max-y` で clamp されます。飽和した場合、その軸の remainder はクリアされます。

## Debug Logging

Zephyr logging を有効にし、ZMK の log level を debug にすると、この processor の `LOG_DBG` 出力を有効にできます。たとえば ZMK 設定で `CONFIG_LOG=y` と `CONFIG_ZMK_LOG_LEVEL_DBG=y` を設定します。ログを見るには、USB logging、RTT、UART など、ZMK 側のログ出力 backend も必要です。USB CDC ACM logging を使う場合は `CONFIG_ZMK_USB_LOGGING=y` を使えます。

debug log には、touch reset、保存された原点座標、raw ABS 座標、原点からの delta、deadzone 後の magnitude、fixed-point scaled 値、入力/出力 remainder、生成された REL 出力、飽和、抑制された入力イベントが含まれます。

## Configuration Reference

| Property | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `x-deadzone` | int | 48 | タッチ原点周辺で `REL_X = 0` とする X 絶対座標 count。 |
| `y-deadzone` | int | 48 | タッチ原点周辺で `REL_Y = 0` とする Y 絶対座標 count。 |
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
| `suppress-abs` | bool | false | 変換されなかった ABS event を消費し、後段へ流さないようにします。 |
| `suppress-btn-touch` | bool | false | contact state として使った後の `BTN_TOUCH` event を消費します。 |
| `suppress-btn0` | bool | false | トラックパッドが物理クリックとして報告する `BTN_0` event を消費します。 |

scale と accel scale は runtime で `0..4096` に clamp されます。deadzone と max は負にならないように補正されます。

## License

MIT License. 詳細は [LICENSE](LICENSE) を参照してください。
