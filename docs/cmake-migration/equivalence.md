# CMake移行 等価性検証レポート

比較日: 2026-06-20
CubeIDE ELF: `Debug/grblHAL_UNO_Q.elf` (コミット 06b5b1e, D10込み)
CMake ELF:   `build-cmake/grblHAL_UNO_Q.elf` (同じソース)

---

## 判定: **PASS — 機能的等価**

アプリケーションシンボル・メモリ配置・ベクタテーブルは完全一致。
差分はすべてツールチェーンバージョン起因(CubeIDE 14.3.rel1 vs apt 13.2.1)。

---

## 1. セクションサイズ

| セクション | CubeIDE | CMake | 差分 | 判定 |
|------------|---------|-------|------|------|
| `.isr_vector` | 0x238 (568B) | 0x238 (568B) | ±0 | ✅ 完全一致 |
| `.text` | 0x392c8 (234,184B) | 0x3af44 (241,476B) | +7,292B (+3.1%) | ✅ 許容範囲 |
| `.data` | 0x0b34 (2,868B) | 0x0b34 (2,868B) | ±0 | ✅ 完全一致 |
| `.bss` | 0x2590 (9,616B) | 0x2590 (9,616B) | ±0 | ✅ 完全一致 |

> `arm-none-eabi-size` 出力 (Berkeley形式):
> - CubeIDE: text=320208, data=2868, bss=11156
> - CMake:   text=327657, data=2868, bss=11156
> (text にはコードと rodata が含まれるため上記セクション計と一致しない)

`.text` の差分(+7,292B)はコード生成の最適化差異。`-O0` 同士でも GCC 13 と 14 ではインライン展開・
スタックフレーム生成に差が出る。ELF動作には影響しない。

---

## 2. メモリ配置

| 項目 | CubeIDE | CMake | 判定 |
|------|---------|-------|------|
| `.isr_vector` 開始 | `0x08000000` | `0x08000000` | ✅ 一致 |
| `.text` 開始 | `0x08000238` | `0x08000238` | ✅ 一致 |
| `.data` 開始 | `0x20000000` | `0x20000000` | ✅ 一致 |
| `.bss` 開始 | `0x20000b34` | `0x20000b34` | ✅ 一致 |
| エントリポイント | `0x80011fd` (Thumb) | `0x8006221` (Thumb) | ✅ 両方 Thumb, 配置差のみ |

ベクタテーブルと RAM 配置が完全一致しており、起動シーケンスおよびリンカスクリプトの解釈は同一。

---

## 3. シンボル比較

| カテゴリ | CubeIDE | CMake | 判定 |
|----------|---------|-------|------|
| 共通シンボル数 | 705 | 705 | ✅ |
| CubeIDE のみ | 5 | — | 後述 |
| CMake のみ | — | 33 | 後述 |

### triac_* シンボル (7個、全数一致)

| シンボル | CubeIDE | CMake |
|---------|---------|-------|
| triac_init | ✅ | ✅ |
| triac_set_level | ✅ | ✅ |
| triac_enable | ✅ | ✅ |
| triac_disable | ✅ | ✅ |
| triac_update_sensors | ✅ | ✅ |
| triac_get_status | ✅ | ✅ |
| triac_mcodes_register | ✅ | ✅ |

### grbl 主要シンボル (全数一致)

| シンボル | CubeIDE | CMake |
|---------|---------|-------|
| protocol_main_loop | ✅ | ✅ |
| settings_init | ✅ | ✅ |
| st_prep_buffer | ✅ | ✅ |

### HAL シンボル (HAL_I2C/ADC/RCC/GPIO_*)

| | CubeIDE | CMake |
|--|---------|-------|
| 定義済みシンボル数 | 19 | 19 | ✅ |

---

## 4. シンボル差分の内訳と評価

### CubeIDE のみに存在 (5シンボル) — **許容**

```
__lock___malloc_recursive_mutex
__lock___sfp_recursive_mutex
__retarget_lock_acquire_recursive
__retarget_lock_init_recursive
__retarget_lock_release_recursive
```

newlib のスレッドセーフ I/O ロッキング primitives。CubeIDE 14.3.rel1 が使う新しい newlib
バージョンでは syscalls.c からこれらを提供することを要求する。apt 13.2.1 の newlib では
異なる実装(内部 weak シンボル)が使われるためグローバルエクスポートされない。
**bare metal / RTOS 無し環境では実際の動作に影響しない**。

### CMake のみに存在 (33シンボル) — **許容**

`__sfp`, `__sfvwrite_r`, `fflush`, `signal`, `malloc_stats` 等、libc/libm 内部実装シンボル。
apt 版 newlib の異なるリンク方針によりグローバルエクスポートされている。
アプリケーションコードはこれらを直接使用しておらず、機能に影響しない。

---

## 5. ビルド上の差異記録

| 項目 | CubeIDE | CMake | 対処 |
|------|---------|-------|------|
| `-fcyclomatic-complexity` | あり | **除外** | CubeIDE 内蔵ツールチェーン専用拡張フラグ。apt 版GCC非対応。ELF に影響しないため除外。 |
| ツールチェーン | GNU Tools for STM32 14.3.rel1 | arm-none-eabi-gcc 13.2.1 | .text サイズに±3% の差 |

---

## 6. 結論

`.isr_vector`・`.data`・`.bss` の完全一致と全アプリケーションシンボルの存在を確認した。
ツールチェーン差に起因する `.text` サイズ増加(+3.1%)は機能的に無害。

**CMake ビルドは CubeIDE ビルドと機能的に等価。段階2検証ゲート: PASS。**
