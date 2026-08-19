// Copyright 2011-2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include <xs1.h>
#include <flash.h>
#include <flashlib.h>
#include <string.h>
#include <xclib.h>

#include "xua.h"
#include "dfu_types.h" // for _FLASH_PAGE_SIZE_BYTES and _NUM_DFU_PAGES_PER_FLASH_PAGE defines

/* GW_SPI_DFU_EN: also build this layer when the update transport is the
 application's own rather than USB. See xua_conf_default.h. */
#if (XUA_DFU_EN == 1) || (GW_SPI_DFU_EN == 1)

/* Defines flash area to erase on first DFU download request received
 *
 * Flash library will round it up to the nearest sector, e.g. 4KB
 *
 * XS2 internal flash IS25LQ016B takes 70ms to erase one sector
 * 128KB will take over 2 seconds, for instance
 *
 * Your host software might implement a 5sec timeout as per USB spec 9.2.6.1,
 * and 5 seconds is just over 300KB
 */
#ifndef FLASH_MAX_UPGRADE_SIZE
#define FLASH_MAX_UPGRADE_SIZE (512 * 1024)
#endif

/* GW_SPI_DFU_EN: upstream defines this as a no-op, so every flash failure below
 * is swallowed and a caller cannot tell a completed update from a failed one.
 * The SPI DFU engine reports success or failure back to the host, so it has to
 * know. Latch the error here and let the caller drain it with
 * flash_get_and_clear_error(). Harmless for the USB DFU path, which simply
 * never calls it. See dfu_engine.xc. */
static int flash_error_latched = 0;

#define FLASH_ERROR() do { flash_error_latched = 1; } while(0)

int flash_get_and_clear_error(void)
{
    int err = flash_error_latched;
    flash_error_latched = 0;
    return err;
}

static int flash_device_open = 0;
static fl_BootImageInfo factory_image;
static fl_BootImageInfo upgrade_image;

static int upgrade_image_valid = 0;
static int current_flash_subpage_index = 0;
static unsigned char current_flash_page_data[_FLASH_PAGE_SIZE_BYTES];

int flash_cmd_enable_ports() __attribute__ ((weak));
int flash_cmd_enable_ports() {
  return 0;
}

int flash_cmd_disable_ports() __attribute__ ((weak));
int flash_cmd_disable_ports() {
  return 0;
}

void DFUCustomFlashEnable() __attribute__ ((weak));
void DFUCustomFlashEnable()
{
    return;
}

void DFUCustomFlashDisable() __attribute__ ((weak));
void DFUCustomFlashDisable()
{
    return;
}

/* Returns non-zero for error */
int flash_cmd_init(void)
{
    fl_BootImageInfo image;

    if (!flash_device_open)
    {
        if (flash_cmd_enable_ports())
            flash_device_open = 1;
    }

    if (!flash_device_open)
    {
        return 1;
    }

#if (!XUA_QUAD_SPI_FLASH)
    // Disable flash protection
    fl_setProtection(0);
#endif

    if (fl_getFactoryImage(&image) != 0)
    {
        return 1;
    }

    factory_image = image;

    if (fl_getNextBootImage(&image) == 0)
    {
        upgrade_image_valid = 1;
        upgrade_image = image;
    }

     return 0;
}

int flash_cmd_deinit(void)
{
    if (!flash_device_open)
        return 0;

    flash_cmd_disable_ports();
    flash_device_open = 0;
    return 0;
}

void flash_cmd_read_page(unsigned char *data)
{
    if (!upgrade_image_valid)
    {
        *(unsigned int *)data = 1;
        return;
    }

    if (*(unsigned int *)data == 0)
    {
        fl_startImageRead(&upgrade_image);
    }

    current_flash_subpage_index = 0;

    if (fl_readImagePage(current_flash_page_data) == 0)
    {
        *(unsigned int *)data = 0;
     }
    else
    {
        *(unsigned int *)data = 1;
    }
    return;
}

int flash_cmd_read_page_data(unsigned char *data)
{
    unsigned char *page_data_ptr = &current_flash_page_data[current_flash_subpage_index * _DFU_TRANSFER_SIZE_BYTES];
    memcpy(data, page_data_ptr, _DFU_TRANSFER_SIZE_BYTES);

    current_flash_subpage_index++;

    return _DFU_TRANSFER_SIZE_BYTES;
}


int flash_cmd_start_write_image()
{
    current_flash_subpage_index = 0;
    int ret = fl_startImageAdd(&factory_image, FLASH_MAX_UPGRADE_SIZE, 0);
    if(ret < 0)
        FLASH_ERROR();
    return ret;
}

void flash_cmd_reset_subpage_index()
{
    current_flash_subpage_index = 0;
}

void flash_cmd_end_write_image()
{
     if (fl_endWriteImage() != 0)
        FLASH_ERROR();

    // Sanity check
    fl_BootImageInfo image = factory_image;
    if (fl_getNextBootImage(&image) != 0)
        FLASH_ERROR();

}

int flash_cmd_write_page_data(unsigned char *data)
{
    unsigned char *page_data_ptr = &current_flash_page_data[current_flash_subpage_index * _DFU_TRANSFER_SIZE_BYTES];

    /* GW_SPI_DFU_EN: both of these drop the caller's data on the floor. They
     * have to latch, or a transfer that erased nothing first, or overran its
     * page, would look like a clean update. */
    if (upgrade_image_valid)
    {
        FLASH_ERROR();
        return 0;
    }

    if (current_flash_subpage_index >= _NUM_DFU_PAGES_PER_FLASH_PAGE)
    {
        FLASH_ERROR();
        return 0;
    }

    memcpy(page_data_ptr, data, _DFU_TRANSFER_SIZE_BYTES);

    current_flash_subpage_index++;

    if (current_flash_subpage_index == _NUM_DFU_PAGES_PER_FLASH_PAGE)
    {
        if (fl_writeImagePage(current_flash_page_data) != 0)
            FLASH_ERROR();
    }

    return 0;
}

int flash_cmd_erase_all(void)
{
    fl_BootImageInfo tmp_image = upgrade_image;

    if (upgrade_image_valid)
    {
        if (fl_deleteImage(&upgrade_image) != 0)
        {
            FLASH_ERROR();
        }

        // Keep deleting all upgrade images
        // TODO Perhaps using replace would be nicer...
        while(1)
        {
            if (fl_getNextBootImage(&tmp_image) == 0)
            {
                if (fl_deleteImage(&tmp_image) != 0)
                {
                    FLASH_ERROR();
                }
            }
            else
            {
                break;
            }
        }

    upgrade_image_valid = 0;
    }
    return 0;
}
#endif
