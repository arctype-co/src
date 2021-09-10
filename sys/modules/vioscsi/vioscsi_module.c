/*
 * Copyright (c) 2013 Google Inc.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/device.h>
#include <sys/module.h>

MODULE(MODULE_CLASS_DRIVER, vioscsi, "virtio");

#ifdef _MODULE
#include "ioconf.c"
#endif

static int
vioscsi_modcmd(modcmd_t cmd, void *opaque)
{
        int error = 0;

#ifdef _MODULE
        switch (cmd) {
        case MODULE_CMD_INIT:
                error = config_init_component(cfdriver_ioconf_vioscsi,
                    cfattach_ioconf_vioscsi, cfdata_ioconf_vioscsi);
                break;
        case MODULE_CMD_FINI:
                error = config_fini_component(cfdriver_ioconf_vioscsi,
                    cfattach_ioconf_vioscsi, cfdata_ioconf_vioscsi);
                break;
        default:
                error = ENOTTY;
                break;
        }
#endif

        return error;
}
