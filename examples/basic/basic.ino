#include <Arduino.h>
#include <avr/io.h>
#include <LiteRTOS.h>

/* 範例（純 C）：兩個 task，用 FreeRTOS 的寫法。
 *   - blink：每 500 ms 切換板上 LED，使用 xTaskDelayUntil 保持固定週期。
 *   - report：每 1000 ms 從序列埠印出 tick 與 blink 的 stack 剩餘量。
 * 注意：
 *   1. LiteRTOS 是協作式排程，task 內請用 vTaskDelay / xTaskDelayUntil，不要用 delay()
 *      （delay() 會讓所有 task 一起等）。
 *   2. Arduino 的 Serial 是 C++ 類別，C 檔不能用，所以這裡直接操作 UART0 暫存器。 */

static StaticTask_t blinkTcb;
static StackType_t blinkStack[96];
static TaskHandle_t blinkHandle;

static StaticTask_t reportTcb;
static StackType_t reportStack[128];

/* ---- 最小的 UART0 輸出（115200 baud @ 16 MHz，倍速模式）---- */
static void uartInit(void) {
  UCSR0A = (1 << U2X0);
  UBRR0 = 16;                                  /* 16 MHz / (8 * 115200) - 1 ≈ 16 */
  UCSR0B = (1 << TXEN0);                       /* 只開傳送 */
  UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);      /* 8 data bits, 1 stop bit, no parity */
}

static void uartPutc(char c) {
  while (!(UCSR0A & (1 << UDRE0))) { }         /* 等傳送暫存器空 */
  UDR0 = (uint8_t)c;
}

static void uartPuts(const char *s) {
  while (*s) { uartPutc(*s++); }
}

static void uartPutU(uint16_t v) {
  char buf[5];
  uint8_t n = 0;
  if (v == 0) { uartPutc('0'); return; }
  while (v) { buf[n++] = (char)('0' + (v % 10)); v /= 10; }
  while (n) { uartPutc(buf[--n]); }
}

static void blinkTask(void *arg) {
  (void)arg;
  pinMode(LED_BUILTIN, OUTPUT);
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(500));
  }
}

static void reportTask(void *arg) {
  (void)arg;
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    uartPuts("tick=");
    uartPutU(xTaskGetTickCount());
    uartPuts(" blink stack free=");
    uartPutU(uxTaskGetStackHighWaterMark(blinkHandle));
    uartPuts(" report stack free=");
    uartPutU(uxTaskGetStackHighWaterMark(NULL));
    uartPuts("\r\n");

    xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(1000));
  }
}

void setup(void) {
  uartInit();

  blinkHandle = xTaskCreateStatic(
    blinkTask, "blink", sizeof(blinkStack), NULL,
    tskIDLE_PRIORITY + 1, blinkStack, &blinkTcb);

  xTaskCreateStatic(
    reportTask, "report", sizeof(reportStack), NULL,
    tskIDLE_PRIORITY + 1, reportStack, &reportTcb);

  vTaskStartScheduler();   /* 不會返回，所以 loop() 不會被呼叫 */
}

void loop(void) {}
