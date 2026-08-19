// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.

#ifndef DFU_CONF_H
#define DFU_CONF_H

#include "xua_conf_full.h"

/* GW_SPI_DFU_EN keeps lib_dfu built when the USB facing DFU is off, so
 * device_reboot() is the real thing rather than the empty stub lib_dfu falls
 * back to. Nothing instantiates lib_dfu's USB request handling unless
 * XUA_DFU_EN is set, so this adds no USB surface. */
#define DFU_ENABLE (XUA_DFU_EN || GW_SPI_DFU_EN)

#define DFU_USB_EN XUA_USB_EN

#define DFU_BCD_DEVICE BCD_DEVICE

#endif /* DFU_CONF_H */
