;/*****************************************************************************
; * @file:    startup_SAM3XA.s
; * @purpose: CMSIS Cortex-M3 Core Device Startup File 
; *           for the Atmel SAM3XA Device Series 
; * @version: V1.10
; * @date:    15. April 2013
; *------- <<< Use Configuration Wizard in Context Menu >>> ------------------
; *
; * Copyright (C) 2010-2013 ARM Limited. All rights reserved.
; * ARM Limited (ARM) is supplying this software for use with Cortex-M3 
; * processor based microcontrollers.  This file can be freely distributed 
; * within development tools that are supporting such ARM based processors. 
; *
; * THIS SOFTWARE IS PROVIDED "AS IS".  NO WARRANTIES, WHETHER EXPRESS, IMPLIED
; * OR STATUTORY, INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF
; * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE.
; * ARM SHALL NOT, IN ANY CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL, OR
; * CONSEQUENTIAL DAMAGES, FOR ANY REASON WHATSOEVER.
; *
; *****************************************************************************/


; <h> Stack Configuration
;   <o> Stack Size (in Bytes) <0x0-0xFFFFFFFF:8>
; </h>

Stack_Size      EQU     0x00000600

                AREA    STACK, NOINIT, READWRITE, ALIGN=3
				EXPORT	__initial_sp
Stack_Mem       SPACE   Stack_Size
__initial_sp


; <h> Heap Configuration
;   <o>  Heap Size (in Bytes) <0x0-0xFFFFFFFF:8>
; </h>

Heap_Size       EQU     0x00001b00

                AREA    HEAP, NOINIT, READWRITE, ALIGN=3
				EXPORT 	  __heap_base
				EXPORT    __heap_limit
__heap_base
Heap_Mem        SPACE   Heap_Size
__heap_limit


                PRESERVE8
                THUMB


; Vector Table Mapped to Address 0 at Reset

                AREA    ENTRY, DATA, READONLY
                EXPORT  __Vectors

__Vectors       DCD     __initial_sp              ;  0: Top of Stack
                DCD     Reset_Handler             ;  1: Reset Handler
                DCD     NMI_Handler               ;  2: NMI Handler
                DCD     HardFault_Handler         ;  3: Hard Fault Handler
                DCD     MemManage_Handler         ;  4: MPU Fault Handler
                DCD     BusFault_Handler          ;  5: Bus Fault Handler
                DCD     UsageFault_Handler        ;  6: Usage Fault Handler
                DCD     0                         ;  7: Reserved
                DCD     0                         ;  8: Reserved
                DCD     0                         ;  9: Reserved
                DCD     0                         ; 10: Reserved
                DCD     SVC_Handler               ; 11: SVCall Handler
                DCD     DebugMon_Handler          ; 12: Debug Monitor Handler
                DCD     0                         ; 13: Reserved
                DCD     PendSV_Handler            ; 14: PendSV Handler
                DCD     SysTick_Handler           ; 15: SysTick Handler

                ; External Interrupts
                DCD     Undefined_IRQHandle           ;  0: Supply Controller (SUPC)
                DCD     Undefined_IRQHandle           ;  1: Reset Controller (RSTC)
                DCD     Undefined_IRQHandle            ;  2: Real Time Clock (RTC)
                DCD     Undefined_IRQHandle            ;  3: Real Time Timer (RTT)
                DCD     Undefined_IRQHandle            ;  4: Watchdog Timer (WDT)
                DCD     Undefined_IRQHandle            ;  5: Power Management Controller (PMC)
                DCD     Undefined_IRQHandle           ;  6: Enhanced Flash Controller 0 (EFC0)
                DCD     Undefined_IRQHandle           ;  7: Enhanced Flash Controller 1 (EFC1)
                DCD     Undefined_IRQHandle           ;  8: Universal Asynchronous Receiver Transceiver (UART)
                DCD     Undefined_IRQHandle            ;  9: Static Memory Controller (SMC)
                DCD     Undefined_IRQHandle         ; 10: Synchronous Dynamic RAM Controller (SDRAMC)
                DCD     Undefined_IRQHandle           ; 11: Parallel I/O Controller A + 16: (PIOA)
                DCD     Undefined_IRQHandle           ; 12: Parallel I/O Controller B (PIOB)
                DCD     Undefined_IRQHandle           ; 13: Parallel I/O Controller C (PIOC)
                DCD     Undefined_IRQHandle           ; 14: Parallel I/O Controller D (PIOD)
                DCD     Undefined_IRQHandle           ; 15: Parallel I/O Controller E (PIOE)
                DCD     Undefined_IRQHandle           ; 16: Parallel I/O Controller F (PIOF)
                DCD     Undefined_IRQHandle         ; 17: USART 0 (USART0)
                DCD     Undefined_IRQHandle         ; 18: USART 1 (USART1)
                DCD     Undefined_IRQHandle         ; 19: USART 2 (USART2)
                DCD     Undefined_IRQHandle         ; 20: USART 3 (USART3)
                DCD     Undefined_IRQHandle          ; 21: Multimedia Card Interface (HSMCI)
                DCD     Undefined_IRQHandle           ; 22: Two-Wire Interface 0 (TWI0)
                DCD     Undefined_IRQHandle           ; 23: Two-Wire Interface 1 (TWI1)
                DCD     Undefined_IRQHandle           ; 24: Serial Peripheral Interface (SPI0)
                DCD     Undefined_IRQHandle           ; 25: Serial Peripheral Interface (SPI1)
                DCD     Undefined_IRQHandle            ; 26: Synchronous Serial Controller (SSC)
                DCD     Undefined_IRQHandle            ; 27: Timer Counter 0 (TC0)
                DCD     Undefined_IRQHandle            ; 28: Timer Counter 1 (TC1)
                DCD     Undefined_IRQHandle            ; 29: Timer Counter 2 (TC2)
                DCD     Undefined_IRQHandle            ; 30: Timer Counter 3 (TC3)
                DCD     Undefined_IRQHandle            ; 31: Timer Counter 4 (TC4)
                DCD     Undefined_IRQHandle            ; 32: Timer Counter 5 (TC5)
                DCD     Undefined_IRQHandle            ; 33: Timer Counter 6 (TC6)
                DCD     Undefined_IRQHandle            ; 34: Timer Counter 7 (TC7)
                DCD     Undefined_IRQHandle            ; 35: Timer Counter 8 (TC8)
                DCD     Undefined_IRQHandle            ; 36: Pulse Width Modulation Controller (PWM)
                DCD     Undefined_IRQHandle            ; 37: ADC Controller (ADC)
                DCD     Undefined_IRQHandle           ; 38: DAC Controller (DACC)
                DCD     Undefined_IRQHandle           ; 39: DMA Controller (DMAC)
                DCD     UOTGHS_IRQHandler         ; 40: USB OTG High Speed (UOTGHS)
                DCD     Undefined_IRQHandle           ; 41: True Random Number Generator (TRNG)
                DCD     Undefined_IRQHandle           ; 42: Ethernet MAC (EMAC)
                DCD     Undefined_IRQHandle           ; 43: CAN Controller 0 (CAN0)
                DCD     Undefined_IRQHandle           ; 44: CAN Controller 1 (CAN1)

                AREA    |.text|, CODE, READONLY


; Reset Handler

Reset_Handler   PROC
                EXPORT  Reset_Handler           
                IMPORT  __main
				B		Reset_Handler
                LDR     R0, =__main
                BX      R0
                ENDP


; Dummy Exception Handlers (infinite loops which can be modified)                

NMI_Handler     PROC
                EXPORT  NMI_Handler               [WEAK]
                B       .
                ENDP
HardFault_Handler\
                PROC
                EXPORT  HardFault_Handler         [WEAK]
                B       .
                ENDP
MemManage_Handler\
                PROC
                EXPORT  MemManage_Handler         [WEAK]
                B       .
                ENDP
BusFault_Handler\
                PROC
                EXPORT  BusFault_Handler          [WEAK]
                B       .
                ENDP
UsageFault_Handler\
                PROC
                EXPORT  UsageFault_Handler        [WEAK]
                B       .
                ENDP
SVC_Handler     PROC
                EXPORT  SVC_Handler               [WEAK]
                B       .
                ENDP
DebugMon_Handler\
                PROC
                EXPORT  DebugMon_Handler          [WEAK]
                B       .
                ENDP
PendSV_Handler  PROC
                EXPORT  PendSV_Handler            [WEAK]
                B       .
                ENDP
SysTick_Handler PROC
                EXPORT  SysTick_Handler           [WEAK]
                B       .
                ENDP

UOTGHS_Handler PROC
                EXPORT  UOTGHS_IRQHandler           [WEAK]
UOTGHS_IRQHandler
                B       .
                ENDP


Default_Handler PROC

                EXPORT  Undefined_IRQHandle       [WEAK]
Undefined_IRQHandle
                B       .

                ENDP


                ALIGN


; User Initial Stack & Heap

                IF      :DEF:__MICROLIB
                
                EXPORT  __initial_sp
                EXPORT  __heap_base
                EXPORT  __heap_limit
                
                ELSE
                
                IMPORT  __use_two_region_memory
                EXPORT  __user_initial_stackheap
__user_initial_stackheap

                LDR     R0, =  Heap_Mem
                LDR     R1, =(Stack_Mem + Stack_Size)
                LDR     R2, = (Heap_Mem +  Heap_Size)
                LDR     R3, = Stack_Mem
                BX      LR

                ENDIF
                ALIGN

                END
