/**
 * PS Move API
 * Copyright (c) 2011 Thomas Perl <thp.io/about>
 * All Rights Reserved
 **/

#include "psmove.h"

#include "hidapi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <unistd.h>



/* Begin private definitions */

/* Vendor ID and Product ID of PS Move Controller */
#define PSMOVE_VID 0x054c
#define PSMOVE_PID 0x03d5

/* Buffer size for writing LEDs and reading sensor data */
#define PSMOVE_BUFFER_SIZE 49

/* Buffer size for calibration data */
#define PSMOVE_CALIBRATION_SIZE 49

/* Buffer size for the Bluetooth address get request */
#define PSMOVE_BTADDR_GET_SIZE 16

/* Buffer size for the Bluetooth address set request */
#define PSMOVE_BTADDR_SET_SIZE 23

enum PSMove_Request_Type {
    PSMove_Req_GetInput = 0x01,
    PSMove_Req_SetLEDs = 0x02,
    PSMove_Req_GetBTAddr = 0x04,
    PSMove_Req_SetBTAddr = 0x05,
    PSMove_Req_GetCalibration = 0x10,
};

typedef struct {
    unsigned char type; /* message type, must be PSMove_Req_SetLEDs */
    unsigned char _zero; /* must be zero */
    unsigned char r; /* red value, 0x00..0xff */
    unsigned char g; /* green value, 0x00..0xff */
    unsigned char b; /* blue value, 0x00..0xff */
    unsigned char rumble2; /* unknown, should be 0x00 for now */
    unsigned char rumble; /* rumble value, 0x00..0xff */
    unsigned char _padding[PSMOVE_BUFFER_SIZE-7]; /* must be zero */
} PSMove_Data_LEDs;

typedef struct {
    unsigned char type; /* message type, must be PSMove_Req_GetInput */
    unsigned char buttons1;
    unsigned char buttons2;
    unsigned char buttons3;
    unsigned char buttons4;
    unsigned char _unk5;
    unsigned char trigger; /* trigger value; 0..255 */
    unsigned char _unk7;
    unsigned char _unk8;
    unsigned char _unk9;
    unsigned char _unk10;
    unsigned char _unk11;
    unsigned char _unk12;
    unsigned char aXlow; /* low byte of accelerometer X value */
    unsigned char aXhigh; /* high byte of accelerometer X value */
    unsigned char aYlow;
    unsigned char aYhigh;
    unsigned char aZlow;
    unsigned char aZhigh;
    unsigned char aXlow2; /* low byte of accelerometer X value, 2nd frame */
    unsigned char aXhigh2; /* high byte of accelerometer X value, 2nd frame */
    unsigned char aYlow2;
    unsigned char aYhigh2;
    unsigned char aZlow2;
    unsigned char aZhigh2;
    unsigned char gXlow; /* low byte of gyro X value */
    unsigned char gXhigh; /* high byte of gyro X value */
    unsigned char gYlow;
    unsigned char gYhigh;
    unsigned char gZlow;
    unsigned char gZhigh;
    unsigned char gXlow2; /* low byte of gyro X value, 2nd frame */
    unsigned char gXhigh2; /* high byte of gyro X value, 2nd frame */
    unsigned char gYlow2;
    unsigned char gYhigh2;
    unsigned char gZlow2;
    unsigned char gZhigh2;
    unsigned char mag38; /* magnetometer data, weirdly aligned (see code) */
    unsigned char mag39;
    unsigned char mag40;
    unsigned char mag41;
    unsigned char mag42;
    unsigned char _padding[PSMOVE_BUFFER_SIZE-42]; /* unknown */
} PSMove_Data_Input;

struct _PSMove {
    /* The handle to the HIDAPI device */
    hid_device *handle;

    /* Various buffers for PS Move-related data */
    PSMove_Data_LEDs leds;
    PSMove_Data_Input input;
};

/* Macro: Print a critical message if an assertion fails */
#define psmove_CRITICAL(x) \
        {fprintf(stderr, "[PSMOVE] Assertion fail in %s: %s\n", __func__, x);}

/* Macros: Return immediately if an assertion fails + log */
#define psmove_return_if_fail(expr) \
        {if(!(expr)){psmove_CRITICAL(#expr);return;}}
#define psmove_return_val_if_fail(expr, val) \
        {if(!(expr)){psmove_CRITICAL(#expr);return(val);}}

/* End private definitions */



PSMove *
psmove_connect()
{
    PSMove *move = (PSMove*)calloc(1, sizeof(PSMove));

    move->handle = hid_open(PSMOVE_VID, PSMOVE_PID, NULL);

    if (!move->handle) {
        free(move);
        return NULL;
    }

    /* Use Non-Blocking I/O */
    hid_set_nonblocking(move->handle, 1);

    /* Message type for LED set requests */
    move->leds.type = PSMove_Req_SetLEDs;

    return move;
}

int
psmove_get_btaddr(PSMove *move, PSMove_Data_BTAddr *addr)
{
    unsigned char cal[PSMOVE_CALIBRATION_SIZE];
    unsigned char btg[PSMOVE_BTADDR_GET_SIZE];
    int res;

    psmove_return_val_if_fail(move != NULL, 0);

    /* Get calibration data */
    memset(cal, 0, sizeof(cal));
    cal[0] = PSMove_Req_GetCalibration;
    res = hid_get_feature_report(move->handle, cal, sizeof(cal));
    if (res < 0) {
        return 0;
    }

    /* Get Bluetooth address */
    memset(btg, 0, sizeof(btg));
    btg[0] = PSMove_Req_GetBTAddr;
    res = hid_get_feature_report(move->handle, btg, sizeof(btg));

    if (res == sizeof(btg)) {
#ifdef PSMOVE_DEBUG
        fprintf(stderr, "[PSMOVE] current bt mac addr: ");
        for (int i=15; i>=10; i--) {
            if (i != 15) putc(':', stderr);
            fprintf(stderr, "%02x", btg[i]);
        }
        fprintf(stderr, "\n");
#endif
        if (addr != NULL) {
            /* Copy 6 bytes (btg[10]..btg[15]) into addr */
            memcpy(addr, btg+10, 6);
        }

        /* Success! */
        return 1;
    }

    return 0;
}

int
psmove_set_btaddr(PSMove *move, PSMove_Data_BTAddr *addr)
{
    unsigned char bts[PSMOVE_BTADDR_SET_SIZE];
    int res;

    psmove_return_val_if_fail(move != NULL, 0);
    psmove_return_val_if_fail(addr != NULL, 0);

    /* Get calibration data */
    memset(bts, 0, sizeof(bts));
    bts[0] = PSMove_Req_SetBTAddr;

    /* Copy 6 bytes from addr into bts[1]..bts[6] */
    memcpy(bts+1, addr, 6);

    res = hid_send_feature_report(move->handle, bts, sizeof(bts));

    return (res == sizeof(bts));
}

enum PSMove_Connection_Type
psmove_connection_type(PSMove *move)
{
    wchar_t wstr[255];
    int res;

    psmove_return_val_if_fail(move != NULL, Conn_Unknown);

    wstr[0] = 0x0000;
    res = hid_get_serial_number_string(move->handle,
            wstr, sizeof(wstr)/sizeof(wstr[0]));

    /**
     * As it turns out, we don't get a serial number when connected via USB,
     * so assume that when the serial number length is zero, then we have the
     * USB connection type, and if we have a greater-than-zero length, then it
     * is a Bluetooth connection.
     **/
    if (res == 0) {
        return Conn_USB;
    } else if (res > 0) {
        return Conn_Bluetooth;
    }

    return Conn_Unknown;
}

void
psmove_set_leds(PSMove *move, unsigned char r, unsigned char g,
        unsigned char b)
{
    psmove_return_if_fail(move != NULL);
    move->leds.r = r;
    move->leds.g = g;
    move->leds.b = b;
}

void
psmove_set_rumble(PSMove *move, unsigned char rumble)
{
    psmove_return_if_fail(move != NULL);
    move->leds.rumble2 = 0x00;
    move->leds.rumble = rumble;
}

int
psmove_update_leds(PSMove *move)
{
    int res;

    psmove_return_val_if_fail(move != NULL, 0);

    res = hid_write(move->handle, (unsigned char*)(&(move->leds)),
            sizeof(move->leds));
    return (res == sizeof(move->leds));
}

int
psmove_poll(PSMove *move)
{
    int res;

    psmove_return_val_if_fail(move != NULL, 0);

#ifdef PSMOVE_DEBUG
    /* store old sequence number before reading */
    int oldseq = (move->input.buttons4 & 0x0F);
#endif

    res = hid_read(move->handle, (unsigned char*)(&(move->input)),
        sizeof(move->input));

    if (res == sizeof(move->input)) {
        /* Sanity check: The first byte should be PSMove_Req_GetInput */
        psmove_return_val_if_fail(move->input.type == PSMove_Req_GetInput, 0);

        /**
         * buttons4's 4 least significant bits contain the sequence number,
         * so we add 1 to signal "success" and add the sequence number for
         * consumers to utilize the data
         **/
#ifdef PSMOVE_DEBUG
        int seq = (move->input.buttons4 & 0x0F);
        if (seq != ((oldseq + 1) % 16)) {
            fprintf(stderr, "[PSMOVE] Warning: Dropped frames (seq %d -> %d)\n",
                    oldseq, seq);
        }
#endif
        return 1 + (move->input.buttons4 & 0x0F);
    }

    return 0;
}

unsigned int
psmove_get_buttons(PSMove *move)
{
    psmove_return_val_if_fail(move != NULL, 0);

    return ((move->input.buttons2) |
            (move->input.buttons1 << 8) |
            ((move->input.buttons3 & 0x01) << 16) |
            ((move->input.buttons4 & 0xF0) << 13));
}

unsigned char
psmove_get_trigger(PSMove *move)
{
    psmove_return_val_if_fail(move != NULL, 0);

    return move->input.trigger;
}

void
psmove_get_accelerometer(PSMove *move, int *ax, int *ay, int *az)
{
    psmove_return_if_fail(move != NULL);

    if (ax != NULL) {
        *ax = ((move->input.aXlow + move->input.aXlow2) +
               ((move->input.aXhigh + move->input.aXhigh2) << 8)) / 2 - 0x8000;
    }

    if (ay != NULL) {
        *ay = ((move->input.aYlow + move->input.aYlow2) +
               ((move->input.aYhigh + move->input.aYhigh2) << 8)) / 2 - 0x8000;
    }

    if (az != NULL) {
        *az = ((move->input.aZlow + move->input.aZlow2) +
               ((move->input.aZhigh + move->input.aZhigh2) << 8)) / 2 - 0x8000;
    }
}

void
psmove_get_gyroscope(PSMove *move, int *gx, int *gy, int *gz)
{
    psmove_return_if_fail(move != NULL);

    if (gx != NULL) {
        *gx = ((move->input.gXlow + move->input.gXlow2) +
               ((move->input.gXhigh + move->input.gXhigh2) << 8)) / 2 - 0x8000;
    }

    if (gy != NULL) {
        *gy = ((move->input.gYlow + move->input.gYlow2) +
               ((move->input.gYhigh + move->input.gYhigh2) << 8)) / 2 - 0x8000;
    }

    if (gz != NULL) {
        *gz = ((move->input.gZlow + move->input.gZlow2) +
               ((move->input.gZhigh + move->input.gZhigh2) << 8)) / 2 - 0x8000;
    }
}

void
psmove_get_magnetometer(PSMove *move, int *mx, int *my, int *mz)
{
    psmove_return_if_fail(move != NULL);

    if (mx != NULL) {
        *mx = move->input.mag38 << 0x0C | move->input.mag39 << 0x04;
    }

    if (my != NULL) {
        *my = move->input.mag40 << 0x08 | (move->input.mag41 & 0xF0);
    }

    if (mz != NULL) {
        *mz = move->input.mag41 << 0x0C | move->input.mag42 << 0x0f;
    }
}

void
psmove_disconnect(PSMove *move)
{
    psmove_return_if_fail(move != NULL);
    free(move);
}

