/* tools/lxr_ref: a stand-in for the STM32 HAL headers the LXR's sources include, on a host. GPL-3.0-only. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define __IO volatile
typedef struct { volatile uint32_t CTRL, CYCCNT, LAR; } DWT_Type;
typedef struct { volatile uint32_t DEMCR; } CoreDebug_Type;
static DWT_Type __dwt; static CoreDebug_Type __cd;
#define DWT (&__dwt)
#define CoreDebug (&__cd)
#define CoreDebug_DEMCR_TRCENA_Msk 0
#define DWT_CTRL_CYCCNTENA_Msk 0
typedef struct { int Instance; } RNG_HandleTypeDef;
typedef int HAL_StatusTypeDef;
static inline uint32_t HAL_GetTick(void){return 0;}
#define __STATIC_INLINE static inline
typedef struct { int x; } I2S_HandleTypeDef, UART_HandleTypeDef, TIM_HandleTypeDef, SPI_HandleTypeDef, DMA_HandleTypeDef, ADC_HandleTypeDef, I2C_HandleTypeDef, GPIO_TypeDef;
static uint32_t SystemCoreClock = 216000000;
typedef union { struct { uint32_t _r:28, V:1, C:1, Z:1, N:1; } b; uint32_t w; } APSR_Type;
static inline uint32_t __get_APSR(void){ return 0; }   /* host: the V flag is not seen */
#define __fp16 _Float16
