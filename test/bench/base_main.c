/* testbench 的「空白對照組」：同樣的 Arduino core、同樣的 LED 閃爍邏輯，但不含 LiteRTOS。
 * test/bench/report.py 用它當基準，從 size_main.c 的 Flash/RAM 扣掉，
 * 剩下的就是 LiteRTOS 核心（加上那兩個 task 的 TCB 與 stack）的成本。 */
#include <Arduino.h>

static volatile uint16_t counter;

void setup(void) {
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop(void) {
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  counter++;
  delay(500);
}
