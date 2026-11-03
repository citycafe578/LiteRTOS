#include <Arduino.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <LiteRTOS.h>

/* 同時處理兩個輸入來源、控制 LED 明滅：
 *
 *   UART 收到字元 ──ISR──▶ 通知 ──▶ UartProtocolPump ─┐
 *                                                       ├─▶ 共用設定 ──▶ GpioController 閃 LED
 *   D2 按鈕（INT0） ──ISR──▶ 通知 ──▶ I2cProtocolPump ─┘
 *
 * 兩個 pump 平常都在睡（xTaskNotifyWait 不吃 CPU）；ISR 只做最少的事：
 * 存下資料、送一個通知位元。真正的解析在 task 裡做。
 *
 * UART 指令（115200 baud）：
 *   f  快閃（100 ms）    s  慢閃（500 ms）
 *   b  閃爍模式          o  恆亮模式
 *
 * 說明：
 *   1. 這裡直接操作 UART0 暫存器，不使用 Arduino 的 Serial（它自己佔用了 RX 中斷）。
 *   2. 真正的 I2C 要接 TWI 中斷，Arduino 的 Wire 已經佔用它，所以這個範例用 D2 按鈕
 *      （外部中斷 INT0）當作第二個事件來源。換成 I2C 時，只要在 TWI 的 ISR 裡
 *      同樣呼叫 xTaskNotifyGiveFromISR 即可，task 的寫法不變。 */

#define EVT_RX       0x01   /* UART 收到字元 */
#define EVT_OVERFLOW 0x02   /* UART 緩衝區滿了，掉了字元 */
#define EVT_EDGE     0x01   /* 按鈕被按下 */

#define MODE_BLINK   0
#define MODE_SOLID   1

/* ---------- 兩個 task 共用的設定（用臨界區保護）---------- */
static volatile uint16_t blinkMs = 500;
static volatile uint8_t  ledMode = MODE_BLINK;

/* ---------- task 與 handle ---------- */
static StaticTask_t uartTcb, i2cTcb, gpioTcb;
static StackType_t  uartStack[96], i2cStack[96], gpioStack[64];
static TaskHandle_t uartHandle, i2cHandle;

/* ---------- UART 接收緩衝區（ISR 寫、task 讀）---------- */
#define RX_SIZE 16
static volatile uint8_t rxBuf[RX_SIZE];
static volatile uint8_t rxHead, rxTail;

static void uartInit(void) {
  UCSR0A = (1 << U2X0);
  UBRR0  = 16;                                          /* 115200 baud @ 16 MHz */
  UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0); /* 開接收、傳送、接收中斷 */
  UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

static void uartPuts(const char *s) {
  while (*s) {
    while (!(UCSR0A & (1 << UDRE0))) { }
    UDR0 = (uint8_t)*s++;
  }
}

ISR(USART_RX_vect) {
  const uint8_t b = UDR0;
  const uint8_t next = (uint8_t)((rxHead + 1) % RX_SIZE);
  if (next == rxTail) {
    xTaskNotifyGiveFromISR(uartHandle, EVT_OVERFLOW);
    return;
  }
  rxBuf[rxHead] = b;
  rxHead = next;
  xTaskNotifyGiveFromISR(uartHandle, EVT_RX);
}

ISR(INT0_vect) {
  xTaskNotifyGiveFromISR(i2cHandle, EVT_EDGE);
}

static bool rxPop(uint8_t *out) {
  bool ok = false;
  taskENTER_CRITICAL();
  if (rxTail != rxHead) {
    *out = rxBuf[rxTail];
    rxTail = (uint8_t)((rxTail + 1) % RX_SIZE);
    ok = true;
  }
  taskEXIT_CRITICAL();
  return ok;
}

static void setBlinkMs(uint16_t ms) {
  taskENTER_CRITICAL();
  blinkMs = ms;
  taskEXIT_CRITICAL();
}

static void setMode(uint8_t m) {
  taskENTER_CRITICAL();
  ledMode = m;
  taskEXIT_CRITICAL();
}

/* ---------- UartProtocolPump：收到通知才醒來，把字元解析成指令 ---------- */
static void uartProtocolPump(void *arg) {
  (void)arg;
  for (;;) {
    uint8_t events = 0;
    xTaskNotifyWait(NULL, &events);          /* 睡到有 ISR 通知 */

    if (events & EVT_OVERFLOW) {
      uartPuts("rx overflow\r\n");
    }

    uint8_t c;
    while (rxPop(&c)) {
      switch (c) {
        case 'f': setBlinkMs(100); uartPuts("fast\r\n");  break;
        case 's': setBlinkMs(500); uartPuts("slow\r\n");  break;
        case 'b': setMode(MODE_BLINK); uartPuts("blink\r\n"); break;
        case 'o': setMode(MODE_SOLID); uartPuts("solid\r\n"); break;
        default:  break;
      }
    }
  }
}

/* ---------- I2cProtocolPump（這裡由按鈕代替）：每按一下切換模式 ---------- */
static void i2cProtocolPump(void *arg) {
  (void)arg;
  for (;;) {
    xTaskNotifyWait(NULL, NULL);
    vTaskDelay(pdMS_TO_TICKS(50));           /* 簡單的去彈跳：等抖動結束 */

    taskENTER_CRITICAL();
    ledMode = (ledMode == MODE_BLINK) ? MODE_SOLID : MODE_BLINK;
    const uint8_t m = ledMode;
    taskEXIT_CRITICAL();
    uartPuts(m == MODE_BLINK ? "button: blink\r\n" : "button: solid\r\n");
  }
}

/* ---------- GpioController：依共用設定控制 LED ---------- */
static void gpioController(void *arg) {
  (void)arg;
  pinMode(LED_BUILTIN, OUTPUT);
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    taskENTER_CRITICAL();
    const uint16_t ms = blinkMs;
    const uint8_t mode = ledMode;
    taskEXIT_CRITICAL();

    if (mode == MODE_BLINK) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(ms / 2));
    } else {
      digitalWrite(LED_BUILTIN, HIGH);
      xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(20));
    }
  }
}

void setup(void) {
  uartInit();

  uartHandle = xTaskCreateStatic(uartProtocolPump, "uart", sizeof(uartStack), NULL,
                                 tskIDLE_PRIORITY + 2, uartStack, &uartTcb);
  i2cHandle  = xTaskCreateStatic(i2cProtocolPump, "i2c", sizeof(i2cStack), NULL,
                                 tskIDLE_PRIORITY + 2, i2cStack, &i2cTcb);
  xTaskCreateStatic(gpioController, "gpio", sizeof(gpioStack), NULL,
                    tskIDLE_PRIORITY + 1, gpioStack, &gpioTcb);

  pinMode(2, INPUT_PULLUP);                  /* D2 = INT0 */
  EICRA = (1 << ISC01);                      /* 下降緣觸發 */
  EIMSK = (1 << INT0);

  vTaskStartScheduler();                     /* 不會返回 */
}

void loop(void) {}
