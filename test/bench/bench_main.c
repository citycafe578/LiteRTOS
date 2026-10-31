/* LiteRTOS 實機 testbench：在板子上量 LiteRTOS 的速度與資源，結果由序列埠（115200）印出。
 *
 * 用法：  pio run -e bench -t upload   然後開序列埠監視器（115200），約 3 秒後印出結果。
 *
 * 量測方式：Timer1 以 16 MHz（不分頻）計數，32 位元（溢位中斷補高 16 位），所以 1 cycle = 62.5 ns。
 * 量測期間 Timer0 的中斷（millis）會暫時關掉，避免它的 ISR 混進數字；量完就恢復。每項重複 5 次取最小值。
 * 注意：這個 testbench 會占用 Timer1，所以不能同時用 analogWrite() 驅動 9、10 腳。 */
#include <Arduino.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include "../../src/LiteRTOS.h"
#include "../../src/LiteRTOS_internal.h"

/* ------------------------------------------------------------ 輸出 */
static void uartInit(void) {
  UCSR0A = (1 << U2X0);
  UBRR0 = 16;
  UCSR0B = (1 << TXEN0);
  UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}
static void putc_(char c) {
#ifdef BENCH_SIM
  GPIOR0 = (uint8_t)c;                  /* simavr 沒有可用的 UART，改寫到 GPIOR0 */
#else
  while (!(UCSR0A & (1 << UDRE0))) { }
  UDR0 = (uint8_t)c;
#endif
}
static void puts_(const char *s) { while (*s) { putc_(*s++); } }
static void putu(uint32_t v) {
  char buf[10]; uint8_t n = 0;
  if (v == 0) { putc_('0'); return; }
  while (v) { buf[n++] = (char)('0' + (v % 10)); v /= 10; }
  while (n) { putc_(buf[--n]); }
}
/* 印「cycles (xx.x us)」 */
static void putcyc(uint32_t cyc) {
  putu(cyc); puts_(" cyc ("); putu(cyc / 16); putc_('.'); putu((cyc % 16) * 10 / 16); puts_(" us)");
}

/* ------------------------------------------------------------ Timer1：32 位元 cycle 計數 */
static volatile uint16_t t1hi;
ISR(TIMER1_OVF_vect) { t1hi++; }

static void timerStart(void) {
  TCCR1A = 0;
  TCCR1B = (1 << CS10);                 /* 不分頻 */
  TCNT1 = 0;
  t1hi = 0;
  TIMSK1 = (1 << TOIE1);
}
static uint32_t cycles(void) {
  const uint8_t s = SREG;
  cli();
  uint16_t lo = TCNT1;
  uint16_t hi = t1hi;
  if ((TIFR1 & (1 << TOV1)) && lo < 0x8000) { hi++; }   /* 溢位旗標還沒被處理 */
  SREG = s;
  return ((uint32_t)hi << 16) | lo;
}
static void millisOff(void) { TIMSK0 &= (uint8_t)~(1 << TOIE0); }
static void millisOn(void)  { TIMSK0 |= (1 << TOIE0); }

/* ------------------------------------------------------------ tasks */
#define NTASK  LITERTOS_MAX_TASKS
#define CTRL_STACK 128
#define WORK_STACK  64
static StaticTask_t tcb[NTASK];
static StackType_t ctrlStack[CTRL_STACK];
static StackType_t workStack[NTASK - 1][WORK_STACK];
static TaskHandle_t h[NTASK];

/* 其他 task 只做一件事：讓出 CPU，用來讓排程器有「k 個 task 可選」的負載 */
static void workTask(void *arg) { (void)arg; for (;;) { taskYIELD(); } }

/* ------------------------------------------------------------ 量測 */
#define ROUNDS 200
#define REPEAT 5
static uint32_t ovh;                    /* 讀一次 cycles() 的額外成本 */

/* k 個 task 可執行時，每次切換平均幾 cycles。控制 task 本身算一個。 */
static uint32_t measureSwitch(uint8_t k) {
  for (uint8_t i = 1; i < NTASK; i++) {
    if (i < k) { vTaskResume(h[i]); } else { vTaskSuspend(h[i]); }
  }
  taskYIELD();                          /* 讓剛恢復的 task 都跑過一次 */
  uint32_t best = 0xFFFFFFFFUL;
  for (uint8_t rep = 0; rep < REPEAT; rep++) {          /* 取最小值，排除偶發的中斷干擾 */
    millisOff();
    const uint32_t t0 = cycles();
    for (uint16_t i = 0; i < ROUNDS; i++) { taskYIELD(); }
    const uint32_t t1 = cycles();
    millisOn();
    /* 控制 task 每 yield 一次，整個系統共切換 k 次（每個 task 各一次） */
    const uint32_t v = (t1 - t0 - ovh) / ((uint32_t)ROUNDS * k);
    if (v < best) { best = v; }
  }
  return best;
}

static uint32_t measureCritical(void) {
  uint32_t best = 0xFFFFFFFFUL;
  for (uint8_t rep = 0; rep < REPEAT; rep++) {
    millisOff();
    const uint32_t t0 = cycles();
    for (uint16_t i = 0; i < ROUNDS; i++) { taskENTER_CRITICAL(); taskEXIT_CRITICAL(); }
    const uint32_t t1 = cycles();
    millisOn();
    const uint32_t v = (t1 - t0 - ovh) / ROUNDS;
    if (v < best) { best = v; }
  }
  return best;
}

static uint32_t measureTickCount(void) {
  uint32_t best = 0xFFFFFFFFUL;
  volatile TickType_t sink = 0;
  for (uint8_t rep = 0; rep < REPEAT; rep++) {
    millisOff();
    const uint32_t t0 = cycles();
    for (uint16_t i = 0; i < ROUNDS; i++) { sink = xTaskGetTickCount(); }
    const uint32_t t1 = cycles();
    millisOn();
    const uint32_t v = (t1 - t0 - ovh) / ROUNDS;
    if (v < best) { best = v; }
  }
  (void)sink;
  return best;
}

/* xTaskDelayUntil(10 ms) 的實際週期，量 100 次，回傳最小與最大（cycles）。Timer0 要開著，tick 才會走。 */
static void measureJitter(uint32_t *mn, uint32_t *mx) {
  TickType_t last = xTaskGetTickCount();
  xTaskDelayUntil(&last, pdMS_TO_TICKS(10));        /* 先對齊到 tick */
  uint32_t prev = cycles();
  *mn = 0xFFFFFFFFUL; *mx = 0;
  for (uint8_t i = 0; i < 100; i++) {
    xTaskDelayUntil(&last, pdMS_TO_TICKS(10));
    const uint32_t now = cycles();
    const uint32_t d = now - prev;
    prev = now;
    if (d < *mn) { *mn = d; }
    if (d > *mx) { *mx = d; }
  }
}

static void controller(void *arg) {
  (void)arg;
  uint32_t sw[NTASK + 1];

  /* 校正：連續讀兩次 cycles() 的差 */
  { uint32_t best = 0xFFFFFFFFUL;
    for (uint8_t i = 0; i < 20; i++) { const uint32_t a = cycles(); const uint32_t b = cycles(); if (b - a < best) { best = b - a; } }
    ovh = best; }

  for (uint8_t k = 1; k <= NTASK; k++) { sw[k] = measureSwitch(k); }
  const uint32_t crit = measureCritical();
  const uint32_t tick = measureTickCount();

  /* 抖動：先只剩控制 task，再讓其他 task 全部忙著 yield */
  uint32_t mnIdle, mxIdle, mnBusy, mxBusy;
  for (uint8_t i = 1; i < NTASK; i++) { vTaskSuspend(h[i]); }
  measureJitter(&mnIdle, &mxIdle);
  for (uint8_t i = 1; i < NTASK; i++) { vTaskResume(h[i]); }
  measureJitter(&mnBusy, &mxBusy);

  /* ---- 報告 ---- */
  puts_("\r\n=== LiteRTOS testbench (16 MHz, 1 cycle = 62.5 ns) ===\r\n");
  puts_("LITERTOS_MAX_TASKS : "); putu(NTASK); puts_("\r\n");
  puts_("sizeof(TCB)        : "); putu(sizeof(StaticTask_t)); puts_(" B\r\n");
  puts_("timer read overhead: "); putu(ovh); puts_(" cyc (已從下列數字扣掉)\r\n");
  puts_("\r\n-- 每次 task 切換（taskYIELD，含排程器挑選）--\r\n");
  puts_("   task 總數固定 "); putu(NTASK); puts_(" 個，其餘用 vTaskSuspend 暫停；數字含迴圈本身約 15 cyc\r\n");
  for (uint8_t k = 1; k <= NTASK; k++) {
    puts_("  "); putu(k); puts_(" 個可執行     : "); putcyc(sw[k]); puts_("\r\n");
  }
  puts_("\r\n-- 其他 API --\r\n");
  puts_("  taskENTER/EXIT_CRITICAL : "); putcyc(crit); puts_("\r\n");
  puts_("  xTaskGetTickCount       : "); putcyc(tick); puts_("\r\n");
  puts_("\r\n-- xTaskDelayUntil(10 ms) 實際週期（100 次，最小 ~ 最大）--\r\n");
  puts_("   tick 是 millis()，解析度 1 ms，所以約有 ±1 ms 的抖動是正常的\r\n");
  puts_("  只有 1 個 task   : "); putu(mnIdle / 16); puts_(" ~ "); putu(mxIdle / 16); puts_(" us\r\n");
  puts_("  其他 task 全忙   : "); putu(mnBusy / 16); puts_(" ~ "); putu(mxBusy / 16); puts_(" us\r\n");
  puts_("\r\n-- stack 高水位（剩餘 / 配置，bytes）--\r\n");
  for (uint8_t i = 0; i < NTASK; i++) {
    puts_("  task "); putu(i); puts_(i == 0 ? " (控制) : " : "        : ");
    putu(uxTaskGetStackHighWaterMark(h[i])); puts_(" / "); putu(i == 0 ? CTRL_STACK : WORK_STACK); puts_("\r\n");
  }
  puts_("=== done ===\r\n");
  for (;;) { vTaskDelay(portMAX_DELAY); }
}

void setup(void) {
  uartInit();
  timerStart();
  h[0] = xTaskCreateStatic(controller, "ctl", CTRL_STACK, NULL, tskIDLE_PRIORITY + 1, ctrlStack, &tcb[0]);
  for (uint8_t i = 1; i < NTASK; i++) {
    h[i] = xTaskCreateStatic(workTask, "wk", WORK_STACK, NULL, tskIDLE_PRIORITY + 1, workStack[i - 1], &tcb[i]);
  }
  vTaskStartScheduler();
}
void loop(void) {}
