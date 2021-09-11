/*
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

MODULE(MODULE_CLASS_DRIVER, ld_virtio, "ld,virtio");

#ifdef _MODULE
/*
 * XXX Don't allow ioconf.c to redefine the "struct cfdriver ld_cd"
 * XXX it will be defined in the common-code module
 */
#undef  CFDRIVER_DECL
#define CFDRIVER_DECL(name, class, attr)
#include "ioconf.c"
#endif

static int
ld_virtio_modcmd(modcmd_t cmd, void *opaque)
{
#ifdef _MODULE
	/*
	 * We ignore the cfdriver_vec[] that ioconf provides, since
	 * the cfdrivers are attached already.
	 */
	static struct cfdriver * const no_cfdriver_vec[] = { NULL };
#endif
	int error = 0;

#ifdef _MODULE
	switch (cmd) {
	case MODULE_CMD_INIT:
		error = config_init_component(no_cfdriver_vec,
		    cfattach_ioconf_ld_virtio, cfdata_ioconf_ld_virtio);
		break;
	case MODULE_CMD_FINI:
		error = config_fini_component(no_cfdriver_vec,
		    cfattach_ioconf_ld_virtio, cfdata_ioconf_ld_virtio);
		break;
	default:
		error = ENOTTY;
		break;
	}
#endif

	return error;
}
