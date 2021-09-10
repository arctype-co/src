/*
 * Copyright (c) 2020 The NetBSD Foundation, Inc.
 * Copyright (c) 2010 Minoura Makoto.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/device.h>
#include <sys/module.h>

MODULE(MODULE_CLASS_DRIVER, if_vioif, "virtio");

#ifdef _MODULE
#include "ioconf.c"
#endif

static int
if_vioif_modcmd(modcmd_t cmd, void *opaque)
{
	int error = 0;

#ifdef _MODULE
	switch (cmd) {
	case MODULE_CMD_INIT:
		error = config_init_component(cfdriver_ioconf_if_vioif,
		    cfattach_ioconf_if_vioif, cfdata_ioconf_if_vioif);
		break;
	case MODULE_CMD_FINI:
		error = config_fini_component(cfdriver_ioconf_if_vioif,
		    cfattach_ioconf_if_vioif, cfdata_ioconf_if_vioif);
		break;
	default:
		error = ENOTTY;
		break;
	}
#endif

	return error;
}
