#include <sys/systm.h>
#include <sys/types.h>
#include <sys/device.h>
#include <rump-sys/kern.h>

#define VIRTIO_PRIVATE
#include <dev/pci/virtiovar.h>

static int virtio_pci_probe(device_t parent, cfdata_t match, void *aux)
{
	aprint_normal_dev(parent, "Probing virtio_pci device at %p\n", aux);
	return 0;
}

CFATTACH_DECL_NEW(virtio_pci, sizeof(struct virtio_softc), virtio_pci_probe, NULL, NULL, NULL);

#if 0
#ifdef _MODULE
/*
 * XXX Don't allow ioconf.c to redefine the "struct cfdriver virtio_cd"
 * XXX it will be defined in virtio.c
 */
#undef  CFDRIVER_DECL
#define CFDRIVER_DECL(name, class, attr)
#endif
#include "ioconf.c"
RUMP_COMPONENT(RUMP_COMPONENT_DEV)
{
	config_init_component(cfdriver_ioconf_virtio,
	    cfattach_ioconf_virtio, cfdata_ioconf_virtio);
}
#endif
