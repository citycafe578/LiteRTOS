/* testbench 的「基準應用」：用最常用的 API（建立 task、vTaskDelay、xTaskDelayUntil、taskYIELD）
 * 跑兩個 task。test/bench/report.py 拿它跟 base_main.c 比 Flash/RAM，
 * 得到「在這種用法下 LiteRTOS 吃多少」。沒用到的 API 會被連結器丟掉，不算進去。 */
#include <Arduino.h>
#include "../../src/LiteRTOS.h"

#define STACK_A 96
#define STACK_B 128

static StaticTask_t szTcbA, szTcbB;
static StackType_t szStackA[STACK_A], szStackB[STACK_B];
static volatile uint16_t counter;

static void taskA(void *arg) {
  (void)arg;
  pinMode(LED_BUILTIN, OUTPUT);
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    xTaskDelayUntil(&last, pdMS_TO_TICKS(500));
  }
}

static void taskB(void *arg) {
  (void)arg;
  for (;;) {
    counter++;
    vTaskDelay(pdMS_TO_TICKS(100));
    taskYIELD();
  }
}

void setup(void) {
  xTaskCreateStatic(taskA, "A", STACK_A, NULL, tskIDLE_PRIORITY + 1, szStackA, &szTcbA);
  xTaskCreateStatic(taskB, "B", STACK_B, NULL, tskIDLE_PRIORITY + 1, szStackB, &szTcbB);
  vTaskStartScheduler();
}

void loop(void) {}
