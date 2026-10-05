#ifndef _USB_HID_H
#define _USB_HID_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include "pad.h"

#define TWIN_USB_VID        0x0810
#define TWIN_USB_PID        0x0001
#define TWIN_REPORT_ID_PAD1 0x01
#define TWIN_REPORT_ID_PAD2 0x02

#define LIBUSB_TRANSFER_TYPE_MASK      0x03
#define LIBUSB_ENDPOINT_DIR_MASK       0x80
#define LIBUSB_TRANSFER_TYPE_INTERRUPT 3U
#define LIBUSB_ENDPOINT_IN             0x80

#ifndef LIBUSB_ERROR_NO_DEVICE
#define LIBUSB_ERROR_NO_DEVICE (-4)
#endif

#ifndef LIBUSB_ERROR_TIMEOUT
#define LIBUSB_ERROR_TIMEOUT (-7)
#endif

/* Transparent descriptors used by libSceUsbd, matching the layout used by
 * jocover's usb_hid implementation. */
typedef struct libusb_device libusb_device;
typedef struct libusb_device_handle libusb_device_handle;

typedef struct libusb_endpoint_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
    uint8_t bRefresh;
    uint8_t bSynchAddress;
    uint8_t* extra;
    int extra_length;
} libusb_endpoint_descriptor;

typedef struct libusb_device_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} libusb_device_descriptor;

typedef struct libusb_interface_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
    libusb_endpoint_descriptor* endpoint;
    const unsigned char* extra;
    int extra_length;
} libusb_interface_descriptor;

typedef struct libusb_interface {
    libusb_interface_descriptor* altsetting;
    int num_altsetting;
} libusb_interface;

typedef struct libusb_config_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t MaxPower;
    libusb_interface* interface;
    unsigned char* extra;
    int extra_length;
} libusb_config_descriptor;

struct twin_usb_device {
    libusb_device_handle* handle;
    int interface;
    int input_endpoint;
    int input_ep_max_packet_size;
    volatile int stop_thread;
    bool thread_started;
    pthread_t thread;
};

int usb_hid_init(struct twin_usb_device* device);
int usb_hid_exit(struct twin_usb_device* device);
int usb_hid_get_pad(int32_t handle, ScePadData* pData);
void usb_hid_bind_handle(int32_t handle, int player_index);
void usb_hid_unbind_handle(int32_t handle);

/* Mapping recovered from the user's actual DragonRise Twin USB HID reports. */
bool parse_twin_usb_report(const uint8_t* hid_report, size_t report_len, ScePadData* pad_data);
void route_twin_usb_input(const uint8_t* hid_report, size_t report_len);

bool usb_hid_is_twin_handle(int32_t handle);

#endif
