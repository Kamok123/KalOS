#include "usb_hid.h"
#include "usb.h"
#include "xhci.h"
#include "io.h"
#include <stddef.h>

// Keyboard state
static bool keyboard_available = false;
static UsbDeviceInfo* keyboard_device = nullptr;
static HidKeyboardReport last_keyboard_report = {0};

// Keyboard buffer
#define KB_BUFFER_SIZE 256
static char kb_buffer[KB_BUFFER_SIZE];
static volatile uint8_t kb_buffer_start = 0;
static volatile uint8_t kb_buffer_end = 0;

// Mouse state
static bool mouse_available = false;
static UsbDeviceInfo* mouse_device = nullptr;
static int32_t mouse_x = 0;
static int32_t mouse_y = 0;
static bool mouse_left = false;
static bool mouse_right = false;
static bool mouse_middle = false;

// Screen dimensions (set during init)
static int32_t screen_width = 1024;
static int32_t screen_height = 768;

// HID keycode to ASCII conversion table (US keyboard layout)
// Index = HID keycode, value = ASCII (lowercase)
static const char hid_to_ascii[128] = {
    0,    0,    0,    0,   'a',  'b',  'c',  'd',   // 0x00-0x07
    'e',  'f',  'g',  'h',  'i',  'j',  'k',  'l',   // 0x08-0x0F
    'm',  'n',  'o',  'p',  'q',  'r',  's',  't',   // 0x10-0x17
    'u',  'v',  'w',  'x',  'y',  'z',  '1',  '2',   // 0x18-0x1F
    '3',  '4',  '5',  '6',  '7',  '8',  '9',  '0',   // 0x20-0x27
    '\n', 27,   '\b', '\t', ' ',  '-',  '=',  '[',   // 0x28-0x2F (Enter, Esc, Backspace, Tab, Space)
    ']',  '\\', '#',  ';',  '\'', '`',  ',',  '.',   // 0x30-0x37
    '/',  0,    0,    0,    0,    0,    0,    0,     // 0x38-0x3F (CapsLock, F1-F6)
    0,    0,    0,    0,    0,    0,    0,    0,     // 0x40-0x47 (F7-F12, PrintScreen, ScrollLock)
    0,    0,    0,    0,    0,    0,    0,    0,     // 0x48-0x4F (Pause, Insert, Home, PageUp, Delete, End, PageDown, Right)
    0,    0,    0,    0,    '/',  '*',  '-',  '+',   // 0x50-0x57 (Left, Down, Up, NumLock, Keypad /,*,-,+)
    '\n', '1',  '2',  '3',  '4',  '5',  '6',  '7',   // 0x58-0x5F (Keypad Enter, 1-7)
    '8',  '9',  '0',  '.',  0,    0,    0,    '=',   // 0x60-0x67 (Keypad 8-0, ., International, App, Power, Keypad =)
    0,    0,    0,    0,    0,    0,    0,    0,     // 0x68-0x6F
    0,    0,    0,    0,    0,    0,    0,    0,     // 0x70-0x77
    0,    0,    0,    0,    0,    0,    0,    0      // 0x78-0x7F
};

// Shifted ASCII
static const char hid_to_ascii_shift[128] = {
    0,    0,    0,    0,   'A',  'B',  'C',  'D',   // 0x00-0x07
    'E',  'F',  'G',  'H',  'I',  'J',  'K',  'L',   // 0x08-0x0F
    'M',  'N',  'O',  'P',  'Q',  'R',  'S',  'T',   // 0x10-0x17
    'U',  'V',  'W',  'X',  'Y',  'Z',  '!',  '@',   // 0x18-0x1F
    '#',  '$',  '%',  '^',  '&',  '*',  '(',  ')',   // 0x20-0x27
    '\n', 27,   '\b', '\t', ' ',  '_',  '+',  '{',   // 0x28-0x2F
    '}',  '|',  '~',  ':',  '"',  '~',  '<',  '>',   // 0x30-0x37
    '?',  0,    0,    0,    0,    0,    0,    0,     // 0x38-0x3F
    0,    0,    0,    0,    0,    0,    0,    0,     // 0x40-0x47
    0,    0,    0,    0,    0,    0,    0,    0,     // 0x48-0x4F
    0,    0,    0,    0,    '/',  '*',  '-',  '+',   // 0x50-0x57
    '\n', '1',  '2',  '3',  '4',  '5',  '6',  '7',   // 0x58-0x5F
    '8',  '9',  '0',  '.',  0,    0,    0,    '=',   // 0x60-0x67
    0,    0,    0,    0,    0,    0,    0,    0,     // 0x68-0x6F
    0,    0,    0,    0,    0,    0,    0,    0,     // 0x70-0x77
    0,    0,    0,    0,    0,    0,    0,    0      // 0x78-0x7F
};

// Check if a key was in the previous report (for detecting new key presses)
static bool key_was_pressed(uint8_t keycode) {
    for (int i = 0; i < 6; i++) {
        if (last_keyboard_report.keys[i] == keycode) {
            return true;
        }
    }
    return false;
}

// Add a character to the keyboard buffer
static void kb_buffer_push(char c) {
    uint8_t next = (kb_buffer_end + 1) % KB_BUFFER_SIZE;
    if (next != kb_buffer_start) {
        kb_buffer[kb_buffer_end] = c;
        kb_buffer_end = next;
    }
}

// Key repeat state
static uint8_t repeat_keycode = 0;
static uint32_t repeat_counter = 0;
static const uint32_t REPEAT_DELAY = 30;   // Polls before repeat starts
static const uint32_t REPEAT_RATE = 5;      // Polls between repeats

// Process a keyboard report
static void process_keyboard_report(HidKeyboardReport* report) {
    bool shift = (report->modifiers & (HID_MOD_LEFT_SHIFT | HID_MOD_RIGHT_SHIFT)) != 0;
    
    // Check for newly pressed keys and handle repeats
    uint8_t current_key = 0;
    for (int i = 0; i < 6; i++) {
        uint8_t keycode = report->keys[i];
        if (keycode != 0 && keycode < 128) {
            current_key = keycode;
            break;  // Use first valid key for repeat tracking
        }
    }
    
    // Check all keys for new presses
    for (int i = 0; i < 6; i++) {
        uint8_t keycode = report->keys[i];
        
        if (keycode == 0) continue;  // No key
        if (keycode >= 128) continue; // Invalid
        
        
        // Only process if this is a new key press
        if (!key_was_pressed(keycode)) {
            char c;
            if (shift) {
                c = hid_to_ascii_shift[keycode];
            } else {
                c = hid_to_ascii[keycode];
            }
            
            if (c != 0) {
                kb_buffer_push(c);
            }
            
            // Start repeat tracking for this key
            repeat_keycode = keycode;
            repeat_counter = 0;
        }
    }
    
    // Handle key repeat for held key
    if (current_key != 0 && current_key == repeat_keycode) {
        repeat_counter++;
        if (repeat_counter >= REPEAT_DELAY) {
            // After initial delay, repeat at rate
            if ((repeat_counter - REPEAT_DELAY) % REPEAT_RATE == 0) {
                char c;
                if (shift) {
                    c = hid_to_ascii_shift[current_key];
                } else {
                    c = hid_to_ascii[current_key];
                }
                if (c != 0) {
                    kb_buffer_push(c);
                }
            }
        }
    } else if (current_key == 0) {
        // Key released
        repeat_keycode = 0;
        repeat_counter = 0;
    } else {
        // Different key - reset repeat
        repeat_keycode = current_key;
        repeat_counter = 0;
    }
    
    // Save report for next comparison
    last_keyboard_report = *report;
}

// Process a mouse report
static void process_mouse_report(HidMouseReport* report) {
    // Update button states
    mouse_left = (report->buttons & HID_MOUSE_LEFT) != 0;
    mouse_right = (report->buttons & HID_MOUSE_RIGHT) != 0;
    mouse_middle = (report->buttons & HID_MOUSE_MIDDLE) != 0;
    
    // Update position
    mouse_x += report->x;
    mouse_y += report->y;
    
    // Clamp to screen bounds
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= screen_width) mouse_x = screen_width - 1;
    if (mouse_y >= screen_height) mouse_y = screen_height - 1;
}

// Set boot protocol for a HID device
static bool set_boot_protocol(UsbDeviceInfo* dev) {
    uint16_t transferred;
    return xhci_control_transfer(
        dev->slot_id,
        0x21,  // Host-to-device, Class, Interface
        HID_REQ_SET_PROTOCOL,
        HID_PROTOCOL_BOOT,
        dev->hid_interface,
        0,
        nullptr,
        &transferred
    );
}

// Set idle rate (0 = only report on change)
static bool set_idle(UsbDeviceInfo* dev, uint8_t idle_rate) {
    uint16_t transferred;
    return xhci_control_transfer(
        dev->slot_id,
        0x21,  // Host-to-device, Class, Interface
        HID_REQ_SET_IDLE,
        idle_rate << 8,
        dev->hid_interface,
        0,
        nullptr,
        &transferred
    );
}

void usb_hid_init() {
    // Find keyboard
    keyboard_device = usb_find_keyboard();
    if (keyboard_device) {
        keyboard_available = true;
        set_boot_protocol(keyboard_device);
        set_idle(keyboard_device, 0);
    }
    
    // Find mouse
    mouse_device = usb_find_mouse();
    if (mouse_device) {
        mouse_available = true;
        set_boot_protocol(mouse_device);
        set_idle(mouse_device, 0);
        
        // Initialize mouse to center of screen
        mouse_x = screen_width / 2;
        mouse_y = screen_height / 2;
    }
}

void usb_hid_poll() {
    // Poll keyboard
    if (keyboard_device && keyboard_available) {
        HidKeyboardReport report;
        uint16_t transferred;
        
        uint8_t xhci_ep = keyboard_device->hid_endpoint;
        
        if (xhci_interrupt_transfer(keyboard_device->slot_id, xhci_ep, 
                                    &report, sizeof(report), &transferred)) {
            if (transferred >= 3) {  // Minimum valid keyboard report
                process_keyboard_report(&report);
            }
        }
    }
    
    // Poll mouse
    if (mouse_device && mouse_available) {
        HidMouseReport report;
        uint16_t transferred;
        
        // hid_endpoint now stores the xHCI endpoint index directly
        uint8_t xhci_ep = mouse_device->hid_endpoint;
        
        if (xhci_interrupt_transfer(mouse_device->slot_id, xhci_ep,
                                    &report, sizeof(report), &transferred)) {
            if (transferred >= 3) {  // Minimum valid mouse report
                process_mouse_report(&report);
            }
        }
    }
}

bool usb_hid_keyboard_available() {
    return keyboard_available;
}

bool usb_hid_keyboard_has_char() {
    return kb_buffer_start != kb_buffer_end;
}

char usb_hid_keyboard_get_char() {
    if (kb_buffer_start == kb_buffer_end) {
        return 0;
    }
    char c = kb_buffer[kb_buffer_start];
    kb_buffer_start = (kb_buffer_start + 1) % KB_BUFFER_SIZE;
    return c;
}

bool usb_hid_mouse_available() {
    return mouse_available;
}

void usb_hid_mouse_get_state(int32_t* x, int32_t* y, bool* left, bool* right, bool* middle) {
    if (x) *x = mouse_x;
    if (y) *y = mouse_y;
    if (left) *left = mouse_left;
    if (right) *right = mouse_right;
    if (middle) *middle = mouse_middle;
}
