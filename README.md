# LiteRTOS

[English](README.md) | [繁體中文](README.zh-TW.md)

A tiny cooperative RTOS for the Arduino Uno (ATmega328P), with a FreeRTOS-style API (the same names for the functions it implements).

## Why

On an Arduino Uno (2 KB RAM, 32 KB flash), FreeRTOS takes a large share of the chip before your own code even starts: a 46-byte TCB per task, an idle task with its own stack, a heap, and a kernel that is built for preemption and many optional features. That leaves little room for the actual program.

LiteRTOS is a smaller kernel for people who already know FreeRTOS. It keeps the FreeRTOS function names and the way you write tasks (`for (;;)`, `vTaskDelay`, `xTaskDelayUntil`), but drops everything a small AVR project rarely needs.

The second goal of this project is an experiment: **does replacing some C with assembly still pay off with a modern compiler (avr-gcc with LTO, `-Os`)?** Some small, hot routines are written in AVR assembly and measured against the C versions. See [Assembly experiment](#assembly-experiment).

## Features

* FreeRTOS-style API: the same names and types for the subset it implements (not a drop-in replacement; see Limitations) (`xTaskCreateStatic`, `vTaskDelay`, `xTaskDelayUntil`, `vTaskSuspend`, `vTaskResume`, `vTaskDelete`, ...)
* Cooperative scheduler, equivalent to FreeRTOS `configUSE_PREEMPTION = 0`
* Priorities, with round-robin among tasks of equal priority
* Static allocation only: no heap, no `malloc`
* 19-byte TCB (FreeRTOS `StaticTask_t` is 46 bytes on AVR)
* 1 ms tick taken from Arduino `millis()`: no timer is used up
* Context switch saves only the registers the avr-gcc ABI requires (r2–r17, r28, r29, SREG)
* Ready-mask scheduler: the scheduler only looks at tasks that can run, and one task switches directly to the next without going through a scheduler context (up to 8 tasks)
* Task notifications: an interrupt handler can wake a sleeping task with up to 8 event bits (`xTaskNotifyWait` / `xTaskNotifyGiveFromISR`)
* `uxTaskGetStackHighWaterMark` for stack sizing
* `portENTER_CRITICAL` / `portEXIT_CRITICAL` as inline assembly macros
* APIs that are not provided fail at compile time with an explanatory message, instead of silently doing nothing
* Tested as real AVR machine code on simavr, with and without LTO

## Status

**Version: v0.3.0** (pre-1.0, the API may still change)

All 28 simavr test cases (in 23 test programs) pass in both plain and LTO builds. It has been verified on a real Arduino Uno. The timing numbers in the assembly experiment below were measured on the simulator. See [Limitations](#limitations).

---

## Basic Usage

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
    vTaskStartScheduler();   // does not return
}

void loop() {}
```

A complete two-task example is in [`examples/basic`](examples/basic/basic.ino). [`examples/notify_demo`](examples/notify_demo/notify_demo.ino) shows two interrupt sources (UART and a button) waking two tasks that control an LED.

Notes:

* Stack depth is in **bytes** (`StackType_t` is `uint8_t`).
* Scheduling is cooperative. A task must call `vTaskDelay`, `xTaskDelayUntil` or `taskYIELD`, or the other tasks never run. Do not call Arduino `delay()` inside a task: it blocks every task.
* The default limit is 4 tasks (`LITERTOS_MAX_TASKS`), at most 8.
* The scheduler picks the next task on the calling task's own stack, so leave some headroom beyond what your own code needs. Check with `uxTaskGetStackHighWaterMark`.

---

## API Overview

| Function | Description |
| -------- | ----------- |
| `xTaskCreateStatic(...)` | Create a task from caller-provided stack and TCB buffers |
| `vTaskStartScheduler()` | Start scheduling; does not return |
| `vTaskDelay(ticks)` | Block for a number of ticks |
| `xTaskDelayUntil(&last, inc)` | Block until a fixed period since the previous wake, with no drift |
| `taskYIELD()` | Let another ready task of equal or higher priority run |
| `vTaskSuspend(task)` / `vTaskResume(task)` | Suspend or resume a task (`NULL` means the calling task for suspend) |
| `vTaskDelete(task)` | Remove another task (`NULL` means the calling task, which is only suspended: its slot is not freed) |
| `xTaskGetTickCount()` | Current tick count |
| `xTaskGetCurrentTaskHandle()` | Handle of the running task |
| `uxTaskGetStackHighWaterMark(task)` | Minimum free stack bytes seen so far (`NULL` means the calling task) |
| `xTaskNotifyWait(task, &events)` | Sleep until an interrupt or task sends a notification, then read and clear the event bits (`NULL` means the calling task). Waits forever; there is no timeout |
| `xTaskNotifyGiveFromISR(task, bits)` | OR event bits into a task's notification and wake it if it is waiting |
| `vTaskRestart(task)` | Reset a task to its entry function with a clean stack and notification (LiteRTOS extension, not a FreeRTOS API; `NULL` means the calling task) |
| `taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()` | Critical section, function version |
| `portENTER_CRITICAL()` / `portEXIT_CRITICAL()` | Critical section, inline-assembly version; must be paired inside one function and must not span a blocking call |

Not provided (compile-time error): `xTaskCreate`, `vTaskDelayUntil`, `vTaskPrioritySet`, `uxTaskPriorityGet`. Queues, semaphores, mutexes, timers and event groups are not part of LiteRTOS.

---

## Resource Comparison

Same test program (4 tasks) built for both kernels with the same flags (`-Os`, LTO), measured on simavr. FreeRTOS is Arduino_FreeRTOS_Library configured as small as possible (cooperative, static allocation, no timers, no mutexes). Whole-program numbers, including the Arduino core.

| | LiteRTOS | FreeRTOS (minimal config) |
| --- | ---: | ---: |
| Flash | 5976 B | 7766 B |
| Static RAM (data + bss) | 1394 B | 1758 B |
| TCB per task | 19 B | 46 B |
| Tick resolution | 1 ms | 16 ms (watchdog tick) |

### Measured on a real Arduino Uno

`test/bench/bench_main.c` run on an Uno (16 MHz, v0.3.0, `-Os` + LTO), cycles per context switch with the other tasks suspended. The simulator gave the same numbers within 1 cycle (123 / 241 / 246 / 251).

| Runnable tasks | 1 | 2 | 3 | 4 |
| --- | ---: | ---: | ---: | ---: |
| `taskYIELD` (includes picking the next task) | 124 cyc (7.7 µs) | 241 cyc (15.0 µs) | 246 cyc (15.3 µs) | 252 cyc (15.7 µs) |

`taskENTER_CRITICAL` + `taskEXIT_CRITICAL`: 17 cycles. `xTaskGetTickCount`: 19 cycles. A task that only calls `taskYIELD()` in a loop peaks at 40 bytes of stack, including the 21-byte initial context.

LiteRTOS is smaller in flash, RAM and TCB. The default (non-minimal) FreeRTOS configuration is larger still; `test/bench/report.py` reproduces the table including it.

Reproduce:

```bash
python test/bench/report.py          # builds with PlatformIO, prints the Flash/RAM table
pio run -e bench -t upload           # LiteRTOS speed test (115200 baud)
pio run -e bench_fr -t upload        # FreeRTOS speed test, same method
```

---

## Assembly experiment

Two routines are written in AVR assembly instead of C, and everything else stays in C:

| File | Replaces | Role |
| --- | --- | --- |
| `src/PortInitStack.S` | C `LiteRTOS_portInitStack` | Builds the initial 21-byte context of a new task |
| `src/uxTaskGetStackHighWaterMark.S` | the C loop in `uxTaskGetStackHighWaterMark` | Counts untouched stack bytes |

The context switch itself and the critical-section macros are inline assembly.

Result so far (simulator): the assembly versions give **identical switch speed** and save about **34 B of flash** over the C versions, with no RAM change. These two routines are not on the switch path, so little gain is expected from them. The more interesting question, whether assembly in the hot path (the scheduler pick, the switch) beats what avr-gcc with LTO generates, is the next step. Every change is measured with the same testbench before it is kept.

---

## Limitations

* Cooperative only. No preemption, no time slicing.
* Static task creation only; at most `LITERTOS_MAX_TASKS` (default 4) tasks.
* No queues, semaphores, mutexes, software timers or event groups yet.
* Not a drop-in replacement for FreeRTOS: only the functions listed above exist, and `vTaskRestart` is a LiteRTOS extension. Code that relies on preemption, queues or other FreeRTOS features has to be changed.
* Notifications carry one byte (8 event bits), have no timeout, and are simpler than FreeRTOS's `xTaskNotifyWait` (same name, different signature).
* The tick comes from `millis()`; it is not exact if interrupts are disabled for long.
* ATmega328P (Arduino Uno) only.

---

## Testing

The kernel is tested as real AVR machine code on [simavr](https://github.com/buserror/simavr). The tests cover priority order, round-robin, delay, drift-free periods and missed-deadline catch-up, callee-saved register preservation across switches, nested critical sections, stack high-water mark, suspend/resume/delete, and the critical-section macros.

```bash
sudo apt install gcc-avr avr-libc build-essential libsimavr-dev libelf-dev
make test-all     # plain and LTO builds
make lint         # compile with -Werror
make size         # kernel flash / RAM
```

CI runs `make test-all` and compiles the example for an Arduino Uno.

---

## Project Layout

```text
src/                 kernel (C) and the assembly routines
examples/basic/      two-task example
examples/notify_demo/  interrupt-driven tasks with notifications
test/simavr/         simulator tests
test/bench/          LiteRTOS vs FreeRTOS testbench
```


## License

MIT. See [LICENSE](LICENSE).
