#include "usb.h"
#include "xhci.h"
#include "heap.h"

// USB devices storage
static UsbDeviceInfo usb_devices[USB_MAX_DEVICES];
static int usb_device_count = 0;

// Parse configuration descriptor to find interfaces and endpoints
static bool usb_parse_config(uint8_t slot_id, UsbDeviceInfo* dev, uint8_t* config_data, uint16_t total_length) {
    uint16_t offset = 0;
    UsbInterfaceDescriptor* current_iface = nullptr;
    
    while (offset < total_length) {
        uint8_t length = config_data[offset];
        uint8_t type = config_data[offset + 1];
        
        if (length == 0) break;  // Malformed descriptor
        
        if (type == USB_DESC_INTERFACE) {
            current_iface = (UsbInterfaceDescriptor*)&config_data[offset];
            
            // Check for HID boot keyboard
            if (current_iface->bInterfaceClass == USB_CLASS_HID &&
                current_iface->bInterfaceSubClass == USB_SUBCLASS_BOOT) {
                if (current_iface->bInterfaceProtocol == USB_PROTOCOL_KEYBOARD) {
                    dev->is_keyboard = true;
                    dev->hid_interface = current_iface->bInterfaceNumber;
                } else if (current_iface->bInterfaceProtocol == USB_PROTOCOL_MOUSE) {
                    dev->is_mouse = true;
                    dev->hid_interface = current_iface->bInterfaceNumber;
                }
            }
        } else if (type == USB_DESC_ENDPOINT && current_iface) {
            UsbEndpointDescriptor* ep = (UsbEndpointDescriptor*)&config_data[offset];
            
            // Look for interrupt IN endpoint for HID
            if ((current_iface->bInterfaceClass == USB_CLASS_HID) &&
                (ep->bEndpointAddress & USB_ENDPOINT_DIR_IN) &&
                ((ep->bmAttributes & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_INTERRUPT)) {
                
                dev->hid_max_packet = ep->wMaxPacketSize;
                dev->hid_interval = ep->bInterval;
                
                // Calculate xHCI endpoint index (DCI - Device Context Index)
                // EP0 = DCI 1, EP1 OUT = DCI 2, EP1 IN = DCI 3, etc.
                uint8_t ep_num = ep->bEndpointAddress & 0x0F;
                uint8_t ep_dir = (ep->bEndpointAddress & USB_ENDPOINT_DIR_IN) ? 1 : 0;
                uint8_t xhci_ep = ep_num * 2 + ep_dir;  // This is the DCI
                
                // Store the xHCI endpoint DCI for later configuration
                dev->hid_endpoint = xhci_ep;
                
                // DON'T configure endpoint yet - device must be SET_CONFIGURATION first!
                // Will be configured after SET_CONFIGURATION
            }
        }
        
        offset += length;
    }
    
    return true;
}

int usb_enumerate_device(uint8_t port) {
    if (usb_device_count >= USB_MAX_DEVICES) {
        return -1;
    }
    
    // Reset port
    if (!xhci_reset_port(port)) {
        return -1;
    }
    
    // Get port speed
    uint8_t speed = xhci_get_port_speed(port);
    if (speed == 0) {
        return -1;
    }
    
    // Enable slot
    int slot_id = xhci_enable_slot();
    if (slot_id < 0) {
        return -1;
    }
    
    // Address device
    if (!xhci_address_device(slot_id, port, speed)) {
        xhci_disable_slot(slot_id);
        return -1;
    }
    
    // Get device descriptor
    UsbDeviceDescriptor dev_desc;
    if (!usb_get_device_descriptor(slot_id, &dev_desc)) {
        xhci_disable_slot(slot_id);
        return -1;
    }
    
    // Create device info
    UsbDeviceInfo* dev = &usb_devices[usb_device_count];
    dev->slot_id = slot_id;
    dev->port = port;
    dev->speed = speed;
    dev->vendor_id = dev_desc.idVendor;
    dev->product_id = dev_desc.idProduct;
    dev->device_class = dev_desc.bDeviceClass;
    dev->device_subclass = dev_desc.bDeviceSubClass;
    dev->device_protocol = dev_desc.bDeviceProtocol;
    dev->is_keyboard = false;
    dev->is_mouse = false;
    dev->configured = false;
    
    // Get configuration descriptor (first 9 bytes to get total length)
    uint8_t config_header[9];
    if (!usb_get_config_descriptor(slot_id, 0, config_header, 9)) {
        xhci_disable_slot(slot_id);
        return -1;
    }
    
    UsbConfigDescriptor* config_desc = (UsbConfigDescriptor*)config_header;
    uint16_t total_length = config_desc->wTotalLength;
    
    // Get full configuration descriptor
    uint8_t* full_config = (uint8_t*)malloc(total_length);
    if (!full_config) {
        xhci_disable_slot(slot_id);
        return -1;
    }
    
    if (!usb_get_config_descriptor(slot_id, 0, full_config, total_length)) {
        free(full_config);
        xhci_disable_slot(slot_id);
        return -1;
    }
    
    // Parse configuration
    dev->config_value = config_desc->bConfigurationValue;
    dev->num_interfaces = config_desc->bNumInterfaces;
    usb_parse_config(slot_id, dev, full_config, total_length);
    
    free(full_config);
    
    // Set configuration
    if (!usb_set_configuration(slot_id, dev->config_value)) {
        xhci_disable_slot(slot_id);
        return -1;
    }
    
    // NOW configure HID endpoint in xHCI (after device is configured)
    if (dev->hid_endpoint != 0) {
        // EP type for interrupt IN = 7
        xhci_configure_endpoint(slot_id, dev->hid_endpoint, 7,
                               dev->hid_max_packet, dev->hid_interval);
    }
    
    dev->configured = true;
    usb_device_count++;
    
    return usb_device_count - 1;
}

bool usb_get_device_descriptor(uint8_t slot_id, UsbDeviceDescriptor* desc) {
    uint16_t transferred;
    return xhci_control_transfer(
        slot_id,
        USB_REQ_DEVICE_TO_HOST | USB_REQ_TYPE_STANDARD | USB_REQ_RECIPIENT_DEVICE,
        USB_REQ_GET_DESCRIPTOR,
        (USB_DESC_DEVICE << 8) | 0,
        0,
        sizeof(UsbDeviceDescriptor),
        desc,
        &transferred
    );
}

bool usb_get_config_descriptor(uint8_t slot_id, uint8_t index, void* buffer, uint16_t size) {
    uint16_t transferred;
    return xhci_control_transfer(
        slot_id,
        USB_REQ_DEVICE_TO_HOST | USB_REQ_TYPE_STANDARD | USB_REQ_RECIPIENT_DEVICE,
        USB_REQ_GET_DESCRIPTOR,
        (USB_DESC_CONFIGURATION << 8) | index,
        0,
        size,
        buffer,
        &transferred
    );
}

bool usb_set_configuration(uint8_t slot_id, uint8_t config_value) {
    uint16_t transferred;
    return xhci_control_transfer(
        slot_id,
        USB_REQ_HOST_TO_DEVICE | USB_REQ_TYPE_STANDARD | USB_REQ_RECIPIENT_DEVICE,
        USB_REQ_SET_CONFIGURATION,
        config_value,
        0,
        0,
        nullptr,
        &transferred
    );
}

int usb_get_device_count() {
    return usb_device_count;
}

UsbDeviceInfo* usb_get_device(int index) {
    if (index < 0 || index >= usb_device_count) return nullptr;
    return &usb_devices[index];
}

UsbDeviceInfo* usb_find_keyboard() {
    for (int i = 0; i < usb_device_count; i++) {
        if (usb_devices[i].is_keyboard && usb_devices[i].configured) {
            return &usb_devices[i];
        }
    }
    return nullptr;
}

UsbDeviceInfo* usb_find_mouse() {
    for (int i = 0; i < usb_device_count; i++) {
        if (usb_devices[i].is_mouse && usb_devices[i].configured) {
            return &usb_devices[i];
        }
    }
    return nullptr;
}

void usb_poll() {
    xhci_poll_events();
}

void usb_init() {
    usb_device_count = 0;
    
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        usb_devices[i].slot_id = 0;
        usb_devices[i].configured = false;
    }
    
    // Initialize xHCI
    if (!xhci_init()) {
        return;
    }
    
    // Scan all ports for connected devices
    uint8_t max_ports = xhci_get_max_ports();
    for (uint8_t port = 1; port <= max_ports; port++) {
        if (xhci_port_connected(port)) {
            usb_enumerate_device(port);
        }
    }
}

