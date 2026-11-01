# LiteRTOS

[English](README.md) | [繁體中文](README.zh-TW.md)

一個給 Arduino Uno（ATmega328P）用的小型協作式 RTOS，函式名稱與 FreeRTOS 相同。

## 為什麼做這個

Arduino Uno 只有 2 KB RAM、32 KB flash。我發現在 Uno 上使用 FreeRTOS 時，我能塞進去的程式碼不多，很大一部分空間在程式開始之前就被 FreeRTOS 佔滿了：每個 task 一個 46 byte 的 TCB、帶自己 stack 的 idle task、heap，以及為搶佔式排程和許多選配功能設計的核心。留給真正程式的空間所剩不多。

所以我設計了 LiteRTOS：給已經會用 FreeRTOS 的人的一個更小的核心。API 名稱與 task 的寫法（`for (;;)`、`vTaskDelay`、`xTaskDelayUntil`）都保持一樣，但拿掉小型 AVR 專案很少用到的東西。

這個專案的第二個目的是一項實驗：**在現代編譯環境（avr-gcc、LTO、`-Os`）下，用組合語言取代部分 C，對韌體是否還有更好的效益？** 幾個小而常被執行的函式改用 AVR 組合語言寫，並和 C 版本比較。見[組合語言實驗](#組合語言實驗)。

## 功能特色

* API 名稱與型別和 FreeRTOS 相同（`xTaskCreateStatic`、`vTaskDelay`、`xTaskDelayUntil`、`vTaskSuspend`、`vTaskResume`、`vTaskDelete` 等）
* 協作式排程，等同 FreeRTOS 的 `configUSE_PREEMPTION = 0`
* 支援優先權，同優先權的 task 輪流執行
* 只用靜態配置：沒有 heap、沒有 `malloc`
* TCB 只有 18 byte（FreeRTOS 在 AVR 上的 `StaticTask_t` 是 46 byte）
* 1 ms 的 tick 取自 Arduino 的 `millis()`，不佔用任何 timer
* context switch 只保存 avr-gcc ABI 要求的暫存器（r2–r17、r28、r29、SREG）
* `uxTaskGetStackHighWaterMark` 可用來決定 stack 大小
* `portENTER_CRITICAL` / `portEXIT_CRITICAL` 為行內組合語言巨集
* 不提供的 API 會在編譯期報錯並說明原因，而不是默默什麼都不做
* 以真正的 AVR 機器碼在 simavr 上測試，一般與 LTO 兩種編譯都跑

## 狀態

**版本：v0.1.0（早期）**

20 項 simavr 測試在一般與 LTO 編譯下全部通過。已在真正的 Arduino Uno 上驗證。下面組合語言實驗中的時間數字是在模擬器上量的。見[限制](#限制)。

---

## 基本用法

```cpp
#include <LiteRTOS.h>

static StaticTask_t blinkTcb;
static StackType_t  blinkStack[96];

void blinkTask(void *arg) {
    (void)arg;
    pinMode(LED_BUILTIN, OUTPUT);
    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(500));
    }
}

void setup() {
    xTaskCreateStatic(blinkTask, "blink", sizeof(blinkStack), NULL,
                      tskIDLE_PRIORITY + 1, blinkStack, &blinkTcb);
    vTaskStartScheduler();   // 不會返回
}

void loop() {}
```

完整的雙 task 範例在 [`examples/basic`](examples/basic/basic.ino)。

注意：

* stack 大小的單位是 **byte**（`StackType_t` 是 `uint8_t`）。
* 排程是協作式的。task 必須呼叫 `vTaskDelay`、`xTaskDelayUntil` 或 `taskYIELD`，否則其他 task 永遠不會執行。不要在 task 裡呼叫 Arduino 的 `delay()`，它會讓所有 task 一起等。
* 預設最多 4 個 task（`LITERTOS_MAX_TASKS`）。

---

## API 總覽

| 函式 | 說明 |
| ---- | ---- |
| `xTaskCreateStatic(...)` | 用呼叫者提供的 stack 與 TCB 緩衝區建立 task |
| `vTaskStartScheduler()` | 開始排程，不會返回 |
| `vTaskDelay(ticks)` | 阻塞指定的 tick 數 |
| `xTaskDelayUntil(&last, inc)` | 阻塞到距上次喚醒固定週期後，不會累積誤差 |
| `taskYIELD()` | 讓同等或更高優先權的就緒 task 執行 |
| `vTaskSuspend(task)` / `vTaskResume(task)` | 暫停或恢復 task（suspend 傳 `NULL` 代表目前的 task） |
| `vTaskDelete(task)` | 移除 task（`NULL` 代表目前的 task） |
| `xTaskGetTickCount()` | 目前的 tick 計數 |
| `xTaskGetCurrentTaskHandle()` | 執行中 task 的 handle |
| `uxTaskGetStackHighWaterMark(task)` | 到目前為止 stack 剩餘的最小 byte 數（`NULL` 代表目前的 task） |
| `taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()` | 臨界區，函式版 |
| `portENTER_CRITICAL()` / `portEXIT_CRITICAL()` | 臨界區，行內組合語言版；必須在同一個函式內成對使用，中間不能呼叫會阻塞的函式 |

不提供（編譯期錯誤）：`xTaskCreate`、`vTaskDelayUntil`、`vTaskPrioritySet`、`uxTaskPriorityGet`。佇列、semaphore、mutex、軟體 timer、event group 目前都不在 LiteRTOS 的範圍內。

---

## 資源比較

兩個核心用同一個測試程式（4 個 task）、相同編譯參數（`-Os`、LTO）建置，在 simavr 上量測。FreeRTOS 是 Arduino_FreeRTOS_Library，設定成盡可能小（協作式、靜態配置、無 timer、無 mutex）。數字是整個程式的總量，包含 Arduino core。

| | LiteRTOS | FreeRTOS（最小設定） |
| --- | ---: | ---: |
| Flash | 5432 B | 7766 B |
| 靜態 RAM（data + bss） | 1386 B | 1758 B |
| 每個 task 的 TCB | 18 B | 46 B |
| tick 解析度 | 1 ms | 16 ms（watchdog tick） |

LiteRTOS 在 flash、RAM 與 TCB 都比較小。FreeRTOS 的預設（非最小）設定更大，`test/bench/report.py` 可以重現包含預設設定的表格。

重現方式：

```bash
python test/bench/report.py          # 用 PlatformIO 編譯，印出 Flash/RAM 比較表
pio run -e bench -t upload           # LiteRTOS 速度測試（序列埠 115200）
pio run -e bench_fr -t upload        # FreeRTOS 速度測試，量法相同
```

---

## 組合語言實驗

兩個函式改用 AVR 組合語言寫，其餘維持 C：

| 檔案 | 取代 | 用途 |
| ---- | ---- | ---- |
| `src/PortInitStack.S` | C 版 `LiteRTOS_portInitStack` | 建立新 task 的 21 byte 初始 context |
| `src/uxTaskGetStackHighWaterMark.S` | `uxTaskGetStackHighWaterMark` 裡的 C 迴圈 | 計算 stack 中沒被動過的 byte 數 |

context switch 本身與臨界區巨集本來就是行內組合語言。

目前的結果（模擬器）：組合語言版的切換速度**與 C 版完全相同**，flash 比 C 版少約 **34 B**，RAM 沒有變化。這兩個函式不在切換路徑上，所以本來就不預期有太大差異。更有意思的問題是：組合語言用在熱路徑（挑選下一個 task、切換）時，能不能贏過 avr-gcc 加 LTO 產生的程式碼，這是下一步。每一項改動都會用同一個 testbench 量測後才保留。

---

## 限制

* 只有協作式排程，沒有搶佔，也沒有時間片。
* 只能靜態建立 task，最多 `LITERTOS_MAX_TASKS`（預設 4）個。
* 還沒有佇列、semaphore、mutex、軟體 timer、event group。
* tick 來自 `millis()`，中斷被關閉太久時會不準。
* 只支援 ATmega328P（Arduino Uno）。

---

## 測試

核心用真正的 AVR 機器碼在 [simavr](https://github.com/buserror/simavr) 上測試。測試涵蓋優先權順序、輪流、延遲、不漂移的週期與錯過期限後的追趕、切換時 callee-saved 暫存器的保存、巢狀臨界區、stack 高水位、suspend/resume/delete，以及臨界區巨集。

```bash
sudo apt install gcc-avr avr-libc build-essential libsimavr-dev libelf-dev
make test-all     # 一般與 LTO 兩種編譯
make lint         # 以 -Werror 編譯
make size         # 核心的 flash / RAM
```

CI 會執行 `make test-all`，並為 Arduino Uno 編譯範例。

---

## 專案結構

```text
src/                 核心（C）與組合語言函式
examples/basic/      雙 task 範例
test/simavr/         模擬器測試
test/bench/          LiteRTOS 與 FreeRTOS 的 testbench
```


## 授權

MIT。見 [LICENSE](LICENSE)。
