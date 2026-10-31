/* FreeRTOS 版的「基準應用」：跟 test/bench/size_main.c 一模一樣的兩個 task，只是換成 FreeRTOS。
 * 兩個環境共用這個檔案：
 *   bench_fr_size        功能對等的最小設定（kernel_min，靜態配置）
 *   bench_fr_stock_size  套件原本的設定（kernel_stock，動態配置、搶佔式）
 * 動態配置時 task 的 stack 在 heap 裡，avr-size 看不到；板子上電後 idle hook 會從序列埠印一次
 * 「heap used」（115200），那才是這個設定真正多吃的 RAM。 */
#include <Arduino.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <Arduino_FreeRTOS.h>
#include <task.h>

#define STACK_A 96
#define STACK_B 128

static volatile uint16_t counter;

#if configSUPPORT_STATIC_ALLOCATION
static StaticTask_t szTcbA, szTcbB;
static StackType_t szStackA[STACK_A], szStackB[STACK_B];
static StaticTask_t idleTcb;                       /* FreeRTOS 一定有 idle task，靜態配置時要自己給記憶體 */
static StackType_t idleStack[configMINIMAL_STACK_SIZE];
void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, configSTACK_DEPTH_TYPE *size) {
  *tcb = &idleTcb;
  *stack = idleStack;
  *size = configMINIMAL_STACK_SIZE;
}
#endif

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

#if !configSUPPORT_STATIC_ALLOCATION
extern char *__brkval, __heap_start;
static void putc_(char c) {
#ifdef BENCH_SIM
  GPIOR0 = (uint8_t)c;
#else
  while (!(UCSR0A & (1 << UDRE0))) { }
  UDR0 = (uint8_t)c;
#endif
}
/* 覆寫套件的 weak idle hook：第一次執行時印出 heap 用量 */
void vApplicationIdleHook(void) {
  static uint8_t done;
  if (!done) {
    done = 1;
    uint16_t used = (uint16_t)((uintptr_t)__brkval - (uintptr_t)&__heap_start);
    static const char m[] PROGMEM = "heap used=";
    for (uint8_t i = 0; i < sizeof(m) - 1; i++) { putc_((char)pgm_read_byte(&m[i])); }
    char buf[6]; uint8_t n = 0;
    do { buf[n++] = (char)('0' + used % 10); used /= 10; } while (used);
    while (n) { putc_(buf[--n]); }
    putc_('\r'); putc_('\n');
  }
}
#endif

void setup(void) {
#if !configSUPPORT_STATIC_ALLOCATION
  UCSR0A = (1 << U2X0); UBRR0 = 16; UCSR0B = (1 << TXEN0); UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
  xTaskCreate(taskA, "A", STACK_A, NULL, tskIDLE_PRIORITY + 1, NULL);
  xTaskCreate(taskB, "B", STACK_B, NULL, tskIDLE_PRIORITY + 1, NULL);
#else
  xTaskCreateStatic(taskA, "A", STACK_A, NULL, tskIDLE_PRIORITY + 1, szStackA, &szTcbA);
  xTaskCreateStatic(taskB, "B", STACK_B, NULL, tskIDLE_PRIORITY + 1, szStackB, &szTcbB);
#endif
  vTaskStartScheduler();
}

void loop(void) {}
