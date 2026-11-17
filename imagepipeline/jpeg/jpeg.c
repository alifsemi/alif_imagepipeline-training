
#include "RTE_Components.h"
#include "alif.h"

#if RTE_JPEG && defined(RTE_Drivers_JPEG)

#include "RTE_Device.h"

#include "Driver_JPEG.h"

#include <stdio.h>


extern ARM_DRIVER_JPEG Driver_JPEG;

static volatile bool jpeg_done;
static volatile uint32_t jpeg_error;

static void jpeg_callback(uint32_t event)
{
    if (event & ARM_JPEG_EVENT_FRAME_READY) {
        jpeg_done = true;
    }
    if ((event & ARM_JPEG_EVENT_BUFFER_FULL) ||
        (event & ARM_JPEG_EVENT_BUS_ERROR)   ||
        (event & ARM_JPEG_EVENT_TIMEOUT)) {
        jpeg_done = false;
        jpeg_error = event;
    }
}

int jpeg_enc_init(void)
{
    int ret = 0;
    ret = Driver_JPEG.Initialize(jpeg_callback);
    if(ret != ARM_DRIVER_OK)
    {
        printf("JPEG Init fails.\r\n");
        return ret;
    }

    Driver_JPEG.PowerControl(ARM_POWER_FULL);
    if(ret != ARM_DRIVER_OK)
        printf("JPEG PowerControl fails.\r\n");

    // Initialize JPEG configuration instance.
    ARM_JPEG_FORMAT fmt;
    fmt.pixel_format        = ARM_JPEG_FORMAT_NV12;
    fmt.pitch               = RTE_ISP_OUTPUT_WIDTH;
    fmt.width               = RTE_ISP_OUTPUT_WIDTH;
    fmt.height              = RTE_ISP_OUTPUT_HEIGHT;

    ret = Driver_JPEG.Control(ARM_JPEG_CTRL_SET_FORMAT, (uint32_t) &fmt);
    if(ret != ARM_DRIVER_OK) {
        printf("JPEG Control fails.\r\n");
    }

    // Configure quality factor.
    ret = Driver_JPEG.Control(ARM_JPEG_CTRL_SET_QUALITY, 80);
    if(ret != ARM_DRIVER_OK) {
        printf("JPEG Control fails.\r\n");
    }

    return ret;
}

// define JPEG output buffer object
ARM_JPEG_BUFFER jpeg_buffer_output  =
{
    .buf    = NULL,
    // NV12 format, x1.5 (full size luma, quarter size each chroma)
    .size   = RTE_ISP_OUTPUT_WIDTH*RTE_ISP_OUTPUT_HEIGHT*3/2,
};

int jpeg_compress(void* in_buf, void* out_buf, uint32_t out_size)
{
    uint32_t ret = 0;

    jpeg_done = false;
    jpeg_error = 0;

    // define JPEG input buffer object
    static ARM_JPEG_INPUT_BUFFER        jpeg_buffer_input;
    jpeg_buffer_input.num_planes        = 3;
    jpeg_buffer_input.plane[0]          = in_buf;
    jpeg_buffer_input.plane[1]          = (void*) ((uint8_t*) in_buf + RTE_ISP_OUTPUT_WIDTH*RTE_ISP_OUTPUT_HEIGHT);
    jpeg_buffer_input.plane_pitch[0]    = RTE_ISP_OUTPUT_WIDTH;
    jpeg_buffer_input.plane_pitch[1]    = RTE_ISP_OUTPUT_WIDTH;

    // reset the output buffer size to avoid buffer full error
    jpeg_buffer_output.buf              = out_buf;
    jpeg_buffer_output.size             = out_size;

    // Start the encoding.
    ret = Driver_JPEG.Start(&jpeg_buffer_input, &jpeg_buffer_output);
    if(ret != ARM_DRIVER_OK){
        printf("JPEG Control fails.\r\n");
        return ARM_DRIVER_ERROR;
    }

    // Wait for encoding to complete.
    while (false == jpeg_done)
    {
        __WFI();
        if(0 < jpeg_error){
            printf("<<JPEG failed with error code %ld.\r\n", jpeg_error);
            return ARM_DRIVER_ERROR;
        }
    }

    return ret;
}

size_t jpeg_get_output_size(void)
{
    return (size_t) jpeg_buffer_output.size;
}

#endif // RTE_JPEG && defined(RTE_Drivers_JPEG)