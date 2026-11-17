/* Copyright (C) 2024 Alif Semiconductor - All Rights Reserved.
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

#if RTE_CPI && defined(RTE_Drivers_CPI)

#include <math.h>
#include <stdio.h>

#include "Driver_CPI.h"

#if RTE_ISP && defined(RTE_Drivers_ISP)
#include "Driver_ISP.h"
#include "vsi_comm_video.h"

#include "isp_calibration.h"

#if RTE_CPI_AXI_PORT
#error "RTE_CPI_AXI_PORT should be disabled in this example to save the raw camera frame buffer memory"
#endif

static VIDEO_BUF_S isp_buf;

#else
#if defined(RTE_CPI_AXI_PORT) && !RTE_CPI_AXI_PORT
#error "RTE_CPI_AXI_PORT should be enabled when ISP is disabled"
#endif
#endif

extern ARM_DRIVER_CPI Driver_CPI;

typedef enum {
CAM_CB_EVENT_NONE            = 0,
CAM_CB_EVENT_ERROR           = (1 << 0),
CAM_CB_EVENT_CAPTURE_STOPPED = (1 << 1),
ISP_VSYNC_CB_EVENT           = (1 << 2),
ISP_MI_FRAME_DUMP_EVENT      = (1 << 3),
ISP_FRAME_IN_DETECTED        = (1 << 4)
} CAM_CB_EVENT;

static volatile CAM_CB_EVENT g_cam_cb_events = CAM_CB_EVENT_NONE;


volatile uint32_t err_flag = 0;

static void camera_callback(uint32_t event) {


    uint32_t known_events = ARM_CPI_EVENT_CAMERA_CAPTURE_STOPPED |
                            ARM_CPI_EVENT_CAMERA_FRAME_HSYNC_DETECTED |
                            ARM_CPI_EVENT_CAMERA_FRAME_VSYNC_DETECTED;
#if RTE_ISP && defined(RTE_Drivers_ISP)
    known_events |= ARM_ISP_MI_EVENT_MP_CB_WRAP_DETECTED |
                    ARM_ISP_MI_EVENT_MP_CR_WRAP_DETECTED;
#endif

    if (event & ARM_CPI_EVENT_CAMERA_CAPTURE_STOPPED) {
        g_cam_cb_events |= CAM_CB_EVENT_CAPTURE_STOPPED;
    }
#if RTE_ISP && defined(RTE_Drivers_ISP)
    known_events |= ARM_ISP_EVENT_FRAME_VSYNC_DETECTED |
                    ARM_ISP_EVENT_FRAME_IN_DETECTED |
                    ARM_ISP_MI_EVENT_MP_FRAME_END_DETECTED |
                    ARM_ISP_MI_EVENT_FILL_MP_Y_DETECTED |
                    ARM_ISP_MI_EVENT_MP_Y_WRAP_DETECTED |
                    ARM_ISP_EVENT_AWB_DONE;

    if (event & ARM_ISP_EVENT_FRAME_VSYNC_DETECTED) {
        g_cam_cb_events |= ISP_VSYNC_CB_EVENT;
    }
    if (event & ARM_ISP_EVENT_FRAME_IN_DETECTED) {
        g_cam_cb_events |= ISP_FRAME_IN_DETECTED;
    }
    if (event & ARM_ISP_MI_EVENT_MP_FRAME_END_DETECTED) {
        g_cam_cb_events |= ISP_MI_FRAME_DUMP_EVENT;
    }
    if (event & (ARM_ISP_MI_EVENT_FILL_MP_Y_DETECTED | ARM_ISP_MI_EVENT_MP_Y_WRAP_DETECTED)) {
        g_cam_cb_events |= ISP_MI_FRAME_DUMP_EVENT;
    }
#endif

    /* Explicit error events and any unrecognized event bits are treated as errors */
    if (event & ~known_events) {
        err_flag = event;
        g_cam_cb_events |= CAM_CB_EVENT_ERROR | CAM_CB_EVENT_CAPTURE_STOPPED;  // Mark error always as stopped
    }
}

int camera_init(void)
{
#if RTE_ISP && defined(RTE_Drivers_ISP)
    /* Apply project-local ISP calibration before the ISP is initialized. */
    isp_set_user_configuration();
    isp_param_set_square_crop();
#endif
    int ret = Driver_CPI.Initialize(camera_callback);
    if (ret != ARM_DRIVER_OK) {
        printf("\r\n Error: CAMERA Initialize failed.\r\n");
        return ret;
    }
    printf("\r\n CAMERA Initialized.\r\n");

    /* Power up Camera peripheral */
    ret = Driver_CPI.PowerControl(ARM_POWER_FULL);
    if (ret != ARM_DRIVER_OK) {
        printf("\r\n Error: CAMERA Power Up failed.\r\n");
        return ret;
    }

    /* Control configuration for camera controller */
    ret = Driver_CPI.Control(CPI_CONFIGURE, 0);
    if (ret != ARM_DRIVER_OK) {
        printf("\r\n Error: CPI Configuration failed.\r\n");
        return ret;
    }

    /* Control configuration for camera sensor */
    ret = Driver_CPI.Control(CPI_CAMERA_SENSOR_CONFIGURE, 0);
    if (ret != ARM_DRIVER_OK) {
        printf("\r\n Error: CAMERA SENSOR Configuration failed.\r\n");
        return ret;
    }

    /*Control configuration for camera events */
    ret = Driver_CPI.Control(CPI_EVENTS_CONFIGURE, ARM_CPI_EVENT_CAMERA_CAPTURE_STOPPED | ARM_CPI_EVENT_ERR_CAMERA_INPUT_FIFO_OVERRUN |
                                                    ARM_CPI_EVENT_ERR_CAMERA_OUTPUT_FIFO_OVERRUN | ARM_CPI_EVENT_ERR_HARDWARE);
    if (ret != ARM_DRIVER_OK) {
        printf("\r\n Error: CAMERA SENSOR Event Configuration failed.\r\n");
        return ret;
    }
#if RTE_ISP && defined(RTE_Drivers_ISP)
#if RTE_ISP && RTE_Drivers_ISP_EXPM_MODULE
    /* The driver (Ensemble 2.2.1) offsets the AE measurement window by the crop
    * offset, but the measurement runs on the already-cropped image, so the
    * window ends up outside the frame. Reset the offsets to zero. */
    {
        static struct isp_params meas_params;
        meas_params.valid_mask = ISP_PARAM_MASK_AEM;
        ret = Driver_CPI.Control(ISP_CONTROL_GET_PARAM, (uint32_t)&meas_params);
        if (ret == ARM_DRIVER_OK) {
            meas_params.aem.h_offs = 0;
            meas_params.aem.v_offs = 0;
            ret = Driver_CPI.Control(ISP_CONTROL_SET_PARAM, (uint32_t)&meas_params);
        }
        if (ret != ARM_DRIVER_OK) {
            printf("\r\n Error: ISP measurement window configuration failed.\r\n");
            return ret;
        }
    }
#endif
#endif

    printf("CPI camera Initialization Success\r\n");
    return ret;
}

int camera_capture(void* buf_addr)
{
    g_cam_cb_events = CAM_CB_EVENT_NONE;
    int ret = ARM_DRIVER_OK;

#if RTE_ISP && defined(RTE_Drivers_ISP)
    CAM_CB_EVENT callback_event = ISP_MI_FRAME_DUMP_EVENT;

    // Set up the capture buffer for the ISP
    isp_buf.index = 0;
    isp_buf.numPlanes = 2; // semi-planar
    isp_buf.imageSize = RTE_ISP_OUTPUT_WIDTH * RTE_ISP_OUTPUT_HEIGHT * 3 / 2;
    isp_buf.planes[0].dmaPhyAddr = (vsi_dma_t) buf_addr;
    isp_buf.planes[1].dmaPhyAddr = (vsi_dma_t) ((uint8_t*) buf_addr + RTE_ISP_OUTPUT_WIDTH * RTE_ISP_OUTPUT_HEIGHT);

    ret = Driver_CPI.Control(ISP_CONTROL_QBUF, (uint32_t) &isp_buf);
    if(ret != ARM_DRIVER_OK)
    {
        printf("\r\n Error: ISP buffer configuration failed.\r\n");
        return ret;
    }
 #else
     CAM_CB_EVENT callback_event = CAM_CB_EVENT_CAPTURE_STOPPED;
 #endif

    // For ISP case the argument is ignored and data goes to the buffer(s) set above
    ret = Driver_CPI.CaptureFrame(buf_addr);

    if (ret != ARM_DRIVER_OK) {
        printf("\r\n Error: CAMERA Capture Frame failed.\r\n");
        return ret;
    } else { // Wait for capture
        while (!(g_cam_cb_events & callback_event)) {
            __WFI();
        }
    }

#if RTE_ISP && defined(RTE_Drivers_ISP)
    ret = Driver_CPI.Control(ISP_PROCESS_FRAME_END, 0);
    if (ret != ARM_DRIVER_OK){
        printf("\r\n Error: ISP Process Frame End failed.\r\n");
        return ret;
    }

#if RTE_ISP && RTE_Drivers_ISP_AE_MODULE
    /* Apply the AE-computed exposure and gain back to the sensor. Without this
    * the ISP AE loop computes new values every frame but they are never
    * written to the sensor, so auto exposure never converges. */
    static uint32_t prev_int_line = 0;
    static uint32_t prev_gain_q16_16 = 0;
    struct isp_ae_cached_values ae = {0};
    ret = Driver_CPI.Control(ISP_CONTROL_AE_GET_CACHED, (uint32_t)&ae);
    if (ret == ARM_DRIVER_OK && ae.int_line != 0) {
        uint32_t gain_q16_16 = (ae.again * ae.dgain) / 16;
        if (ae.int_line != prev_int_line || gain_q16_16 != prev_gain_q16_16) {
            printf("AE: int_line=%ld, again=%ld, dgain=%ld, gain_q16_16=%ld\r\n",
                    ae.int_line, ae.again, ae.dgain, gain_q16_16);
            ret = Driver_CPI.Control(CPI_ISP_CAMERA_SENSOR_EXPOSURE, ae.int_line);
            if (ret != ARM_DRIVER_OK) {
                printf("\r\n Error: Setting camera exposure failed: %d\r\n", ret);
                return ret;
            }
            ret = Driver_CPI.Control(CPI_ISP_CAMERA_SENSOR_GAIN, gain_q16_16);
            if (ret != ARM_DRIVER_OK) {
                printf("\r\n Error: Setting camera gain failed: %d\r\n", ret);
                return ret;
            }
            prev_int_line = ae.int_line;
            prev_gain_q16_16 = gain_q16_16;
        }
    }
#endif /* RTE_ISP_AE_MODULE */
#endif

    if (g_cam_cb_events & CAM_CB_EVENT_ERROR) {
        printf("\r\n Error: g_cam_cb_events = 0x%X\r\n", g_cam_cb_events);
        ret = 0xFFFF;
    }
    return ret;
}

/* Revised matrix (BECP-1455)
* Manual WB, Illuminant 6021, Relative Red 1.42, Relative Blue 1.47
*/
#define C_RR +2.2583f
#define C_RG -0.5501f
#define C_RB -0.1248f
#define C_GR -0.1606f
#define C_GG +1.4318f
#define C_GB -0.5268f
#define C_BR -0.6317f
#define C_BG -0.0653f
#define C_BB +2.3735f

const float *camera_get_color_correction_matrix(void) {
#if defined(RTE_Drivers_CAMERA_SENSOR_ARX3A0)
    static const float ccm[9] = {C_RR, C_GR, C_BR, C_RG, C_GG, C_BG, C_RB, C_GB, C_BB};
    return ccm;
#endif
    return NULL;
}

static inline float srgb_oetf(float lum) {
    if (lum <= 0.0031308f) {
        return 12.92f * lum;
    } else {
        return 1.055f * powf(lum, 1.0f / 2.4f) - 0.055f;
    }
}

uint8_t *camera_get_gamma_lut(void) {
    static bool inited = false;
    static uint8_t lut[256];

    if (inited) {
        return lut;
    }

    for (int i = 0; i < 256; i++) {
        lut[i] = (uint8_t)(255.0f * srgb_oetf(i * (1.0f / 255.0f)) + 0.5f);
    }

    inited = true;
    return lut;
}

#endif // RTE_CPI && RTE_Drivers_CPI