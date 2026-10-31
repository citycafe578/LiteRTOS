#pragma once
/* simavr 驗證用的最小替身：核心只需要 millis()。 */
#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#ifdef __cplusplus
extern "C" {
#endif
unsigned long millis(void);
#ifdef __cplusplus
}
#endif
