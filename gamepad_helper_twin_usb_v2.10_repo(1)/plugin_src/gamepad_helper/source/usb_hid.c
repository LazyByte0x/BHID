#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "plugin_common.h"
#include "usb_hid.h"

/*
 * libSceUsbd is loaded dynamically, exactly as in jocover's usb_hid branch.
 */
static int (*sceUsbdInit)(void);
static int (*sceUsbdExit)(void);

static libusb_device_handle* (*sceUsbdOpenDeviceWithVidPid)(
    uint16_t vendorId,
    uint16_t productId
);

static int (*sceUsbdClaimInterface)(
    libusb_device_handle* deviceHandle,
    int interfaceNumber
);

static int (*sceUsbdInterruptTransfer)(
    libusb_device_handle* deviceHandle,
    uint8_t endpoint,
    uint8_t* data,
    int length,
    int* transferred,
    uint32_t timeout
);

static int (*sceUsbdReleaseInterface)(
    libusb_device_handle* deviceHandle,
    int interfaceNumber
);

static libusb_device* (*sceUsbdGetDevice)(
    libusb_device_handle* deviceHandle
);

static int (*sceUsbdGetDeviceDescriptor)(
    libusb_device* device,
    libusb_device_descriptor* desc
);

static int (*sceUsbdGetActiveConfigDescriptor)(
    libusb_device* device,
    libusb_config_descriptor** config
);

static int (*sceUsbdGetConfigDescriptor)(
    libusb_device* device,
    uint8_t configIndex,
    libusb_config_descriptor** config
);

static void (*sceUsbdFreeConfigDescriptor)(
    libusb_config_descriptor* config
);

static void (*sceUsbdClose)(
    libusb_device_handle* deviceHandle
);

#ifndef LIBUSB_ERROR_TIMEOUT
#define LIBUSB_ERROR_TIMEOUT (-7)
#endif

#ifndef LIBUSB_ERROR_NO_DEVICE
#define LIBUSB_ERROR_NO_DEVICE (-4)
#endif

#define TWIN_USB_TRANSFER_TIMEOUT_MS 250
#define TWIN_USB_MAX_PACKET_SIZE 64

static struct twin_usb_device* g_device = NULL;

static ScePadData g_pad_state[2];
static bool g_pad_valid[2];

static int32_t g_pad_handles[2] = {
    -1,
    -1
};

static bool g_usbd_initialized = false;

static pthread_mutex_t g_state_mutex = PTHREAD_MUTEX_INITIALIZER;

/* =======================================================
 * libSceUsbd
 * ======================================================= */

static void load_usbd_symbols(int h)
{
    sys_dynlib_dlsym(h, "sceUsbdInit", &sceUsbdInit);
    sys_dynlib_dlsym(h, "sceUsbdExit", &sceUsbdExit);

    sys_dynlib_dlsym(
        h,
        "sceUsbdOpenDeviceWithVidPid",
        &sceUsbdOpenDeviceWithVidPid
    );

    sys_dynlib_dlsym(
        h,
        "sceUsbdClaimInterface",
        &sceUsbdClaimInterface
    );

    sys_dynlib_dlsym(
        h,
        "sceUsbdInterruptTransfer",
        &sceUsbdInterruptTransfer
    );

    sys_dynlib_dlsym(
        h,
        "sceUsbdReleaseInterface",
        &sceUsbdReleaseInterface
    );

    sys_dynlib_dlsym(
        h,
        "sceUsbdGetDevice",
        &sceUsbdGetDevice
    );

    sys_dynlib_dlsym(
        h,
        "sceUsbdGetDeviceDescriptor",
        &sceUsbdGetDeviceDescriptor
    );

    sys_dynlib_dlsym(
        h,
        "sceUsbdGetActiveConfigDescriptor",
        &sceUsbdGetActiveConfigDescriptor
    );

    sys_dynlib_dlsym(
        h,
        "sceUsbdGetConfigDescriptor",
        &sceUsbdGetConfigDescriptor
    );

    sys_dynlib_dlsym(
        h,
        "sceUsbdFreeConfigDescriptor",
        &sceUsbdFreeConfigDescriptor
    );

    sys_dynlib_dlsym(
        h,
        "sceUsbdClose",
        &sceUsbdClose
    );
}

static bool symbols_ready(void)
{
    return sceUsbdInit &&
           sceUsbdExit &&
           sceUsbdOpenDeviceWithVidPid &&
           sceUsbdClaimInterface &&
           sceUsbdInterruptTransfer &&
           sceUsbdReleaseInterface &&
           sceUsbdGetDevice &&
           sceUsbdGetDeviceDescriptor &&
           sceUsbdGetActiveConfigDescriptor &&
           sceUsbdGetConfigDescriptor &&
           sceUsbdFreeConfigDescriptor &&
           sceUsbdClose;
}

/* =======================================================
 * DragonRise Twin USB HID mapping
 * ======================================================= */

bool parse_twin_usb_report(
    const uint8_t* hid_report,
    size_t report_len,
    ScePadData* pad_data)
{
    if (!hid_report || !pad_data || report_len < 7)
        return false;

    memset(pad_data, 0, sizeof(*pad_data));

    /*
     * Analog mapping:
     *
     * byte[1] = RX
     * byte[2] = RY
     * byte[3] = LX
     * byte[4] = LY
     */
    pad_data->rightStick.x = hid_report[1];
    pad_data->rightStick.y = hid_report[2];

    pad_data->leftStick.x = hid_report[3];
    pad_data->leftStick.y = hid_report[4];

    const uint8_t byte5 = hid_report[5];
    const uint8_t byte6 = hid_report[6];

    /*
     * Face buttons
     */
    if (byte5 & 0x10)
        pad_data->buttons |= SCE_PAD_BUTTON_TRIANGLE;

    if (byte5 & 0x20)
        pad_data->buttons |= SCE_PAD_BUTTON_CIRCLE;

    if (byte5 & 0x40)
        pad_data->buttons |= SCE_PAD_BUTTON_CROSS;

    if (byte5 & 0x80)
        pad_data->buttons |= SCE_PAD_BUTTON_SQUARE;

    /*
     * D-pad hat
     *
     * 0 = Up
     * 1 = Up + Right
     * 2 = Right
     * 3 = Down + Right
     * 4 = Down
     * 5 = Down + Left
     * 6 = Left
     * 7 = Up + Left
     */
    switch (byte5 & 0x0F) {
        case 0:
            pad_data->buttons |= SCE_PAD_BUTTON_UP;
            break;

        case 1:
            pad_data->buttons |=
                SCE_PAD_BUTTON_UP |
                SCE_PAD_BUTTON_RIGHT;
            break;

        case 2:
            pad_data->buttons |= SCE_PAD_BUTTON_RIGHT;
            break;

        case 3:
            pad_data->buttons |=
                SCE_PAD_BUTTON_DOWN |
                SCE_PAD_BUTTON_RIGHT;
            break;

        case 4:
            pad_data->buttons |= SCE_PAD_BUTTON_DOWN;
            break;

        case 5:
            pad_data->buttons |=
                SCE_PAD_BUTTON_DOWN |
                SCE_PAD_BUTTON_LEFT;
            break;

        case 6:
            pad_data->buttons |= SCE_PAD_BUTTON_LEFT;
            break;

        case 7:
            pad_data->buttons |=
                SCE_PAD_BUTTON_UP |
                SCE_PAD_BUTTON_LEFT;
            break;

        default:
            break;
    }

    /*
     * DragonRise Twin byte[6] mapping:
     *
     * 0x01 = L1
     * 0x02 = R1
     * 0x04 = L2
     * 0x08 = R2
     * 0x10 = Touch Pad
     * 0x20 = Options
     * 0x40 = L3
     * 0x80 = R3
     */
    if (byte6 & 0x01)
        pad_data->buttons |= SCE_PAD_BUTTON_L1;

    if (byte6 & 0x02)
        pad_data->buttons |= SCE_PAD_BUTTON_R1;

    if (byte6 & 0x04)
        pad_data->buttons |= SCE_PAD_BUTTON_L2;

    if (byte6 & 0x08)
        pad_data->buttons |= SCE_PAD_BUTTON_R2;

    if (byte6 & 0x10)
        pad_data->buttons |= SCE_PAD_BUTTON_TOUCH_PAD;

    if (byte6 & 0x20)
        pad_data->buttons |= SCE_PAD_BUTTON_OPTIONS;

    if (byte6 & 0x40)
        pad_data->buttons |= SCE_PAD_BUTTON_L3;

    if (byte6 & 0x80)
        pad_data->buttons |= SCE_PAD_BUTTON_R3;

    /*
     * The adapter exposes L2/R2 digitally.
     *
     * Give games a useful analogButtons value as well:
     * pressed = 255, released = 0.
     *
     * This does NOT imply that the hardware provides real
     * analog trigger levels.
     */
    pad_data->analogButtons.l2 =
        (byte6 & 0x04) ? 0xFF : 0x00;

    pad_data->analogButtons.r2 =
        (byte6 & 0x08) ? 0xFF : 0x00;

    pad_data->connected = true;
    pad_data->connectedCount = 1;
    pad_data->timestamp = sceKernelGetProcessTime();

    return true;
}

void route_twin_usb_input(
    const uint8_t* hid_report,
    size_t report_len)
{
    if (!hid_report || report_len < 7)
        return;

    const uint8_t report_id = hid_report[0];

    const int slot =
        (report_id == TWIN_REPORT_ID_PAD1) ? 0 :
        (report_id == TWIN_REPORT_ID_PAD2) ? 1 :
        -1;

    if (slot < 0)
        return;

    ScePadData parsed;

    if (!parse_twin_usb_report(
            hid_report,
            report_len,
            &parsed)) {
        return;
    }

    pthread_mutex_lock(&g_state_mutex);

    g_pad_state[slot] = parsed;
    g_pad_valid[slot] = true;

    pthread_mutex_unlock(&g_state_mutex);
}

/* =======================================================
 * State management
 * ======================================================= */

static void mark_pads_disconnected(void)
{
    pthread_mutex_lock(&g_state_mutex);

    g_pad_valid[0] = false;
    g_pad_valid[1] = false;

    memset(
        &g_pad_state[0],
        0,
        sizeof(g_pad_state[0])
    );

    memset(
        &g_pad_state[1],
        0,
        sizeof(g_pad_state[1])
    );

    /*
     * Important:
     * Do NOT clear g_pad_handles here.
     *
     * A USB disconnect invalidates the cached input state,
     * but the pad handle should remain associated with Twin
     * until scePadClose / usb_hid_exit.
     */
    pthread_mutex_unlock(&g_state_mutex);
}

/* =======================================================
 * USB input thread
 * ======================================================= */

static void* twin_usb_read_thread(void* arg)
{
    struct twin_usb_device* dev =
        (struct twin_usb_device*)arg;

    uint8_t buffer[TWIN_USB_MAX_PACKET_SIZE];

    while (!dev->stop_thread) {
        int transferred = 0;

        int packet_size =
            dev->input_ep_max_packet_size;

        if (packet_size <= 0 ||
            packet_size > (int)sizeof(buffer)) {

            packet_size = (int)sizeof(buffer);
        }

        int ret = sceUsbdInterruptTransfer(
            dev->handle,
            (uint8_t)dev->input_endpoint,
            buffer,
            packet_size,
            &transferred,
            TWIN_USB_TRANSFER_TIMEOUT_MS
        );

        /*
         * Timeout is normal during idle periods.
         */
        if (ret == LIBUSB_ERROR_TIMEOUT)
            continue;

        /*
         * Any other negative result is a transfer error.
         */
        if (ret < 0) {
            if (!dev->stop_thread) {
                if (ret == LIBUSB_ERROR_NO_DEVICE) {
                    final_printf(
                        "[TwinUSB] USB device disconnected\n"
                    );
                } else {
                    final_printf(
                        "[TwinUSB] interrupt transfer failed: %d\n",
                        ret
                    );
                }

                /*
                 * Keep the pad handles bound, but invalidate
                 * their cached controller state.
                 */
                mark_pads_disconnected();

                dev->stop_thread = 1;
            }

            break;
        }

        if (transferred > 0) {
            route_twin_usb_input(
                buffer,
                (size_t)transferred
            );
        }
    }

    return NULL;
}

/* =======================================================
 * Endpoint discovery
 * ======================================================= */

static int find_input_endpoint(
    libusb_config_descriptor* config,
    int* interface_number,
    int* endpoint,
    int* packet_size)
{
    if (!config ||
        !interface_number ||
        !endpoint ||
        !packet_size) {

        return -1;
    }

    for (int i = 0;
         i < config->bNumInterfaces;
         ++i) {

        libusb_interface* intf =
            &config->interface[i];

        for (int j = 0;
             j < intf->num_altsetting;
             ++j) {

            libusb_interface_descriptor* desc =
                &intf->altsetting[j];

            /*
             * HID interface class.
             */
            if (desc->bInterfaceClass != 3)
                continue;

            for (int k = 0;
                 k < desc->bNumEndpoints;
                 ++k) {

                libusb_endpoint_descriptor* ep =
                    &desc->endpoint[k];

                const bool interrupt =
                    (ep->bmAttributes &
                     LIBUSB_TRANSFER_TYPE_MASK) ==
                    LIBUSB_TRANSFER_TYPE_INTERRUPT;

                const bool input =
                    (ep->bEndpointAddress &
                     LIBUSB_ENDPOINT_DIR_MASK) ==
                    LIBUSB_ENDPOINT_IN;

                if (interrupt && input) {
                    *interface_number =
                        desc->bInterfaceNumber;

                    *endpoint =
                        ep->bEndpointAddress;

                    *packet_size =
                        ep->wMaxPacketSize;

                    return 0;
                }
            }
        }
    }

    return -1;
}

/* =======================================================
 * Initialization
 * ======================================================= */

int usb_hid_init(struct twin_usb_device* device)
{
    if (!device)
        return -1;

    memset(
        device,
        0,
        sizeof(*device)
    );

    device->input_endpoint = -1;
    device->interface = -1;
    device->thread_started = false;
    device->stop_thread = 0;

    g_device = NULL;
    g_usbd_initialized = false;

    char module[256];

    int h = 0;

    snprintf(
        module,
        sizeof(module),
        "/%s/common/lib/%s",
        sceKernelGetFsSandboxRandomWord(),
        "libSceUsbd.sprx"
    );

    if (sys_dynlib_load_prx(module, &h) < 0) {
        final_printf(
            "[TwinUSB] failed to load libSceUsbd.sprx\n"
        );

        return -1;
    }

    load_usbd_symbols(h);

    if (!symbols_ready()) {
        final_printf(
            "[TwinUSB] failed to resolve libSceUsbd symbols\n"
        );

        return -1;
    }

    int ret = sceUsbdInit();

    if (ret < 0) {
        final_printf(
            "[TwinUSB] sceUsbdInit failed: %d\n",
            ret
        );

        return -1;
    }

    g_usbd_initialized = true;

    device->handle =
        sceUsbdOpenDeviceWithVidPid(
            TWIN_USB_VID,
            TWIN_USB_PID
        );

    if (!device->handle) {
        final_printf(
            "[TwinUSB] DragonRise %04x:%04x not found\n",
            TWIN_USB_VID,
            TWIN_USB_PID
        );

        sceUsbdExit();
        g_usbd_initialized = false;

        return -1;
    }

    /*
     * Get the underlying USB device object once and reuse it.
     */
    libusb_device* raw_device =
        sceUsbdGetDevice(device->handle);

    if (!raw_device) {
        final_printf(
            "[TwinUSB] failed to get USB device object\n"
        );

        sceUsbdClose(device->handle);
        device->handle = NULL;

        sceUsbdExit();
        g_usbd_initialized = false;

        return -1;
    }

    /* ---------------------------------------------------
     * USB configuration
     * --------------------------------------------------- */

    libusb_config_descriptor* config = NULL;

    ret = sceUsbdGetActiveConfigDescriptor(
        raw_device,
        &config
    );

    if (ret < 0) {
        ret = sceUsbdGetConfigDescriptor(
            raw_device,
            0,
            &config
        );
    }

    if (ret < 0 || !config) {
        final_printf(
            "[TwinUSB] failed to get USB configuration: %d\n",
            ret
        );

        sceUsbdClose(device->handle);
        device->handle = NULL;

        sceUsbdExit();
        g_usbd_initialized = false;

        return -1;
    }

    /* ---------------------------------------------------
     * USB device descriptor
     * --------------------------------------------------- */

    libusb_device_descriptor desc;

    memset(
        &desc,
        0,
        sizeof(desc)
    );

    ret = sceUsbdGetDeviceDescriptor(
        raw_device,
        &desc
    );

    if (ret < 0) {
        final_printf(
            "[TwinUSB] failed to read USB device descriptor: %d\n",
            ret
        );

        sceUsbdFreeConfigDescriptor(config);

        sceUsbdClose(device->handle);
        device->handle = NULL;

        sceUsbdExit();
        g_usbd_initialized = false;

        return -1;
    }

    final_printf(
        "[TwinUSB] device VID=%04x PID=%04x\n",
        desc.idVendor,
        desc.idProduct
    );

    /*
     * The device was already opened by fixed VID/PID.
     * Keep a second sanity check anyway.
     */
    if (desc.idVendor != TWIN_USB_VID ||
        desc.idProduct != TWIN_USB_PID) {

        final_printf(
            "[TwinUSB] unexpected USB device %04x:%04x\n",
            desc.idVendor,
            desc.idProduct
        );

        sceUsbdFreeConfigDescriptor(config);

        sceUsbdClose(device->handle);
        device->handle = NULL;

        sceUsbdExit();
        g_usbd_initialized = false;

        return -1;
    }

    /* ---------------------------------------------------
     * Find HID interrupt IN endpoint
     * --------------------------------------------------- */

    ret = find_input_endpoint(
        config,
        &device->interface,
        &device->input_endpoint,
        &device->input_ep_max_packet_size
    );

    sceUsbdFreeConfigDescriptor(config);

    if (ret < 0 ||
        device->interface < 0 ||
        device->input_endpoint < 0) {

        final_printf(
            "[TwinUSB] no interrupt IN endpoint found\n"
        );

        sceUsbdClose(device->handle);
        device->handle = NULL;

        sceUsbdExit();
        g_usbd_initialized = false;

        return -1;
    }

    /* ---------------------------------------------------
     * Claim HID interface
     * --------------------------------------------------- */

    ret = sceUsbdClaimInterface(
        device->handle,
        device->interface
    );

    if (ret < 0) {
        final_printf(
            "[TwinUSB] claim interface %d failed: %d\n",
            device->interface,
            ret
        );

        sceUsbdClose(device->handle);
        device->handle = NULL;

        sceUsbdExit();
        g_usbd_initialized = false;

        return -1;
    }

    /*
     * Keep transfers bounded by our local read buffer.
     */
    if (device->input_ep_max_packet_size <= 0 ||
        device->input_ep_max_packet_size >
            TWIN_USB_MAX_PACKET_SIZE) {

        device->input_ep_max_packet_size =
            TWIN_USB_MAX_PACKET_SIZE;
    }

    device->stop_thread = 0;
    device->thread_started = false;

    g_device = device;

    /*
     * Reset controller state.
     *
     * This intentionally does not clear pad handles.
     * Handles are bound later by scePadOpen.
     */
    mark_pads_disconnected();

    final_printf(
        "[TwinUSB] interface=%d input_ep=0x%02x packet=%d\n",
        device->interface,
        device->input_endpoint,
        device->input_ep_max_packet_size
    );

    /* ---------------------------------------------------
     * Start USB read thread
     * --------------------------------------------------- */

    if (pthread_create(
            &device->thread,
            NULL,
            twin_usb_read_thread,
            device) != 0) {

        final_printf(
            "[TwinUSB] failed to create read thread\n"
        );

        sceUsbdReleaseInterface(
            device->handle,
            device->interface
        );

        sceUsbdClose(device->handle);
        device->handle = NULL;

        sceUsbdExit();
        g_usbd_initialized = false;

        g_device = NULL;

        return -1;
    }

    device->thread_started = true;

    final_printf(
        "[TwinUSB] DragonRise HID ready "
        "(Report 0x01=P1, 0x02=P2)\n"
    );

    return 0;
}

/* =======================================================
 * Handle binding
 * ======================================================= */

void usb_hid_bind_handle(
    int32_t handle,
    int player_index)
{
    if (handle < 0 ||
        player_index < 0 ||
        player_index > 1) {
        return;
    }

    pthread_mutex_lock(&g_state_mutex);

    g_pad_handles[player_index] = handle;

    pthread_mutex_unlock(&g_state_mutex);
}

void usb_hid_unbind_handle(
    int32_t handle)
{
    if (handle < 0)
        return;

    pthread_mutex_lock(&g_state_mutex);

    for (int i = 0; i < 2; ++i) {
        if (g_pad_handles[i] == handle)
            g_pad_handles[i] = -1;
    }

    pthread_mutex_unlock(&g_state_mutex);
}

static int slot_for_handle_locked(int32_t handle)
{
    if (g_pad_handles[0] == handle)
        return 0;

    if (g_pad_handles[1] == handle)
        return 1;

    return -1;
}

/* =======================================================
 * Read pad state
 * ======================================================= */

int usb_hid_get_pad(
    int32_t handle,
    ScePadData* pData)
{
    if (!g_device ||
        !g_device->handle ||
        !pData) {

        return -1;
    }

    pthread_mutex_lock(&g_state_mutex);

    const int slot =
        slot_for_handle_locked(handle);

    if (slot < 0) {
        pthread_mutex_unlock(&g_state_mutex);
        return -1;
    }

    if (g_pad_valid[slot]) {
        *pData = g_pad_state[slot];

        pthread_mutex_unlock(&g_state_mutex);

        return 0;
    }

    pthread_mutex_unlock(&g_state_mutex);

    /*
     * No valid cached report yet / controller disconnected.
     */
    memset(
        pData,
        0,
        sizeof(*pData)
    );

    pData->connected = false;
    pData->connectedCount = 0;

    return 0;
}

/* =======================================================
 * Shutdown
 * ======================================================= */

int usb_hid_exit(
    struct twin_usb_device* device)
{
    if (!device)
        return 0;

    device->stop_thread = 1;

    /*
     * InterruptTransfer has a 250 ms timeout,
     * so the thread will wake and terminate.
     */
    if (device->thread_started) {
        pthread_join(
            device->thread,
            NULL
        );

        device->thread_started = false;
    }

    /*
     * Clear cached controller state first.
     * Handles remain bound until explicitly cleared below.
     */
    mark_pads_disconnected();

    pthread_mutex_lock(&g_state_mutex);

    g_pad_handles[0] = -1;
    g_pad_handles[1] = -1;

    pthread_mutex_unlock(&g_state_mutex);

    if (device->handle &&
        device->interface >= 0) {

        sceUsbdReleaseInterface(
            device->handle,
            device->interface
        );
    }

    if (device->handle) {
        sceUsbdClose(
            device->handle
        );

        device->handle = NULL;
    }

    if (g_usbd_initialized &&
        sceUsbdExit) {

        sceUsbdExit();
        g_usbd_initialized = false;
    }

    g_device = NULL;

    return 0;
}

/* =======================================================
 * Twin handle check
 * ======================================================= */

bool usb_hid_is_twin_handle(
    int32_t handle)
{
    if (handle < 0)
        return false;

    pthread_mutex_lock(&g_state_mutex);

    const bool is_twin =
        (g_pad_handles[0] == handle ||
         g_pad_handles[1] == handle);

    pthread_mutex_unlock(&g_state_mutex);

    return is_twin;
}