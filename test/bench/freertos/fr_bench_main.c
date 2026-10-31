/* FreeRTOS 版的速度 testbench：量法、task 數量、迴圈都跟 test/bench/bench_main.c 相同，只是換成 FreeRTOS
 *（kernel_min：功能對等的最小設定、合作式）。結果由序列埠（115200）印出。
 *
 * 用法：  pio run -e bench_fr -t upload   然後開序列埠監視器（115200），約 2 秒後印出結果。
 *
 * 量測方式：Timer1 以 16 MHz（不分頻）計數，32 位元（溢位中斷補高 16 位），所以 1 cycle = 62.5 ns。
 * 量測期間 Timer0 的中斷（millis）會暫時關掉，避免它的 ISR 混進數字；量完就恢復。
 * 注意：這個 testbench 會占用 Timer1，所以不能同時用 analogWrite() 驅動 9、10 腳。 */
#include <Arduino.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <Arduino_FreeRTOS.h>
#include <task.h>

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
#define NTASK  4                 /* 與 LiteRTOS 的 LITERTOS_MAX_TASKS 相同 */
#define CTRL_STACK 192           /* FreeRTOS 的 context 較大，stack 要給多一點（高水位會印出實際用量）*/
#define WORK_STACK  96
static StaticTask_t tcb[NTASK];
static StackType_t ctrlStack[CTRL_STACK];
static StackType_t workStack[NTASK - 1][WORK_STACK];
static TaskHandle_t h[NTASK];
static StaticTask_t idleTcb;
static StackType_t idleStack[configMINIMAL_STACK_SIZE];
void vApplicationGetIdleTaskMemory(StaticTask_t **t, StackType_t **st, configSTACK_DEPTH_TYPE *n) {
  *t = &idleTcb; *st = idleStack; *n = configMINIMAL_STACK_SIZE;
}

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


  /* ---- 報告 ---- */
  puts_("\r\n=== FreeRTOS testbench (16 MHz, 1 cycle = 62.5 ns) ===\r\n");
  puts_("task 數量（不含 idle）: "); putu(NTASK); puts_("\r\n");
  puts_("sizeof(StaticTask_t) : "); putu(sizeof(StaticTask_t)); puts_(" B\r\n");
  puts_("timer read overhead: "); putu(ovh); puts_(" cyc (已從下列數字扣掉)\r\n");
  puts_("\r\n-- 每次 task 切換（taskYIELD，含排程器挑選）--\r\n");
  puts_("   task 總數固定 "); putu(NTASK); puts_(" 個，其餘用 vTaskSuspend 暫停；數字含迴圈本身約 15 cyc\r\n");
  for (uint8_t k = 1; k <= NTASK; k++) {
    puts_("  "); putu(k); puts_(" 個可執行     : "); putcyc(sw[k]); puts_("\r\n");
  }
  puts_("\r\n-- 其他 API --\r\n");
  puts_("  taskENTER/EXIT_CRITICAL : "); putcyc(crit); puts_("\r\n");
  puts_("  xTaskGetTickCount       : "); putcyc(tick); puts_("\r\n");
  puts_("\r\n-- tick --\r\n");
  puts_("  tick 頻率（WDT）: "); putu(configTICK_RATE_HZ); puts_(" Hz，解析度 "); putu(portTICK_PERIOD_MS); puts_(" ms\r\n");
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
