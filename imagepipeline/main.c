/* Copyright (C) 2023-2024 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification of this code is permitted under the
 * terms stated in the Alif Semiconductor Software License Agreement
 *
 * You should have received a copy of the Alif Semiconductor Software
 * License Agreement with this file. If not, please write to:
 * contact@alifsemi.com, or visit: https://alifsemi.com/license
 *
 */


#include "RTE_Components.h"
#include "alif.h"

#include "board_defs.h"
#include "board_config.h"
#include "leds.h"

#include "power_management.h"

#if !defined(DISABLE_UART_TRACE)
#include <stdio.h>
#include "uart_tracelib.h"
#else
#undef printf
#define printf(fmt, ...)
#endif

#if RTE_CPI && defined(RTE_Drivers_CPI)
#if RTE_ISP && defined(RTE_Drivers_ISP)
static uint8_t cam_buf[RTE_ISP_OUTPUT_WIDTH * RTE_ISP_OUTPUT_HEIGHT * 3 / 2]
    __attribute__((aligned(32), section(".bss.lcd_frame_buf"), used));
#else
static uint8_t cam_buf[RTE_OV5675_CAMERA_SENSOR_FRAME_WIDTH * RTE_OV5675_CAMERA_SENSOR_FRAME_HEIGHT]
    __attribute__((aligned(32), section(".bss.camera_raw_frame_buf"), used));
#endif
#endif // RTE_CPI && defined(RTE_Drivers_CPI)

#if RTE_JPEG && defined(RTE_Drivers_JPEG)
static uint8_t jpeg_buf[128 * 1024]
    __attribute__((aligned(512), section(".bss.video_mem_heap"), used));
#endif

int camera_init(void);
int camera_capture(void* buf_addr);
int jpeg_enc_init(void);
int jpeg_compress(void* in_buf, void* out_buf, uint32_t out_size);
size_t jpeg_get_output_size(void);


int main(void)
{
    int32_t ret;

    // Initialize the pinmux, GPIOs and LEDs
    board_pins_config();
    board_gpios_config();
    leds_init();

    // Initialize the SE services and enable required clocks
    se_services_port_init();
    ret = board_clocks_config(CLKEN_HFOSC_MASK | CLKEN_CLK_100M_MASK);
    if (0 != ret) {
        __BKPT(0);
    }

    // Set the default power profile
    if (true != init_power_management()) {
        __BKPT(0);
    }

    #if !defined(DISABLE_UART_TRACE) // Initialize the UART trace
    tracelib_init(NULL, NULL);
    #endif

    printf("\r\nHello from Alif Ensemble Camera Pipeline Demo\r\n");

    #if RTE_CPI && defined(RTE_Drivers_CPI)
    ret = camera_init();
    if (ARM_DRIVER_OK != ret) {
        printf("Camera init failed with error code %ld\r\n", ret);
    }

    #if RTE_JPEG && defined(RTE_Drivers_JPEG)
    ret = jpeg_enc_init();
    if (ARM_DRIVER_OK != ret) {
        printf("JPEG init failed with error code %ld\r\n", ret);
    }
    #endif // RTE_JPEG && defined(RTE_Drivers_JPEG)

    while (ARM_DRIVER_OK == ret) {
        leds_toggle_green();

        ret = camera_capture(cam_buf);

        if (ARM_DRIVER_OK == ret) {
            printf("\r\nImage captured successfully\r\n");

            #if !(RTE_JPEG && defined(RTE_Drivers_JPEG)) // no need to print raw buffer info if JPEG is enabled
            printf("Use 'dump memory img.bin cam_buf cam_buf+sizeof(cam_buf)' "
                   "to save the capture to your workspace\r\n");
            #endif // !RTE_JPEG && !defined(RTE_Drivers_JPEG)

            #if RTE_JPEG && defined(RTE_Drivers_JPEG)

            // Force-enable cycle counter even when debugger is not connected
            DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
            DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
            DWT->CYCCNT = 0;

            ret = jpeg_compress(cam_buf, jpeg_buf, sizeof(jpeg_buf));
            uint32_t jpeg_cycles = DWT->CYCCNT;

            if (ARM_DRIVER_OK != ret) {
                printf("JPEG compress failed with error code %ld\r\n", ret);
            } else {
                size_t size = jpeg_get_output_size();
                printf("JPEG compressed successfully in %.2f usec, size: %u\r\n", (float)jpeg_cycles / 160.0f, size);
                printf("Use 'dump memory img.jpeg jpeg_buf jpeg_buf+%u' "
                       "to save the JPEG to your workspace\r\n", size);
            }
            #endif // RTE_JPEG && defined(RTE_Drivers_JPEG)
        }
    }

    // If execution gets here, critical failure was encountered
    printf("Appplication terminated with error code %ld\r\n", ret);

    #endif // RTE_CPI && defined(RTE_Drivers_CPI)

    leds_toggle_red();

    for(;;);

    return 0;
}