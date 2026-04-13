/*
 * Manul - Keyboard Interface
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * Based on Iris by Mikhail Matveev / VersaTerm by David Hansel
 *
 * Simplified: no Russian input, no macros, no keymap editing.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "keyboard.h"
#include "keyboard_usb.h"
#include "keyboard_ps2.h"
#include "browser_config.h"
#include "sound.h"
#include "font.h"
#include "pico/util/queue.h"
#include "pico/time.h"
#include <ctype.h>
#include <stdlib.h>

#define KEYBOARD_MODIFIER_BOTHSHIFT (KEYBOARD_MODIFIER_LEFTSHIFT|KEYBOARD_MODIFIER_RIGHTSHIFT)

// defined in main.c
void wait(uint32_t milliseconds);

#define INFLASHFUN __in_flash(".kbdfun")

static queue_t keyboard_queue;
static uint8_t keyboard_led_status = 0;
static uint8_t keyboard_modifiers  = 0;

// -------------------------------------------  keyboard layouts  -------------------------------------------

struct IntlMapStruct {
    uint8_t mapNormal[71];
    uint8_t mapShift[71];
    struct { int code; bool shift; int character; } mapOther[10];
    struct { int code; int character; } mapAltGr[12];
    uint8_t keypadDecimal;
};

const struct IntlMapStruct __in_flash(".keymaps") intlMaps[7] = {
  { // US English
    {0    ,0    ,0    ,0    ,'a'  ,'b'  ,'c'  ,'d'  ,
     'e'  ,'f'  ,'g'  ,'h'  ,'i'  ,'j'  ,'k'  ,'l'  ,
     'm'  ,'n'  ,'o'  ,'p'  ,'q'  ,'r'  ,'s'  ,'t'  ,
     'u'  ,'v'  ,'w'  ,'x'  ,'y'  ,'z'  ,'1'  ,'2'  ,
     '3'  ,'4'  ,'5'  ,'6'  ,'7'  ,'8'  ,'9'  ,'0'  ,
     KEY_ENTER, KEY_ESC, KEY_BACKSPACE, KEY_TAB, ' ', '-', '=', '[',
     ']'  ,'\\' ,'\\' ,';'  ,'\'' ,'`'  ,','  ,'.'  ,
     '/'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {0    ,0    ,0    ,0    ,'A'  ,'B'  ,'C'  ,'D'  ,
     'E'  ,'F'  ,'G'  ,'H'  ,'I'  ,'J'  ,'K'  ,'L'  ,
     'M'  ,'N'  ,'O'  ,'P'  ,'Q'  ,'R'  ,'S'  ,'T'  ,
     'U'  ,'V'  ,'W'  ,'X'  ,'Y'  ,'Z'  ,'!'  ,'@'  ,
     '#'  ,'$'  ,'%'  ,'^'  ,'&'  ,'*'  ,'('  ,')'  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,'_'  ,'+'  ,'{'  ,
     '}'  ,'|'  ,'|'  ,':'  ,'"'  ,'~'  ,'<'  ,'>'  ,
     '?'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {{0x64, 0, '\\'}, {0x64, 1, '|'}, {-1,-1}},
    {{-1,-1}},
    '.'
  },{ // UK English
    {0    ,0    ,0    ,0    ,'a'  ,'b'  ,'c'  ,'d'  ,
     'e'  ,'f'  ,'g'  ,'h'  ,'i'  ,'j'  ,'k'  ,'l'  ,
     'm'  ,'n'  ,'o'  ,'p'  ,'q'  ,'r'  ,'s'  ,'t'  ,
     'u'  ,'v'  ,'w'  ,'x'  ,'y'  ,'z'  ,'1'  ,'2'  ,
     '3'  ,'4'  ,'5'  ,'6'  ,'7'  ,'8'  ,'9'  ,'0'  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,'-'  ,'='  ,'['  ,
     ']'  ,'#'  ,'#'  ,';'  ,'\'' ,'`'  ,','  ,'.'  ,
     '/'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {0    ,0    ,0    ,0    ,'A'  ,'B'  ,'C'  ,'D'  ,
     'E'  ,'F'  ,'G'  ,'H'  ,'I'  ,'J'  ,'K'  ,'L'  ,
     'M'  ,'N'  ,'O'  ,'P'  ,'Q'  ,'R'  ,'S'  ,'T'  ,
     'U'  ,'V'  ,'W'  ,'X'  ,'Y'  ,'Z'  ,'!'  ,'"'  ,
     '#'  ,'$'  ,'%'  ,'^'  ,'&'  ,'*'  ,'('  ,')'  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,'_'  ,'+'  ,'{'  ,
     '}'  ,'~'  ,'~'  ,':'  ,'@'  ,'~'  ,'<'  ,'>'  ,
     '?'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {{0x64, 0, '\\'}, {0x64, 1, '|'}, {-1,-1}},
    {{-1,-1}},
    '.'
  },{ // French
    {0    ,0    ,0    ,0    ,'q'  ,'b'  ,'c'  ,'d'  ,
     'e'  ,'f'  ,'g'  ,'h'  ,'i'  ,'j'  ,'k'  ,'l'  ,
     ','  ,'n'  ,'o'  ,'p'  ,'a'  ,'r'  ,'s'  ,'t'  ,
     'u'  ,'v'  ,'z'  ,'x'  ,'y'  ,'w'  ,'&'  ,0    ,
     '"'  ,'\'' ,'('  ,'-'  ,0    ,'_'  ,0    ,0    ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,')'  ,'='  ,'^'  ,
     '$'  ,'*'  ,'*'  ,'m'  ,0    ,0    ,';'  ,':'  ,
     '!'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {0    ,0    ,0    ,0    ,'Q'  ,'B'  ,'C'  ,'D'  ,
     'E'  ,'F'  ,'G'  ,'H'  ,'I'  ,'J'  ,'K'  ,'L'  ,
     '?'  ,'N'  ,'O'  ,'P'  ,'A'  ,'R'  ,'S'  ,'T'  ,
     'U'  ,'V'  ,'Z'  ,'X'  ,'Y'  ,'W'  ,'1'  ,'2'  ,
     '3'  ,'4'  ,'5'  ,'6'  ,'7'  ,'8'  ,'9'  ,'0'  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,0    ,'+'  ,0    ,
     0    ,0    ,0    ,'M'  ,'%'  ,0    ,'.'  ,'/'  ,
     0    ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {{0x64, 0, '<'}, {0x64, 1, '>'}, {-1,-1}},
    {{0x1f, '~'  }, {0x21, '{'  }, {0x20, '#'  }, {0x22, '['  },
     {0x23, '|'  }, {0x25, '\\' }, {0x27, '@'  }, {0x2d, ']'  },
     {0x2e, '}'  }, {0x64, '\\'  }, {-1,-1}},
    ','
  },{ // German
    {0    ,0    ,0    ,0    ,'a'  ,'b'  ,'c'  ,'d'  ,
     'e'  ,'f'  ,'g'  ,'h'  ,'i'  ,'j'  ,'k'  ,'l'  ,
     'm'  ,'n'  ,'o'  ,'p'  ,'q'  ,'r'  ,'s'  ,'t'  ,
     'u'  ,'v'  ,'w'  ,'x'  ,'z'  ,'y'  ,'1'  ,'2'  ,
     '3'  ,'4'  ,'5'  ,'6'  ,'7'  ,'8'  ,'9'  ,'0'  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,0    ,'\'' ,0    ,
     '+'  ,'#'  ,'#'  ,0    ,0    ,'^'  ,','  ,'.'  ,
     '-'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {0    ,0    ,0    ,0    ,'A'  ,'B'  ,'C'  ,'D'  ,
     'E'  ,'F'  ,'G'  ,'H'  ,'I'  ,'J'  ,'K'  ,'L'  ,
     'M'  ,'N'  ,'O'  ,'P'  ,'Q'  ,'R'  ,'S'  ,'T'  ,
     'U'  ,'V'  ,'W'  ,'X'  ,'Z'  ,'Y'  ,'!'  ,'"'  ,
     0    ,'$'  ,'%'  ,'&'  ,'/'  ,'('  ,')'  ,'='  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,'?'  ,'`'  ,0    ,
     '*'  ,'\'' ,'\'', 0    ,0    ,0  ,';'  ,':'  ,
     '_'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {{0x64, 0, '<'}, {0x64, 1, '>'}, {-1,-1}},
    {{0x14, '@'  }, {0x24, '{'  }, {0x25, '['  }, {0x27, '}'  },
     {0x26, ']'  }, {0x2d, '\\' }, {0x30, '~'  }, {0x64, '|'  },
     {-1,-1}},
    ','
  },{ // Italian
    {0    ,0    ,0    ,0    ,'a'  ,'b'  ,'c'  ,'d'  ,
     'e'  ,'f'  ,'g'  ,'h'  ,'i'  ,'j'  ,'k'  ,'l'  ,
     'm'  ,'n'  ,'o'  ,'p'  ,'q'  ,'r'  ,'s'  ,'t'  ,
     'u'  ,'v'  ,'w'  ,'x'  ,'y'  ,'z'  ,'1'  ,'2'  ,
     '3'  ,'4'  ,'5'  ,'6'  ,'7'  ,'8'  ,'9'  ,'0'  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,'\'' ,0    ,0    ,
     '+'  ,0    ,0    ,0    ,0    ,'\\' ,','  ,'.'  ,
     '-'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {0    ,0    ,0    ,0    ,'A'  ,'B'  ,'C'  ,'D'  ,
     'E'  ,'F'  ,'G'  ,'H'  ,'I'  ,'J'  ,'K'  ,'L'  ,
     'M'  ,'N'  ,'O'  ,'P'  ,'Q'  ,'R'  ,'S'  ,'T'  ,
     'U'  ,'V'  ,'W'  ,'X'  ,'Y'  ,'Z'  ,'!'  ,'"'  ,
     0    ,'$'  ,'%'  ,'&'  ,'/'  ,'('  ,')'  ,'='  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,'?'  ,'^'  ,0    ,
     '*'  ,0    ,0    ,0    ,0    ,'|'  ,';'  ,':'  ,
     '_'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {{0x64, 0, '<'}, {0x64, 1, '>'}, {-1,-1}},
    {{0x33, '@'  }, {0x34, '#'  }, {0x2f, '['  }, {0x30, ']'  },
     {-1,-1}},
    ','
  },{ // Belgian
    {0    ,0    ,0    ,0    ,'q'  ,'b'  ,'c'  ,'d'  ,
     'e'  ,'f'  ,'g'  ,'h'  ,'i'  ,'j'  ,'k'  ,'l'  ,
     ','  ,'n'  ,'o'  ,'p'  ,'a'  ,'r'  ,'s'  ,'t'  ,
     'u'  ,'v'  ,'z'  ,'x'  ,'y'  ,'w'  ,'&'  ,0    ,
     '"'  ,'\'' ,'('  ,0    ,0    ,'!'  ,0    ,0    ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,')'  ,'-'  ,'^'  ,
     '$'  ,0    ,0    ,'m'  ,0    ,0    ,';'  ,':'  ,
     '='  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {0    ,0    ,0    ,0    ,'Q'  ,'B'  ,'C'  ,'D'  ,
     'E'  ,'F'  ,'G'  ,'H'  ,'I'  ,'J'  ,'K'  ,'L'  ,
     '?'  ,'N'  ,'O'  ,'P'  ,'A'  ,'R'  ,'S'  ,'T'  ,
     'U'  ,'V'  ,'Z'  ,'X'  ,'Y'  ,'W'  ,'1'  ,'2'  ,
     '3'  ,'4'  ,'5'  ,'6'  ,'7'  ,'8'  ,'9'  ,'0'  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,0    ,'_'  ,0    ,
     '*'  ,0    ,0    ,'M'  ,'%'  ,0    ,'.'  ,'/'  ,
     '+'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {{0x64, 0, '<'}, {0x64, 1, '>'}, {-1,-1}},
    {{0x1e, '|'  }, {0x64, '\\' }, {0x1f, '@'  }, {0x20, '#'  },
     {0x27, '}'  }, {0x26, '{'  }, {0x38, '~'  }, {0x2f, '['  },
     {0x30, ']'  }, {-1,-1}},
    ','
  }, { // Spanish
    {0    ,0    ,0    ,0    ,'a'  ,'b'  ,'c'  ,'d'  ,
     'e'  ,'f'  ,'g'  ,'h'  ,'i'  ,'j'  ,'k'  ,'l'  ,
     'm'  ,'n'  ,'o'  ,'p'  ,'q'  ,'r'  ,'s'  ,'t'  ,
     'u'  ,'v'  ,'w'  ,'x'  ,'y'  ,'z'  ,'1'  ,'2'  ,
     '3'  ,'4'  ,'5'  ,'6'  ,'7'  ,'8'  ,'9'  ,'0'  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,'\'' ,0    ,'`'  ,
     '+'  ,0    ,0    ,0    ,'\'' ,'\\' ,','  ,'.'  ,
     '-'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {0    ,0    ,0    ,0    ,'A'  ,'B'  ,'C'  ,'D'  ,
     'E'  ,'F'  ,'G'  ,'H'  ,'I'  ,'J'  ,'K'  ,'L'  ,
     'M'  ,'N'  ,'O'  ,'P'  ,'Q'  ,'R'  ,'S'  ,'T'  ,
     'U'  ,'V'  ,'W'  ,'X'  ,'Y'  ,'Z'  ,'!'  ,'"'  ,
     0    ,'$'  ,'%'  ,'&'  ,'/'  ,'('  ,')'  ,'='  ,
     KEY_ENTER ,KEY_ESC   ,KEY_BACKSPACE  ,KEY_TAB ,' '  ,'?'  ,0    ,'^'  ,
     '*'  ,0    ,0    ,0    ,0    ,'\\' ,';'  ,':'  ,
     '_'  ,0 , KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
     KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCRN },
    {{0x64, 0, '<'}, {0x64, 1, '>'}, {-1,-1}},
    {{0x35, '\\' }, {0x1e, '|'  }, {0x1f, '@'  }, {0x21, '~'  },
     {0x20, '#'  }, {0x34, '{'  }, {0x2f, '['  }, {0x30, ']'  },
     {0x31, '}'  }, {-1,-1}},
    ','
  }
};

static const int keyMapKeypad[]    = {'\\', '*', '-', '+', KEY_ENTER, KEY_END, KEY_DOWN, KEY_PDOWN, KEY_LEFT, 0, KEY_RIGHT, KEY_HOME, KEY_UP, KEY_PUP, KEY_INSERT, KEY_DELETE};
static const int keyMapKeypadNum[] = {'\\', '*', '-', '+', KEY_ENTER, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '.'};
static const int keyMapSpecial[]   = {KEY_PAUSE, KEY_INSERT, KEY_HOME, KEY_PUP, KEY_DELETE, KEY_END, KEY_PDOWN, KEY_RIGHT, KEY_LEFT, KEY_DOWN, KEY_UP};

static uint8_t keyboardLanguage = 0;

uint8_t INFLASHFUN keyboard_map_key_ascii(uint16_t k, bool *isaltcode) {
    const struct IntlMapStruct *map = &(intlMaps[keyboardLanguage]);
    uint8_t i, ascii = 0;

    uint8_t key = k & 0xFF;
    uint8_t modifier = k >> 8;

    /* Left-Alt + numeric keypad = alt code entry */
    static uint8_t altcodecount = 0;
    static uint8_t altcode = 0;
    if (isaltcode != NULL) *isaltcode = false;
    if ((modifier & KEYBOARD_MODIFIER_LEFTALT) != 0 && key >= HID_KEY_KEYPAD_1 && key <= HID_KEY_KEYPAD_0) {
        altcode *= 10;
        altcode += (key == HID_KEY_KEYPAD_0) ? 0 : (key - HID_KEY_KEYPAD_1 + 1);
        altcodecount++;
        if (altcodecount == 3) {
            if (isaltcode != NULL) *isaltcode = true;
            ascii = altcode;
            altcode = 0;
            altcodecount = 0;
        }
        return ascii;
    } else {
        altcode = 0;
        altcodecount = 0;
    }

    if (modifier & KEYBOARD_MODIFIER_RIGHTALT) {
        for (i = 0; map->mapAltGr[i].code >= 0; i++)
            if (map->mapAltGr[i].code == key)
                { ascii = map->mapAltGr[i].character; break; }
    } else if (key <= HID_KEY_PRINT_SCREEN) {
        bool ctrl  = (modifier & (KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_RIGHTCTRL)) != 0;
        bool shift = (modifier & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT)) != 0;
        bool caps  = (key >= HID_KEY_A) && (key <= HID_KEY_Z) && (keyboard_led_status & KEYBOARD_LED_CAPSLOCK) != 0;

        if (shift ^ caps)
            ascii = map->mapShift[key];
        else
            ascii = map->mapNormal[key];

        if (ctrl && ascii >= 0x40 && ascii < 0x7f)
            ascii &= 0x1f;
    } else if ((key >= HID_KEY_PAUSE) && (key <= HID_KEY_ARROW_UP)) {
        ascii = keyMapSpecial[key - HID_KEY_PAUSE];
    } else if ((key >= HID_KEY_KEYPAD_DIVIDE) && (key <= HID_KEY_KEYPAD_DECIMAL)) {
        if ((keyboard_led_status & KEYBOARD_LED_NUMLOCK) == 0)
            ascii = keyMapKeypad[key - HID_KEY_KEYPAD_DIVIDE];
        else if (key == HID_KEY_KEYPAD_DECIMAL)
            ascii = map->keypadDecimal;
        else
            ascii = keyMapKeypadNum[key - HID_KEY_KEYPAD_DIVIDE];
    } else {
        bool shift = (modifier & KEYBOARD_MODIFIER_BOTHSHIFT) != 0;
        for (i = 0; map->mapOther[i].code >= 0; i++)
            if (map->mapOther[i].code == key && map->mapOther[i].shift == shift)
                ascii = map->mapOther[i].character;
    }

    return ascii;
}

// -------------------------------------------  LED handling  -------------------------------------------

static void INFLASHFUN process_led_keys(uint8_t key, uint8_t modifier) {
    (void)modifier;
    uint8_t status = keyboard_led_status;

    if (key == HID_KEY_CAPS_LOCK)
        keyboard_led_status ^= KEYBOARD_LED_CAPSLOCK;
    else if (key == HID_KEY_NUM_LOCK)
        keyboard_led_status ^= KEYBOARD_LED_NUMLOCK;
    else if (key == HID_KEY_SCROLL_LOCK)
        keyboard_led_status ^= KEYBOARD_LED_SCROLLLOCK;

    if (keyboard_led_status != status)
        keyboard_usb_set_led_status(keyboard_led_status);
}

// -------------------------------------------  key change  -------------------------------------------

static void INFLASHFUN keyboard_add_keypress(uint8_t key, uint8_t modifier) {
    process_led_keys(key, modifier);
    queue_try_add(&keyboard_queue, &key);
    queue_try_add(&keyboard_queue, &modifier);
}

void keyboard_key_change(uint8_t key, bool make) {
    /* Apply user key mapping */
    key = config_get_keyboard_user_mapping()[key];

    uint8_t mod = 0;
    switch (key) {
    case HID_KEY_SHIFT_LEFT:    mod = KEYBOARD_MODIFIER_LEFTSHIFT;  break;
    case HID_KEY_SHIFT_RIGHT:   mod = KEYBOARD_MODIFIER_RIGHTSHIFT; break;
    case HID_KEY_CONTROL_LEFT:  mod = KEYBOARD_MODIFIER_LEFTCTRL;   break;
    case HID_KEY_CONTROL_RIGHT: mod = KEYBOARD_MODIFIER_RIGHTCTRL;  break;
    case HID_KEY_ALT_LEFT:      mod = KEYBOARD_MODIFIER_LEFTALT;    break;
    case HID_KEY_ALT_RIGHT:     mod = KEYBOARD_MODIFIER_RIGHTALT;   break;
    case HID_KEY_GUI_LEFT:      mod = KEYBOARD_MODIFIER_LEFTGUI;    break;
    case HID_KEY_GUI_RIGHT:     mod = KEYBOARD_MODIFIER_RIGHTGUI;   break;
    }

    if (mod != 0) {
        if (make)
            keyboard_modifiers |= mod;
        else
            keyboard_modifiers &= ~mod;
    } else if (make) {
        keyboard_add_keypress(key, keyboard_modifiers);
    }
}

// -------------------------------------------  main functions  -------------------------------------------

size_t INFLASHFUN keyboard_num_keypress(void) {
    return queue_get_level(&keyboard_queue) / 2;
}

uint16_t INFLASHFUN keyboard_read_keypress(void) {
    uint16_t key = 0;
    uint8_t b = 0;
    if (queue_try_remove(&keyboard_queue, &b)) key  = b;
    if (queue_try_remove(&keyboard_queue, &b)) key |= b << 8;
    return key;
}

uint8_t keyboard_get_current_modifiers(void) {
    return keyboard_modifiers;
}

bool keyboard_ctrl_pressed(uint16_t key) {
    return (key & ((KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_RIGHTCTRL) << 8)) != 0;
}

bool keyboard_alt_pressed(uint16_t key) {
    return (key & ((KEYBOARD_MODIFIER_LEFTALT | KEYBOARD_MODIFIER_RIGHTALT) << 8)) != 0;
}

bool keyboard_shift_pressed(uint16_t key) {
    return (key & ((KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT) << 8)) != 0;
}

uint8_t INFLASHFUN keyboard_get_led_status(void) {
    return keyboard_led_status;
}

void INFLASHFUN keyboard_task(void) {
    keyboard_ps2_task();
    keyboard_usb_task();
}

void INFLASHFUN keyboard_apply_settings(void) {
    keyboard_modifiers = 0;
    keyboardLanguage = config_get_keyboard_layout();
    keyboard_ps2_apply_settings();
    keyboard_usb_apply_settings();
}

void INFLASHFUN keyboard_init(void) {
    keyboard_apply_settings();
    queue_init(&keyboard_queue, 1, 32);
    keyboard_ps2_init();
    keyboard_usb_init();
}
