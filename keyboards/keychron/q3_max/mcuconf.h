/* Copyright 2024 ~ 2026 @ Keychron (https://www.keychron.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include_next <mcuconf.h>

#undef STM32_HSECLK
#define STM32_HSECLK 16000000

/*
 * PLL Configuration
 *
 * Default (48 MHz):  PLLM=8, PLLN=96,  PLLP=4, PLLQ=4
 *   VCO = 16MHz/8 × 96  = 192 MHz  →  SYSCLK = 192/4 = 48 MHz,  USB = 192/4 = 48 MHz
 *
 * 84 MHz (double):    PLLM=8, PLLN=168, PLLP=4, PLLQ=7
 *   VCO = 16MHz/8 × 168 = 336 MHz  →  SYSCLK = 336/4 = 84 MHz,  USB = 336/7 = 48 MHz
 *
 * Controlled by SYSCLK_84MHZ (defined in config.h).
 */
#if defined(SYSCLK_84MHZ)
#    define _Q3M_PLLM 8
#    define _Q3M_PLLN 168
#    define _Q3M_PLLP 4
#    define _Q3M_PLLQ 7
#else
#    define _Q3M_PLLM 8
#    define _Q3M_PLLN 96
#    define _Q3M_PLLP 4
#    define _Q3M_PLLQ 4
#endif

#undef STM32_PLLM_VALUE
#define STM32_PLLM_VALUE _Q3M_PLLM

#undef STM32_PLLN_VALUE
#define STM32_PLLN_VALUE _Q3M_PLLN

#undef STM32_PLLP_VALUE
#define STM32_PLLP_VALUE _Q3M_PLLP

#undef STM32_PLLQ_VALUE
#define STM32_PLLQ_VALUE _Q3M_PLLQ

#undef STM32_SPI_USE_SPI1
#define STM32_SPI_USE_SPI1 TRUE
