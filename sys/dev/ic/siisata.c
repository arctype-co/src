/* $NetBSD: siisata.c,v 1.48 2021/08/07 16:19:12 thorpej Exp $ */

/* from ahcisata_core.c */

/*
 * Copyright (c) 2006 Manuel Bouyer.
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
 *
 */

/* from atapi_wdc.c */

/*
 * Copyright (c) 1998, 2001 Manuel Bouyer.
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

/*
 * Copyright (c) 2007, 2008, 2009, 2010 Jonathan A. Kollasch.
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

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: siisata.c,v 1.48 2021/08/07 16:19:12 thorpej Exp $");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/syslog.h>
#include <sys/disklabel.h>
#include <sys/buf.h>
#include <sys/proc.h>

#include <dev/ata/atareg.h>
#include <dev/ata/satavar.h>
#include <dev/ata/satareg.h>
#include <dev/ata/satafisvar.h>
#include <dev/ata/satafisreg.h>
#include <dev/ata/satapmpreg.h>
#include <dev/ic/siisatavar.h>
#include <dev/ic/siisatareg.h>

#include <dev/scsipi/scsi_all.h> /* for SCSI status */

#include "atapibus.h"

#ifdef SIISATA_DEBUG
int siisata_debug_mask = 0;
#endif

#define ATA_DELAY 10000		/* 10s for a drive I/O */
#define WDC_RESET_WAIT 31000	/* 31s for drive reset */

#ifndef __BUS_SPACE_HAS_STREAM_METHODS
#if _BYTE_ORDER == _LITTLE_ENDIAN
#define bus_space_read_stream_4 bus_space_read_4
#define bus_space_read_region_stream_4 bus_space_read_region_4
#else
static inline uint32_t
bus_space_read_stream_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
	return htole32(bus_space_read_4(t, h, o));
}

static inline void
bus_space_read_region_stream_4(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t o, uint32_t *p, bus_size_t c)
{
	bus_space_read_region_4(t, h, o, p, c);
	for (bus_size_t i = 0; i < c; i++) {
		p[i] = htole32(p[i]);
	}
}
#endif
#endif

static void siisata_attach_port(struct siisata_softc *, int);
static void siisata_intr_port(struct siisata_channel *);

void siisata_probe_drive(struct ata_channel *);
void siisata_setup_channel(struct ata_channel *);

void siisata_ata_bio(struct ata_drive_datas *, struct ata_xfer *);
void siisata_reset_drive(struct ata_drive_datas *, int, uint32_t *);
void siisata_reset_channel(struct ata_channel *, int);
int siisata_ata_addref(struct ata_drive_datas *);
void siisata_ata_delref(struct ata_drive_datas *);
void siisata_killpending(struct ata_drive_datas *);

int siisata_cmd_start(struct ata_channel *, struct ata_xfer *);
int siisata_cmd_complete(struct ata_channel *, struct ata_xfer *, int);
void siisata_cmd_poll(struct ata_channel *, struct ata_xfer *);
void siisata_cmd_abort(struct ata_channel *, struct ata_xfer *);
void siisata_cmd_done(struct ata_channel *, struct ata_xfer *, int);
static void siisata_cmd_done_end(struct ata_channel *, struct ata_xfer *);
void siisata_cmd_kill_xfer(struct ata_channel *, struct ata_xfer *, int);

int siisata_bio_start(struct ata_channel *, struct ata_xfer *);
int siisata_bio_complete(struct ata_channel *, struct ata_xfer *, int);
void siisata_bio_poll(struct ata_channel *, struct ata_xfer *);
void siisata_bio_abort(struct ata_channel *, struct ata_xfer *);
void siisata_bio_kill_xfer(struct ata_channel *, struct ata_xfer *, int);
void siisata_exec_command(struct ata_drive_datas *, struct ata_xfer *);

static int siisata_reinit_port(struct ata_channel *, int);
static void siisata_device_reset(struct ata_channel *);
static void siisata_activate_prb(struct siisata_channel *, int);
static void siisata_deactivate_prb(struct siisata_channel *, int);
static int siisata_dma_setup(struct ata_channel *, int, void *, size_t, int);
static void siisata_channel_recover(struct ata_channel *, int, uint32_t);

#if NATAPIBUS > 0
void siisata_atapibus_attach(struct atabus_softc *);
void siisata_atapi_probe_device(struct atapibus_softc *, int);
void siisata_atapi_minphys(struct buf *);
int siisata_atapi_start(struct ata_channel *,struct ata_xfer *);
int siisata_atapi_complete(struct ata_channel *, struct ata_xfer *, int);
void siisata_atapi_poll(struct ata_channel *, struct ata_xfer *);
void siisata_atapi_abort(struct ata_channel *, struct ata_xfer *);
void siisata_atapi_kill_xfer(struct ata_channel *, struct ata_xfer *, int);
void siisata_atapi_scsipi_request(struct scsipi_channel *,
    scsipi_adapter_req_t, void *);
void siisata_atapi_kill_pending(struct scsipi_periph *);
#endif /* NATAPIBUS */

const struct ata_bustype siisata_ata_bustype = {
	.bustype_type = SCSIPI_BUSTYPE_ATA,
	.ata_bio = siisata_ata_bio,
	.ata_reset_drive = siisata_reset_drive,
	.ata_reset_channel = siisata_reset_channel,
	.ata_exec_command = siisata_exec_command,
	.ata_get_params = ata_get_params,
	.ata_addref = siisata_ata_addref,
	.ata_delref = siisata_ata_delref,
	.ata_killpending = siisata_killpending,
	.ata_recovery = siisata_channel_recover,
};

#if NATAPIBUS > 0
static const struct scsipi_bustype siisata_atapi_bustype = {
	.bustype_type = SCSIPI_BUSTYPE_ATAPI,
	.bustype_cmd = atapi_scsipi_cmd,
	.bustype_interpret_sense = atapi_interpret_sense,
	.bustype_printaddr = atapi_print_addr,
	.bustype_kill_pending = siisata_atapi_kill_pending,
	.bustype_async_event_xfer_mode = NULL,
};
#endif /* NATAPIBUS */


void
siisata_attach(struct siisata_softc *sc)
{
	int i;

	SIISATA_DEBUG_PRINT(("%s: %s: GR_GC: 0x%08x\n",
	    SIISATANAME(sc), __func__, GRREAD(sc, GR_GC)), DEBUG_FUNCS);

	sc->sc_atac.atac_cap = ATAC_CAP_DMA | ATAC_CAP_UDMA | ATAC_CAP_NCQ;
	sc->sc_atac.atac_pio_cap = 4;
	sc->sc_atac.atac_dma_cap = 2;
	sc->sc_atac.atac_udma_cap = 6;
	sc->sc_atac.atac_channels = sc->sc_chanarray;
	sc->sc_atac.atac_probe = siisata_probe_drive;
	sc->sc_atac.atac_bustype_ata = &siisata_ata_bustype;
	sc->sc_atac.atac_set_modes = siisata_setup_channel;
#if NATAPIBUS > 0
	sc->sc_atac.atac_atapibus_attach = siisata_atapibus_attach;
#endif

	/* come out of reset state */
	GRWRITE(sc, GR_GC, 0);

	for (i = 0; i < sc->sc_atac.atac_nchannels; i++) {
		siisata_attach_port(sc, i);
	}

	SIISATA_DEBUG_PRINT(("%s: %s: GR_GC: 0x%08x\n", SIISATANAME(sc),
	    __func__, GRREAD(sc, GR_GC)), DEBUG_FUNCS);
	return;
}

static void
siisata_disable_port_interrupt(struct ata_channel *chp)
{
	struct siisata_softc *sc = (struct siisata_softc *)chp->ch_atac;

	PRWRITE(sc, PRX(chp->ch_channel, PRO_PIEC), 0xffffffff);
}

static void
siisata_enable_port_interrupt(struct ata_channel *chp)
{
	struct siisata_softc *sc = (struct siisata_softc *)chp->ch_atac;

	/* clear any interrupts */
	(void)PRREAD(sc, PRX(chp->ch_channel, PRO_PSS));
	PRWRITE(sc, PRX(chp->ch_channel, PRO_PIS), 0xffffffff);
	/* and enable CmdErrr+CmdCmpl interrupting */
	PRWRITE(sc, PRX(chp->ch_channel, PRO_PIES),
	    PR_PIS_CMDERRR | PR_PIS_CMDCMPL);
}

static int
siisata_init_port(struct siisata_softc *sc, int port)
{
	struct siisata_channel *schp;
	struct ata_channel *chp;
	int error;

	schp = &sc->sc_channels[port];
	chp = (struct ata_channel *)schp;

	/*
	 * Come out of reset. Disable no clearing of PR_PIS_CMDCMPL on read
	 * of PR_PSS. Disable 32-bit PRB activation, we use 64-bit activation.
	 */
	PRWRITE(sc, PRX(chp->ch_channel, PRO_PCC),
	    PR_PC_32BA | PR_PC_INCOR | PR_PC_PORT_RESET);
	/* initialize port */
	error = siisata_reinit_port(chp, -1);
	/* enable CmdErrr+CmdCmpl interrupting */
	siisata_enable_port_interrupt(chp);
	/* enable port interrupt */
	GRWRITE(sc, GR_GC, GRREAD(sc, GR_GC) | GR_GC_PXIE(chp->ch_channel));

	return error;
}

static void
siisata_attach_port(struct siisata_softc *sc, int port)
{
	int j;
	int dmasize;
	int error;
	void *prbp;
	struct siisata_channel *schp;
	struct ata_channel *chp;

	schp = &sc->sc_channels[port];
	chp = (struct ata_channel *)schp;
	sc->sc_chanarray[port] = chp;
	chp->ch_channel = port;
	chp->ch_atac = &sc->sc_atac;
	chp->ch_queue = ata_queue_alloc(SIISATA_MAX_SLOTS);
	if (chp->ch_queue == NULL) {
		aprint_error_dev(sc->sc_atac.atac_dev,
		    "port %d: can't allocate memory "
		    "for command queue\n", chp->ch_channel);
		return;
	}

	dmasize = SIISATA_CMD_SIZE * SIISATA_MAX_SLOTS;

	SIISATA_DEBUG_PRINT(("%s: %s: dmasize: %d\n", SIISATANAME(sc),
	    __func__, dmasize), DEBUG_FUNCS);

	error = bus_dmamem_alloc(sc->sc_dmat, dmasize, PAGE_SIZE, 0,
	    &schp->sch_prb_seg, 1, &schp->sch_prb_nseg, BUS_DMA_NOWAIT);
	if (error) {
		aprint_error_dev(sc->sc_atac.atac_dev,
		    "unable to allocate PRB table memory, "
		    "error=%d\n", error);
		return;
	}

	error = bus_dmamem_map(sc->sc_dmat,
	    &schp->sch_prb_seg, schp->sch_prb_nseg,
	    dmasize, &prbp, BUS_DMA_NOWAIT | BUS_DMA_COHERENT);
	if (error) {
		aprint_error_dev(sc->sc_atac.atac_dev,
		    "unable to map PRB table memory, "
		    "error=%d\n", error);
		bus_dmamem_free(sc->sc_dmat,
		    &schp->sch_prb_seg, schp->sch_prb_nseg);
		return;
	}

	error = bus_dmamap_create(sc->sc_dmat, dmasize, 1, dmasize, 0,
	    BUS_DMA_NOWAIT, &schp->sch_prbd);
	if (error) {
		aprint_error_dev(sc->sc_atac.atac_dev,
		    "unable to create PRB table map, "
		    "error=%d\n", error);
		bus_dmamem_unmap(sc->sc_dmat, prbp, dmasize);
		bus_dmamem_free(sc->sc_dmat,
		    &schp->sch_prb_seg, schp->sch_prb_nseg);
		return;
	}

	error = bus_dmamap_load(sc->sc_dmat, schp->sch_prbd,
	    prbp, dmasize, NULL, BUS_DMA_NOWAIT);
	if (error) {
		aprint_error_dev(sc->sc_atac.atac_dev,
		    "unable to load PRB table map, "
		    "error=%d\n", error);
		bus_dmamap_destroy(sc->sc_dmat, schp->sch_prbd);
		bus_dmamem_unmap(sc->sc_dmat, prbp, dmasize);
		bus_dmamem_free(sc->sc_dmat,
		    &schp->sch_prb_seg, schp->sch_prb_nseg);
		return;
	}

	for (j = 0; j < SIISATA_MAX_SLOTS; j++) {
		schp->sch_prb[j] = (struct siisata_prb *)
		    ((char *)prbp + SIISATA_CMD_SIZE * j);
		schp->sch_bus_prb[j] =
		    schp->sch_prbd->dm_segs[0].ds_addr +
		    SIISATA_CMD_SIZE * j;
		error = bus_dmamap_create(sc->sc_dmat, MAXPHYS,
		    SIISATA_NSGE, MAXPHYS, 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
		    &schp->sch_datad[j]);
		if (error) {
			aprint_error_dev(sc->sc_atac.atac_dev,
			    "couldn't create xfer DMA map, error=%d\n",
			    error);
			return;
		}
	}

	if (bus_space_subregion(sc->sc_prt, sc->sc_prh,
	    PRX(chp->ch_channel, PRO_SSTATUS), 4, &schp->sch_sstatus) != 0) {
		aprint_error_dev(sc->sc_atac.atac_dev,
		    "couldn't map port %d SStatus regs\n",
		    chp->ch_channel);
		return;
	}
	if (bus_space_subregion(sc->sc_prt, sc->sc_prh,
	    PRX(chp->ch_channel, PRO_SCONTROL), 4, &schp->sch_scontrol) != 0) {
		aprint_error_dev(sc->sc_atac.atac_dev,
		    "couldn't map port %d SControl regs\n",
		    chp->ch_channel);
		return;
	}
	if (bus_space_subregion(sc->sc_prt, sc->sc_prh,
	    PRX(chp->ch_channel, PRO_SERROR), 4, &schp->sch_serror) != 0) {
		aprint_error_dev(sc->sc_atac.atac_dev,
		    "couldn't map port %d SError regs\n",
		    chp->ch_channel);
		return;
	}

	(void)siisata_init_port(sc, port);

	ata_channel_attach(chp);

	return;
}

void
siisata_childdetached(struct siisata_softc *sc, device_t child)
{
	struct ata_channel *chp;

	for (int i = 0; i < sc->sc_atac.atac_nchannels; i++) {
		chp = sc->sc_chanarray[i];

		if (child == chp->atabus)
			chp->atabus = NULL;
	}
}

int
siisata_detach(struct siisata_softc *sc, int flags)
{
	struct atac_softc *atac = &sc->sc_atac;
	struct scsipi_adapter *adapt = &atac->atac_atapi_adapter._generic;
	struct siisata_channel *schp;
	struct ata_channel *chp;
	int i, j, error;

	if (adapt->adapt_refcnt != 0)
		return EBUSY;

	for (i = 0; i < sc->sc_atac.atac_nchannels; i++) {
		schp = &sc->sc_channels[i];
		chp = sc->sc_chanarray[i];

		if (chp->atabus != NULL) {
			if ((error = config_detach(chp->atabus, flags)) != 0)
				return error;

			KASSERT(chp->atabus == NULL);
		}

		if (chp->ch_flags & ATACH_DETACHED)
			continue;

		for (j = 0; j < SIISATA_MAX_SLOTS; j++)
			bus_dmamap_destroy(sc->sc_dmat, schp->sch_datad[j]);

		bus_dmamap_unload(sc->sc_dmat, schp->sch_prbd);
		bus_dmamap_destroy(sc->sc_dmat, schp->sch_prbd);
		bus_dmamem_unmap(sc->sc_dmat, schp->sch_prb[0],
		    SIISATA_CMD_SIZE * SIISATA_MAX_SLOTS);
		bus_dmamem_free(sc->sc_dmat,
		    &schp->sch_prb_seg, schp->sch_prb_nseg);

		ata_channel_detach(chp);
	}

	/* leave the chip in reset */
	GRWRITE(sc, GR_GC, GR_GC_GLBLRST);

	return 0;
}

void
siisata_resume(struct siisata_softc *sc)
{
	/* come out of reset state */
	GRWRITE(sc, GR_GC, 0);

	for (int port = 0; port < sc->sc_atac.atac_nchannels; port++) {
		int error;

		error = siisata_init_port(sc, port);
		if (error) {
			struct siisata_channel *schp = &sc->sc_channels[port];
			struct ata_channel *chp = (struct ata_channel *)schp;

			ata_channel_lock(chp);
			siisata_reset_channel(chp, AT_POLL);
			ata_channel_unlock(chp);
		}
	}
}

int
siisata_intr(void *v)
{
	struct siisata_softc *sc = v;
	uint32_t is;
	int i, r = 0;
	while ((is = GRREAD(sc, GR_GIS))) {
		SIISATA_DEBUG_PRINT(("%s: %s: GR_GIS: 0x%08x\n",
		    SIISATANAME(sc), __func__, is), DEBUG_INTR);
		r = 1;
		for (i = 0; i < sc->sc_atac.atac_nchannels; i++)
			if (is & GR_GIS_PXIS(i))
				siisata_intr_port(&sc->sc_channels[i]);
	}
	return r;
}

static void
siisata_intr_port(struct siisata_channel *schp)
{
	struct siisata_softc *sc =
	    (struct siisata_softc *)schp->ata_channel.ch_atac;
	struct ata_channel *chp = &schp->ata_channel;
	struct ata_xfer *xfer = NULL;
	uint32_t pss, pis, tfd = 0;
	bool recover = false;

	/* get slot status, clearing completion interrupt */
	pss = PRREAD(sc, PRX(chp->ch_channel, PRO_PSS));

	SIISATA_DEBUG_PRINT(("%s: %s port %d, pss 0x%x ",
	    SIISATANAME(sc), __func__, chp->ch_channel, pss),
	    DEBUG_INTR);

	if (__predict_true((pss & PR_PSS_ATTENTION) == 0)) {
		SIISATA_DEBUG_PRINT(("no attention"), DEBUG_INTR);
		goto process;
	}

	pis = PRREAD(sc, PRX(chp->ch_channel, PRO_PIS));

	SIISATA_DEBUG_PRINT(("pis 0x%x\n", pss), DEBUG_INTR);

	if (pis & PR_PIS_CMDERRR) {
		uint32_t ec;

		ec = PRREAD(sc, PRX(chp->ch_channel, PRO_PCE));
		SIISATA_DEBUG_PRINT(("ec %d\n", ec), DEBUG_INTR);

		/* emulate a CRC error by default */
		tfd = ATACH_ERR_ST(WDCE_CRC, WDCS_ERR);

		if (ec <= PR_PCE_DATAFISERROR) {
			if (ec == PR_PCE_DEVICEERROR &&
			    (chp->ch_flags & ATACH_NCQ) == 0 &&
			    (xfer = ata_queue_get_active_xfer(chp)) != NULL) {
				/* read in specific information about error */
				uint32_t prbfis = bus_space_read_stream_4(
				    sc->sc_prt, sc->sc_prh,
    				    PRSX(chp->ch_channel, xfer->c_slot,
				    PRSO_FIS));

				/* get status and error */
				int ntfd = satafis_rdh_parse(chp,
				    (uint8_t *)&prbfis);

				if (ATACH_ST(ntfd) & WDCS_ERR)
					tfd = ntfd;
			}

			/*
			 * We don't expect the recovery to trigger error,
			 * but handle this just in case.
			 */
			if (!ISSET(chp->ch_flags, ATACH_RECOVERING))
				recover = true;
			else {
				aprint_error_dev(sc->sc_atac.atac_dev,
				    "error ec %x while recovering\n", ec);

				/* Command will be marked as errored out */
				pss = 0;
			}
		} else {
			aprint_error_dev(sc->sc_atac.atac_dev, "fatal error %d"
			    " on port %d (ctx 0x%x), resetting\n",
			    ec, chp->ch_channel,
			    PRREAD(sc, PRX(chp->ch_channel, PRO_PCR)));

			/* okay, we have a "Fatal Error" */
			ata_channel_lock(chp);
			siisata_device_reset(chp);
			ata_channel_unlock(chp);
		}
	}

	/* clear some (ok, all) ints */
	PRWRITE(sc, PRX(chp->ch_channel, PRO_PIS), 0xffffffff);

	if (__predict_false(recover))
		ata_channel_freeze(chp);

process:
	if (xfer != NULL) {
		xfer->ops->c_intr(chp, xfer, tfd);
	} else {
		/*
		 * For NCQ, HBA halts processing when error is notified,
		 * and any further D2H FISes are ignored until the error
		 * condition is cleared. Hence if a command is inactive,
		 * it means it actually already finished successfully.
		 * Note: active slots can change as c_intr() callback
		 * can activate another command(s), so must only process
		 * commands active before we start processing.
		 */
		uint32_t aslots = ata_queue_active(chp);

		for (int slot = 0; slot < SIISATA_MAX_SLOTS; slot++) {
			if ((aslots & __BIT(slot)) != 0 &&
			    (pss & PR_PXSS(slot)) == 0) {
				xfer = ata_queue_hwslot_to_xfer(chp, slot);
				xfer->ops->c_intr(chp, xfer, 0);
			}
		}
	}

	if (__predict_false(recover)) {
		ata_channel_lock(chp);
		ata_channel_thaw_locked(chp);
		ata_thread_run(chp, 0, ATACH_TH_RECOVERY, tfd);
		ata_channel_unlock(chp);
	}
}

/* Recover channel after transfer aborted */
void
siisata_channel_recover(struct ata_channel *chp, int flags, uint32_t tfd)
{
	struct siisata_channel *schp = (struct siisata_channel *)chp;
	struct siisata_softc *sc =
	    (struct siisata_softc *)schp->ata_channel.ch_atac;
	int drive;

	ata_channel_lock_owned(chp);

	if (chp->ch_ndrives > PMP_PORT_CTL) {
		/* Get PM port number for the device in error */
		int pcr = PRREAD(sc, PRX(chp->ch_channel, PRO_PCR));
		drive = PRO_PCR_PMP(pcr);
	} else
		drive = 0;

	/*
	 * If BSY or DRQ bits are set, must execute COMRESET to return
	 * device to idle state. Otherwise, commands can be reissued
	 * after reinitalization of port. After that, need to execute
	 * READ LOG EXT for NCQ to unblock device processing if COMRESET
	 * was not done.
	 */
	if ((ATACH_ST(tfd) & (WDCS_BSY|WDCS_DRQ)) != 0) {
		siisata_device_reset(chp);
		goto out;
	}

	KASSERT(drive >= 0);
	(void)siisata_reinit_port(chp, drive);

	ata_recovery_resume(chp, drive, tfd, flags);

out:
	/* Drive unblocked, back to normal operation */
	return;
}

void
siisata_reset_drive(struct ata_drive_datas *drvp, int flags, uint32_t *sigp)
{
	struct ata_channel *chp = drvp->chnl_softc;
	struct siisata_softc *sc = (struct siisata_softc *)chp->ch_atac;
	struct siisata_channel *schp = (struct siisata_channel *)chp;
	struct siisata_prb *prb;
	uint8_t c_slot;
	uint32_t pss, pis;
	int i;
	bool timed_out;

	ata_channel_lock_owned(chp);

	if (siisata_reinit_port(chp, drvp->drive))
		siisata_reset_channel(chp, flags);

	/* get a slot for running the command on */
	if (!ata_queue_alloc_slot(chp, &c_slot, ATA_MAX_OPENINGS)) {
		panic("%s: %s: failed to get xfer for reset, port %d\n",
		    device_xname(sc->sc_atac.atac_dev),
		    __func__, chp->ch_channel);
		/* NOTREACHED */
	}

	prb = schp->sch_prb[c_slot];
	memset(prb, 0, SIISATA_CMD_SIZE);
	prb->prb_control =
	    htole16(PRB_CF_SOFT_RESET | PRB_CF_INTERRUPT_MASK);
	KASSERT(drvp->drive <= PMP_PORT_CTL);
	prb->prb_fis[rhd_c] = drvp->drive;

	siisata_disable_port_interrupt(chp);

	siisata_activate_prb(schp, c_slot);

	timed_out = true;
	for(i = 0; i < WDC_RESET_WAIT / 10; i++) {
		pss = PRREAD(sc, PRX(chp->ch_channel, PRO_PSS));
		if ((pss & PR_PXSS(c_slot)) == 0) {
			timed_out = false;
			break;
		}
		if (pss & PR_PSS_ATTENTION)
			break;
		ata_delay(chp, 10, "siiprb", flags);
	}

	siisata_deactivate_prb(schp, c_slot);

	if ((pss & PR_PSS_ATTENTION) != 0) {
		pis = PRREAD(sc, PRX(chp->ch_channel, PRO_PIS));
		const uint32_t ps = PRREAD(sc, PRX(chp->ch_channel, PRO_PS));
		const u_int slot = PR_PS_ACTIVE_SLOT(ps);

		if (slot != c_slot)
			device_printf(sc->sc_atac.atac_dev, "%s port %d "
			    "drive %d slot %d c_slot %d", __func__,
			    chp->ch_channel, drvp->drive, slot, c_slot);

		PRWRITE(sc, PRX(chp->ch_channel, PRO_PIS), pis &
		    PR_PIS_CMDERRR);
	}

	siisata_enable_port_interrupt(chp);

	if (timed_out) {
		/* timeout */
		siisata_device_reset(chp);	/* XXX is this right? */
		if (sigp)
			*sigp = 0xffffffff;
	} else {
		/* read the signature out of the FIS */
		if (sigp) {
			*sigp = 0;
			*sigp |= (PRREAD(sc, PRSX(chp->ch_channel, c_slot,
			    PRSO_FIS+0x4)) & 0x00ffffff) << 8;
			*sigp |= PRREAD(sc, PRSX(chp->ch_channel, c_slot,
			    PRSO_FIS+0xc)) & 0xff;
		}
	}

	ata_queue_free_slot(chp, c_slot);
}

void
siisata_reset_channel(struct ata_channel *chp, int flags)
{
	struct siisata_softc *sc = (struct siisata_softc *)chp->ch_atac;
	struct siisata_channel *schp = (struct siisata_channel *)chp;

	SIISATA_DEBUG_PRINT(("%s: %s port %d\n", SIISATANAME(sc), __func__,
	    chp->ch_channel), DEBUG_FUNCS);

	ata_channel_lock_owned(chp);

	if (sata_reset_interface(chp, sc->sc_prt, schp->sch_scontrol,
	    schp->sch_sstatus, flags) != SStatus_DET_DEV) {
		aprint_error("%s port %d: reset failed\n",
		    SIISATANAME(sc), chp->ch_channel);
		/* XXX and then ? */
	}

	siisata_device_reset(chp);

	PRWRITE(sc, PRX(chp->ch_channel, PRO_SERROR),
	    PRREAD(sc, PRX(chp->ch_channel, PRO_SERROR)));

	return;
}

int
siisata_ata_addref(struct ata_drive_datas *drvp)
{
	return 0;
}

void
siisata_ata_delref(struct ata_drive_datas *drvp)
{
	return;
}

void
siisata_killpending(struct ata_drive_datas *drvp)
{
	return;
}

void
siisata_probe_drive(struct ata_channel *chp)
{
	struct siisata_softc *sc = (struct siisata_softc *)chp->ch_atac;
	struct siisata_channel *schp = (struct siisata_channel *)chp;
	int i;
	uint32_t sig;
	struct siisata_prb *prb;
	bool timed_out;
	uint8_t c_slot;

	SIISATA_DEBUG_PRINT(("%s: %s: port %d start\n", SIISATANAME(sc),
	    __func__, chp->ch_channel), DEBUG_FUNCS);

	ata_channel_lock(chp);

	/* get a slot for running the command on */
	if (!ata_queue_alloc_slot(chp, &c_slot, ATA_MAX_OPENINGS)) {
		aprint_error_dev(sc->sc_atac.atac_dev,
		    "%s: failed to get xfer port %d\n",
		    __func__, chp->ch_channel);
		ata_channel_unlock(chp);
		return;
	}

	/*
	 * disable port interrupt as we're polling for PHY up and
	 * prb completion
	 */
	siisata_disable_port_interrupt(chp);

	switch(sata_reset_interface(chp, sc->sc_prt, schp->sch_scontrol,
		schp->sch_sstatus, AT_WAIT)) {
	case SStatus_DET_DEV:
		/* clear any interrupts */
		(void)PRREAD(sc, PRX(chp->ch_channel, PRO_PSS));
		PRWRITE(sc, PRX(chp->ch_channel, PRO_PIS), 0xffffffff);

		/* wait for ready */
		timed_out = 1;
		for (i = 0; i < ATA_DELAY / 10; i++) {
			if (PRREAD(sc, PRX(chp->ch_channel, PRO_PS)) &
			    PR_PS_PORT_READY) {
				timed_out = 0;
				break;
			}

			ata_delay(chp, 10, "siiprbrd", AT_WAIT);
		}
		if (timed_out) {
			aprint_error_dev(sc->sc_atac.atac_dev,
			    "timed out waiting for PORT_READY on port %d, "
			    "reinitializing\n", chp->ch_channel);
			if (siisata_reinit_port(chp, -1))
				siisata_reset_channel(chp, AT_WAIT);
		}

		prb = schp->sch_prb[c_slot];
		memset(prb, 0, SIISATA_CMD_SIZE);
		prb->prb_control = htole16(PRB_CF_SOFT_RESET);
		prb->prb_fis[rhd_c] = PMP_PORT_CTL;

		siisata_activate_prb(schp, c_slot);

		timed_out = 1;
		for(i = 0; i < WDC_RESET_WAIT / 10; i++) {
			if ((PRREAD(sc, PRX(chp->ch_channel, PRO_PSS)) &
			    PR_PXSS(c_slot)) == 0) {
				/* prb completed */
				timed_out = 0;
				break;
			}
			if (PRREAD(sc, PRX(chp->ch_channel, PRO_PIS)) &
			    PR_PIS_CMDERRR) {
				/* we got an error; handle as timeout */
				break;
			}

			ata_delay(chp, 10, "siiprb", AT_WAIT);
		}

		siisata_deactivate_prb(schp, c_slot);

		if (timed_out) {
			aprint_error_dev(sc->sc_atac.atac_dev,
			    "SOFT_RESET failed on port %d (error %d PSS 0x%x PIS 0x%x), "
			    "resetting\n", chp->ch_channel,
			    PRREAD(sc, PRX(chp->ch_channel, PRO_PCE)),
			    PRREAD(sc, PRX(chp->ch_channel, PRO_PSS)),
			    PRREAD(sc, PRX(chp->ch_channel, PRO_PIS)));
			if (siisata_reinit_port(chp, -1))
				siisata_reset_channel(chp, AT_WAIT);
			break;
		}

		/* read the signature out of the FIS */
		sig = 0;
		sig |= (PRREAD(sc, PRSX(chp->ch_channel, c_slot,
		    PRSO_FIS+0x4)) & 0x00ffffff) << 8;
		sig |= PRREAD(sc, PRSX(chp->ch_channel, c_slot,
		    PRSO_FIS+0xc)) & 0xff;

		SIISATA_DEBUG_PRINT(("%s: %s: sig=0x%08x\n", SIISATANAME(sc),
		    __func__, sig), DEBUG_PROBE);

		if (sig == 0x96690101)
			PRWRITE(sc, PRX(chp->ch_channel, PRO_PCS),
			    PR_PC_PMP_ENABLE);
		sata_interpret_sig(chp, 0, sig);
		break;
	default:
		break;
	}

	siisata_enable_port_interrupt(chp);

	ata_queue_free_slot(chp, c_slot);

	ata_channel_unlock(chp);

	SIISATA_DEBUG_PRINT(("%s: %s: port %d done\n", SIISATANAME(sc),
	    __func__, chp->ch_channel), DEBUG_PROBE);
	return;
}

void
siisata_setup_channel(struct ata_channel *chp)
{
	return;
}

static const struct ata_xfer_ops siisata_cmd_xfer_ops = {
	.c_start = siisata_cmd_start,
	.c_intr = siisata_cmd_complete,
	.c_poll = siisata_cmd_poll,
	.c_abort = siisata_cmd_abort,
	.c_kill_xfer = siisata_cmd_kill_xfer,
};

void
siisata_exec_command(struct ata_drive_datas *drvp, struct ata_xfer *xfer)
{
	struct ata_channel *chp = drvp->chnl_softc;
	struct ata_command *ata_c = &xfer->c_ata_c;

	SIISATA_DEBUG_PRINT(("%s: %s begins\n",
	    SIISATANAME((struct siisata_softc *)chp->ch_atac), __func__),
	    DEBUG_FUNCS);

	if (ata_c->flags & AT_POLL)
		xfer->c_flags |= C_POLL;
	if (ata_c->flags & AT_WAIT)
		xfer->c_flags |= C_WAIT;
	xfer->c_drive = drvp->drive;
	xfer->c_databuf = ata_c->data;
	xfer->c_bcount = ata_c->bcount;
	xfer->ops = &siisata_cmd_xfer_ops;

	ata_exec_xfer(chp, xfer);

	SIISATA_DEBUG_PRINT( ("%s: %s ends\n",
	    SIISATANAME((struct siisata_softc *)chp->ch_atac), __func__),
	    DEBUG_FUNCS);
}

int
siisata_cmd_start(struct ata_channel *chp, struct ata_xfer *xfer)
{
	struct siisata_channel *schp = (struct siisata_channel *)chp;
	struct ata_command *ata_c = &xfer->c_ata_c;
	struct siisata_prb *prb;

	SIISATA_DEBUG_PRINT(("%s: %s port %d drive %d command 0x%x, slot %d\n",
	    SIISATANAME((struct siisata_softc *)chp->ch_atac), __func__,
	    chp->ch_channel, xfer->c_drive, ata_c->r_command, xfer->c_slot),
	    DEBUG_FUNCS|DEBUG_XFERS);

	ata_channel_lock_owned(chp);

	prb = schp->sch_prb[xfer->c_slot];
	memset(prb, 0, SIISATA_CMD_SIZE);

	satafis_rhd_construct_cmd(ata_c, prb->prb_fis);
	KASSERT(xfer->c_drive <= PMP_PORT_CTL);
	prb->prb_fis[rhd_c] |= xfer->c_drive;

	if (ata_c->r_command == ATA_DATA_SET_MANAGEMENT) {
		prb->prb_control |= htole16(PRB_CF_PROTOCOL_OVERRIDE);
		prb->prb_protocol_override |= htole16(PRB_PO_WRITE);
	}

	if (siisata_dma_setup(chp, xfer->c_slot,
	    (ata_c->flags & (AT_READ | AT_WRITE)) ? ata_c->data : NULL,
	    ata_c->bcount,
	    (ata_c->flags & AT_READ) ? BUS_DMA_READ : BUS_DMA_WRITE)) {
		ata_c->flags |= AT_DF;
		return ATASTART_ABORT;
	}

	if (xfer->c_flags & C_POLL) {
		/* polled command, disable interrupts */
		prb->prb_control |= htole16(PRB_CF_INTERRUPT_MASK);
		siisata_disable_port_interrupt(chp);
	}

	/* go for it */
	siisata_activate_prb(schp, xfer->c_slot);

	if ((ata_c->flags & AT_POLL) == 0) {
		callout_reset(&chp->c_timo_callout, mstohz(ata_c->timeout),
		    ata_timeout, chp);
		return ATASTART_STARTED;
	} else
		return ATASTART_POLL;
}

void
siisata_cmd_poll(struct ata_channel *chp, struct ata_xfer *xfer)
{
	struct siisata_channel *schp = (struct siisata_channel *)chp;

	/*
	 * polled command
	 */
	for (int i = 0; i < xfer->c_ata_c.timeout * 10; i++) {
		if (xfer->c_ata_c.flags & AT_DONE)
			break;
		siisata_intr_port(schp);
		DELAY(100);
	}

	if ((xfer->c_ata_c.flags & AT_DONE) == 0) {
		ata_timeout(xfer);
	}

	/* reenable interrupts */
	siisata_enable_port_interrupt(chp);

	SIISATA_DEBUG_PRINT(("%s: %s: done\n",
	    SIISATANAME((struct siisata_softc *)chp->ch_atac), __func__),
	    DEBUG_FUNCS);
}

void
siisata_cmd_abort(struct ata_channel *chp, struct ata_xfer *xfer)
{
	siisata_cmd_complete(chp, xfer, 0);
}

void
siisata_cmd_kill_xfer(struct ata_channel *chp, struct ata_xfer *xfer,
    int reason)
{
	struct ata_command *ata_c = &xfer->c_ata_c;
	struct siisata_channel *schp = (struct siisata_channel *)chp;
	bool deactivate = true;

	switch (reason) {
	case KILL_GONE_INACTIVE:
		deactivate = false;
		/* FALLTHROUGH */
	case KILL_GONE:
		ata_c->flags |= AT_GONE;
		break;
	case KILL_RESET:
		ata_c->flags |= AT_RESET;
		break;
	case KILL_REQUEUE:
		panic("%s: not supposed to be requeued\n", __func__);
		break;
	default:
		panic("%s: port %d: unknown reason %d",
		   __func__, chp->ch_channel, reason);
	}

	siisata_cmd_done_end(chp, xfer);

	if (deactivate) {
		siisata_deactivate_prb(schp, xfer->c_slot);
		ata_deactivate_xfer(chp, xfer);
	}
}

int
siisata_cmd_complete(struct ata_channel *chp, struct ata_xfer *xfer, int tfd)
{
	struct siisata_channel *schp = (struct siisata_channel *)chp;
	struct ata_command *ata_c = &xfer->c_ata_c;
#ifdef SIISATA_DEBUG
	struct siisata_softc *sc = (struct siisata_softc *)chp->ch_atac;
#endif

	SIISATA_DEBUG_PRINT(("%s: %s: port %d slot %d\n",
	    SIISATANAME(sc), __func__,
	    chp->ch_channel, xfer->c_slot), DEBUG_FUNCS);
	SIISATA_DEBUG_PRINT(("%s: %s\n", SIISATANAME(sc), __func__),
	    DEBUG_FUNCS|DEBUG_XFERS);

	if (ata_waitdrain_xfer_check(chp, xfer))
		return 0;

	if (xfer->c_flags & C_TIMEOU)
		ata_c->flags |= AT_TIMEOU;

	if (ATACH_ST(tfd) & WDCS_BSY) {
		ata_c->flags |= AT_TIMEOU;
	} else if (ATACH_ST(tfd) & WDCS_ERR) {
		ata_c->r_error = ATACH_ERR(tfd);
		ata_c->flags |= AT_ERROR;
	}

	siisata_cmd_done(chp, xfer, tfd);

	siisata_deactivate_prb(schp, xfer->c_slot);
	ata_deactivate_xfer(chp, xfer);

	if ((ata_c->flags & (AT_TIMEOU|AT_ERROR)) == 0)
		atastart(chp);

	return 0;
}

void
siisata_cmd_done(struct ata_channel *chp, struct ata_xfer *xfer, int tfd)
{
	uint32_t fis[howmany(RDH_FISLEN,sizeof(uint32_t))];
	struct siisata_softc *sc = (struct siisata_softc *)chp->ch_atac;
	struct siisata_channel *schp = (struct siisata_channel *)chp;
	struct ata_command *ata_c = &xfer->c_ata_c;
	uint16_t *idwordbuf;
	int i;

	SIISATA_DEBUG_PRINT(("%s: %s flags 0x%x error 0x%x\n", SIISATANAME(sc),
	    __func__, ata_c->flags, ata_c->r_error), DEBUG_FUNCS|DEBUG_XFERS);

	if (ata_c->flags & (AT_READ | AT_WRITE)) {
		bus_dmamap_sync(sc->sc_dmat, schp->sch_datad[xfer->c_slot], 0,
		    schp->sch_datad[xfer->c_slot]->dm_mapsize,
		    (ata_c->flags & AT_READ) ? BUS_DMASYNC_POSTREAD :
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, schp->sch_datad[xfer->c_slot]);
	}

	if (ata_c->flags & AT_READREG) {
		bus_space_read_region_stream_4(sc->sc_prt, sc->sc_prh,
		    PRSX(chp->ch_channel, xfer->c_slot, PRSO_FIS),
		    fis, __arraycount(fis));
		satafis_rdh_cmd_readreg(ata_c, (uint8_t *)fis);
	}

	/* correct the endianess of IDENTIFY data */
	if (ata_c->r_command == WDCC_IDENTIFY ||
	    ata_c->r_command == ATAPI_IDENTIFY_DEVICE) {
		idwordbuf = xfer->c_databuf;
		for (i = 0; i < (xfer->c_bcount / sizeof(*idwordbuf)); i++) {
			idwordbuf[i] = le16toh(idwordbuf[i]);
		}
	}

	if (PRREAD(sc, PRSX(chp->ch_channel, xfer->c_slot, PRSO_RTC)))
		ata_c->flags |= AT_XFDONE;

	siisata_cmd_done_end(chp, xfer);
}

static void
siisata_cmd_done_end(struct ata_channel *chp, struct ata_xfer *xfer)
{
	struct ata_command *ata_c = &xfer->c_ata_c;

	ata_c->flags |= AT_DONE;
}

static const struct ata_xfer_ops siisata_bio_xfer_ops = {
	.c_start = siisata_bio_start,
	.c_intr = siisata_bio_complete,
	.c_poll = siisata_bio_poll,
	.c_abort = siisata_bio_abort,
	.c_kill_xfer = siisata_bio_kill_xfer,
};

void
siisata_ata_bio(struct ata_drive_datas *drvp, struct ata_xfer *xfer)
{
	struct ata_channel *chp = drvp->chnl_softc;
	struct ata_bio *ata_bio = &xfer->c_bio;

	SIISATA_DEBUG_PRINT(("%s: %s.\n",
	    SIISATANAME((struct siisata_softc *)chp->ch_atac), __func__),
	    DEBUG_FUNCS);

	if (ata_bio->flags & ATA_POLL)
		xfer->c_flags |= C_POLL;
	xfer->c_drive = drvp->drive;
	xfer->c_databuf = ata_bio->databuf;
	xfer->c_bcount = ata_bio->bcount;
	xfer->ops = &siisata_bio_xfer_ops;
	ata_exec_xfer(chp, xfer);
}

int
siisata_bio_start(struct ata_channel *chp, struct ata_xfer *xfer)
{
	struct siisata_channel *schp = (struct siisata_channel *)chp;
	struct siisata_prb *prb;
	struct ata_bio *ata_bio = &xfer->c_bio;

	SIISATA_DEBUG_PRINT(("%s: %s port %d slot %d drive %d\n",
	    SIISATANAME((struct siisata_softc *)chp->ch_atac), __func__,
	    chp->ch_channel, xfer->c_slot, xfer->c_drive), DEBUG_FUNCS);

	ata_channel_lock_owned(chp);

	prb = schp->sch_prb[xfer->c_slot];
	memset(prb, 0, SIISATA_CMD_SIZE);

	satafis_rhd_construct_bio(xfer, prb->prb_fis);
	KASSERT(xfer->c_drive <= PMP_PORT_CTL);
	prb->prb_fis[rhd_c] |= xfer->c_drive;

	if (siisata_dma_setup(chp, xfer->c_slot, ata_bio->databuf, ata_bio->bcount,
	    (ata_bio->flags & ATA_READ) ? BUS_DMA_READ : BUS_DMA_WRITE)) {
		ata_bio->error = ERR_DMA;
		ata_bio->r_error = 0;
		return ATASTART_ABORT;
	}

	if (xfer->c_flags & C_POLL) {
		/* polled command, disable interrupts */
		prb->prb_control |= htole16(PRB_CF_INTERRUPT_MASK);
		siisata_disable_port_interrupt(chp);
	}

	siisata_activate_prb(schp, xfer->c_slot);

	if ((ata_bio->flags & ATA_POLL) == 0) {
		callout_reset(&chp->c_timo_callout, mstohz(ATA_DELAY),
		    ata_timeout, chp);
		return ATASTART_STARTED;
	} else
		return ATASTART_POLL;
}

void
siisata_bio_poll(struct ata_channel *chp, struct ata_xfer *xfer)
{
	struct siisata_channel *schp = (struct siisata_channel *)chp;

	/*
	 * polled command
	 */
	for (int i = 0; i < ATA_DELAY * 10; i++) {
		if (xfer->c_bio.flags & ATA_ITSDONE)
			break;
		siisata_intr_port(schp);
		DELAY(100);
	}

	if ((xfer->c_bio.flags & ATA_ITSDONE) == 0) {
		ata_timeout(xfer);
	}

	siisata_enable_port_interrupt(chp);

	SIISATA_DEBUG_PRINT(("%s: %s: done\n",
	    SIISATANAME((struct siisata_softc *)chp->ch_atac), __func__),
	    DEBUG_FUNCS);
}

void
siisata_bio_abort(struct ata_channel *chp, struct ata_xfer *xfer)
{
	siisata_cmd_complete(chp, xfer, 0);
}

void
siisata_bio_kill_xfer(struct ata_channel *chp, struct ata_xfer *xfer,
    int reason)
{
	struct siisata_channel *schp = (struct siisata_channel *)chp;
	struct ata_bio *ata_bio = &xfer->c_bio;
	int drive = xfer->c_drive;
	bool deactivate = true;

	SIISATA_DEBUG_PRINT(("%s: %s: port %d slot %d\n",
	    SIISATANAME((struct siisata_softc *)chp->ch_atac), __func__,
	    chp->ch_channel, xfer->c_slot), DEBUG_FUNCS);

	ata_bio->flags |= ATA_ITSDONE;
	switch (reason) {
	case KILL_GONE_INACTIVE:
		deactivate = false;
		/* FALLTHROUGH */
	case KILL_GONE:
		ata_bio->error = ERR_NODEV;
		break;
	case KILL_RESET:
		ata_bio->error = ERR_RESET;
		break;
	case KILL_REQUEUE:
		ata_bio->error = REQUEUE;
		break;
	default:
		panic("%s: port %d: unknown reason %d",
		   __func__, chp->ch_channel, reason);
	}
	ata_bio->r_error = WDCE_ABRT;

	if (deactivate) {
		siisata_deactivate_prb(schp, xfer->c_slot);
		ata_deactivate_xfer(chp, xfer);
	}

	(*chp->ch_drive[drive].drv_done)(chp->ch_drive[drive].drv_softc, xfer);
}

int
siisata_bio_complete(struct ata_channel *chp, struct ata_xfer *xfer, int tfd)
{
	struct siisata_softc *sc = (struct siisata_softc *)chp->ch_atac;
	struct siisata_channel *schp = (struct siisata_channel *)chp;
	struct ata_bio *ata_bio = &xfer->c_bio;
	int drive = xfer->c_drive;

	SIISATA_DEBUG_PRINT(("%s: %s: port %d slot %d drive %d tfd %x\n",
	    SIISATANAME((struct siisata_softc *)chp->ch_atac), __func__,
	    chp->ch_channel, xfer->c_slot, xfer->c_drive, tfd), DEBUG_FUNCS);

	if (ata_waitdrain_xfer_check(chp, xfer))
		return 0;

	if (xfer->c_flags & C_TIMEOU) {
		ata_bio->error = TIMEOUT;
	}

	bus_dmamap_sync(sc->sc_dmat, schp->sch_datad[xfer->c_slot], 0,
	    schp->sch_datad[xfer->c_slot]->dm_mapsize,
	    (ata_bio->flags & ATA_READ) ? BUS_DMASYNC_POSTREAD :
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->sc_dmat, schp->sch_datad[xfer->c_slot]);

	ata_bio->flags |= ATA_ITSDONE;
	if (ATACH_ST(tfd) & WDCS_DWF) {
		ata_bio->error = ERR_DF;
	} else if (ATACH_ST(tfd) & WDCS_ERR) {
		ata_bio->error = ERROR;
		ata_bio->r_error = ATACH_ERR(tfd);
	} else if (ATACH_ST(tfd) & WDCS_CORR)
		ata_bio->flags |= ATA_CORR;

	SIISATA_DEBUG_PRINT(("%s: %s bcount: %ld", SIISATANAME(sc), __func__,
	    ata_bio->bcount), DEBUG_XFERS);
	if (ata_bio->error == NOERROR) {
		if ((xfer->c_flags & C_NCQ) != 0 && ata_bio->flags & ATA_READ)
			ata_bio->bcount -=
			    PRREAD(sc, PRSX(chp->ch_channel, xfer->c_slot, PRSO_RTC));
		else
			ata_bio->bcount = 0;
	}
	SIISATA_DEBUG_PRINT((" now %ld\n", ata_bio->bcount), DEBUG_XFERS);

	siisata_deactivate_prb(schp, xfer->c_slot);
	ata_deactivate_xfer(chp, xfer);

	(*chp->ch_drive[drive].drv_done)(chp->ch_drive[drive].drv_softc, xfer);
	if ((ATACH_ST(tfd) & WDCS_ERR) == 0)
		atastart(chp);
	return 0;
}

static int
siisata_dma_setup(struct ata_channel *chp, int slot, void *data,
    size_t count, int op)
{

	int error, seg;
	struct siisata_softc *sc = (struct siisata_softc *)chp->ch_atac;
	struct siisata_channel *schp = (struct siisata_channel *)chp;

	struct siisata_prb *prbp;

	prbp = schp->sch_prb[slot];

	if (data == NULL) {
		goto end;
	}

	error = bus_dmamap_load(sc->sc_dmat, schp->sch_datad[slot],
	    data, count, NULL, BUS_DMA_NOWAIT | BUS_DMA_STREAMING | op);
	if (error) {
		aprint_error("%s port %d: "
		    "failed to load xfer in slot %d: error %d\n",
		    SIISATANAME(sc), chp->ch_channel, slot, error);
		return error;
	}

	bus_dmamap_sync(sc->sc_dmat, schp->sch_datad[slot], 0,
	    schp->sch_datad[slot]->dm_mapsize,
	    (op == BUS_DMA_READ) ? BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

	SIISATA_DEBUG_PRINT(("%s: %d segs, %ld count\n", __func__,
	    schp->sch_datad[slot]->dm_nsegs, (long unsigned int) count),
	    DEBUG_FUNCS | DEBUG_DEBUG);

	for (seg = 0; seg < schp->sch_datad[slot]->dm_nsegs; seg++) {
		prbp->prb_sge[seg].sge_da =
		    htole64(schp->sch_datad[slot]->dm_segs[seg].ds_addr);
		prbp->prb_sge[seg].sge_dc =
		    htole32(schp->sch_datad[slot]->dm_segs[seg].ds_len);
		prbp->prb_sge[seg].sge_flags = htole32(0);
	}
	prbp->prb_sge[seg - 1].sge_flags |= htole32(SGE_FLAG_TRM);
end:
	return 0;
}

static void
siisata_activate_prb(struct siisata_channel *schp, int slot)
{
	struct siisata_softc *sc;
	bus_size_t offset;
	uint64_t pprb;

	sc = (struct siisata_softc *)schp->ata_channel.ch_atac;

	SIISATA_PRB_SYNC(sc, schp, slot, BUS_DMASYNC_PREWRITE);

	offset = PRO_CARX(schp->ata_channel.ch_channel, slot);

	pprb = schp->sch_bus_prb[slot];

	PRWRITE(sc, offset + 0, pprb >>  0);
	PRWRITE(sc, offset + 4, pprb >> 32);
}

static void
siisata_deactivate_prb(struct siisata_channel *schp, int slot)
{
	struct siisata_softc *sc;

	sc = (struct siisata_softc *)schp->ata_channel.ch_atac;

	SIISATA_PRB_SYNC(sc, schp, slot, BUS_DMASYNC_POSTWRITE);
}

static int
siisata_reinit_port(struct ata_channel *chp, int drive)
{
	struct siisata_softc *sc = (struct siisata_softc *)chp->ch_atac;
	int ps;
	int error = 0;

	if (chp->ch_ndrives > 1) {
		/*
		 * Proper recovery would SET this bit, which makes it
		 * not possible to submit new commands and resume execution
		 * on non-errored drives, then wait for those commands,
		 * to finish, and only then clear the bit and reset the state.
		 * For now this is okay, since we never queue commands for
		 * more than one drive.
		 * XXX FIS-based switching
		 */
		PRWRITE(sc, PRX(chp->ch_channel, PRO_PCC), PR_PC_RESUME);

	        for (int i = 0; i < chp->ch_ndrives; i++) {
			if (drive >= 0 && i != drive)
				continue;

			PRWRITE(sc, PRX(chp->ch_channel, PRO_PMPSTS(i)), 0);
			PRWRITE(sc, PRX(chp->ch_channel, PRO_PMPQACT(i)), 0);
		}
	}

	PRWRITE(sc, PRX(chp->ch_channel, PRO_PCS), PR_PC_PORT_INITIALIZE);
	for (int i = 0; i < ATA_DELAY * 100; i++) {
		ps = PRREAD(sc, PRX(chp->ch_channel, PRO_PS));
		if ((ps & PR_PS_PORT_READY) != 0)
			break;

		DELAY(10);
	}
	if ((ps & PR_PS_PORT_READY) == 0) {
		printf("%s: timeout waiting for port to be ready\n", __func__);
		error = EBUSY;
	}

	if (chp->ch_ndrives > 1)
		PRWRITE(sc, PRX(chp->ch_channel, PRO_PCS), PR_PC_PMP_ENABLE);

	return error;
}

static void
siisata_device_reset(struct ata_channel *chp)
{
	struct siisata_softc *sc = (struct siisata_softc *)chp->ch_atac;
	int ps;

	ata_channel_lock_owned(chp);

	/*
	 * This is always called after siisata_reinit_port(), so don't
	 * need to deal with RESUME and clearing device error state.
	 */

	PRWRITE(sc, PRX(chp->ch_channel, PRO_PCS), PR_PC_DEVICE_RESET);

	for (int i = 0; i < ATA_DELAY * 100; i++) {
		ps = PRREAD(sc, PRX(chp->ch_channel, PRO_PS));
		if ((ps & PR_PS_PORT_READY) != 0)
			break;

		DELAY(10);
	}
	if ((ps & PR_PS_PORT_READY) == 0) {
		printf("%s: timeout waiting for port to be ready\n", __func__);
		siisata_reset_channel(chp, AT_POLL);
	}

	ata_kill_active(chp, KILL_RESET, 0);
}


#if NATAPIBUS > 0
void
siisata_atapibus_attach(struct atabus_softc *ata_sc)
{
	struct ata_channel *chp = ata_sc->sc_chan;
	struct atac_softc *atac = chp->ch_atac;
	struct scsipi_adapter *adapt = &atac->atac_atapi_adapter._generic;
	struct scsipi_channel *chan = &chp->ch_atapi_channel;

	/*
	 * Fill in the scsipi_adapter.
	 */
	adapt->adapt_dev = atac->atac_dev;
	adapt->adapt_nchannels = atac->atac_nchannels;
	adapt->adapt_request = siisata_atapi_scsipi_request;
	adapt->adapt_minphys = siisata_atapi_minphys;
	atac->atac_atapi_adapter.atapi_probe_device =
	    siisata_atapi_probe_device;

	/*
	 * Fill in the scsipi_channel.
	 */
	memset(chan, 0, sizeof(*chan));
	chan->chan_adapter = adapt;
	chan->chan_bustype = &siisata_atapi_bustype;
	chan->chan_channel = chp->ch_channel;
	chan->chan_flags = SCSIPI_CHAN_OPENINGS;
	chan->chan_openings = 1;
	chan->chan_max_periph = 1;
	chan->chan_ntargets = 1;
	chan->chan_nluns = 1;

	chp->atapibus = config_found(ata_sc->sc_dev, chan, atapiprint,
	    CFARGS(.iattr = "atapi"));
}

void
siisata_atapi_minphys(struct buf *bp)
{
	if (bp->b_bcount > MAXPHYS)
		bp->b_bcount = MAXPHYS;
	minphys(bp);
}

/*
 * Kill off all pending xfers for a periph.
 *
 * Must be called at splbio().
 */
void
siisata_atapi_kill_pending(struct scsipi_periph *periph)
{
	struct atac_softc *atac =
	    device_private(periph->periph_channel->chan_adapter->adapt_dev);
	struct ata_channel *chp =
	    atac->atac_channels[periph->periph_channel->chan_channel];

	ata_kill_pending(&chp->ch_drive[periph->periph_target]);
}

void
siisata_atapi_kill_xfer(struct ata_channel *chp, struct ata_xfer *xfer,
    int reason)
{
	struct scsipi_xfer *sc_xfer = xfer->c_scsipi;
	struct siisata_channel *schp = (struct siisata_channel *)chp;
	bool deactivate = true;

	/* remove this command from xfer queue */
	switch (reason) {
	case KILL_GONE_INACTIVE:
		deactivate = false;
		/* FALLTHROUGH */
	case KILL_GONE:
		sc_xfer->error = XS_DRIVER_STUFFUP;
		break;
	case KILL_RESET:
		sc_xfer->error = XS_RESET;
		break;
	case KILL_REQUEUE:
		sc_xfer->error = XS_REQUEUE;
		break;
	default:
		panic("%s: port %d: unknown reason %d",
		   __func__, chp->ch_channel, reason);
	}

	if (deactivate) {
		siisata_deactivate_prb(schp, xfer->c_slot);
		ata_deactivate_xfer(chp, xfer);
	}

	ata_free_xfer(chp, xfer);
	scsipi_done(sc_xfer);
}

void
siisata_atapi_probe_device(struct atapibus_softc *sc, int target)
{
	struct scsipi_channel *chan = sc->sc_channel;
	struct scsipi_periph *periph;
	struct ataparams ids;
	struct ataparams *id = &ids;
	struct siisata_softc *siic =
	    device_private(chan->chan_adapter->adapt_dev);
	struct atac_softc *atac = &siic->sc_atac;
	struct ata_channel *chp = atac->atac_channels[chan->chan_channel];
	struct ata_drive_datas *drvp = &chp->ch_drive[target];
	struct scsipibus_attach_args sa;
	char serial_number[21], model[41], firmware_revision[9];
	int s;

	/* skip if already attached */
	if (scsipi_lookup_periph(chan, target, 0) != NULL)
		return;

	/* if no ATAPI device detected at attach time, skip */
	if (drvp->drive_type != ATA_DRIVET_ATAPI) {
		SIISATA_DEBUG_PRINT(("%s: drive %d not present\n", __func__,
		    target), DEBUG_PROBE);
		return;
	}

	/* Some ATAPI devices need a bit more time after software reset. */
	DELAY(5000);
	if (ata_get_params(drvp, AT_WAIT, id) == 0) {
#ifdef ATAPI_DEBUG_PROBE
		log(LOG_DEBUG, "%s drive %d: cmdsz 0x%x drqtype 0x%x\n",
		    device_xname(sc->sc_dev), target,
		    id->atap_config & ATAPI_CFG_CMD_MASK,
		    id->atap_config & ATAPI_CFG_DRQ_MASK);
#endif
		periph = scsipi_alloc_periph(M_WAITOK);
		periph->periph_dev = NULL;
		periph->periph_channel = chan;
		periph->periph_switch = &atapi_probe_periphsw;
		periph->periph_target = target;
		periph->periph_lun = 0;
		periph->periph_quirks = PQUIRK_ONLYBIG;

#ifdef SCSIPI_DEBUG
		if (SCSIPI_DEBUG_TYPE == SCSIPI_BUSTYPE_ATAPI &&
		    SCSIPI_DEBUG_TARGET == target)
			periph->periph_dbflags |= SCSIPI_DEBUG_FLAGS;
#endif
		periph->periph_type = ATAPI_CFG_TYPE(id->atap_config);
		if (id->atap_config & ATAPI_CFG_REMOV)
			periph->periph_flags |= PERIPH_REMOVABLE;
		sa.sa_periph = periph;
		sa.sa_inqbuf.type = ATAPI_CFG_TYPE(id->atap_config);
		sa.sa_inqbuf.removable = id->atap_config & ATAPI_CFG_REMOV ?
		    T_REMOV : T_FIXED;
		strnvisx(model, sizeof(model), id->atap_model, 40,
		    VIS_TRIM|VIS_SAFE|VIS_OCTAL);
		strnvisx(serial_number, sizeof(serial_number),
		    id->atap_serial, 20, VIS_TRIM|VIS_SAFE|VIS_OCTAL);
		strnvisx(firmware_revision, sizeof(firmware_revision),
		    id->atap_revision, 8, VIS_TRIM|VIS_SAFE|VIS_OCTAL);
		sa.sa_inqbuf.vendor = model;
		sa.sa_inqbuf.product = serial_number;
		sa.sa_inqbuf.revision = firmware_revision;

		/*
		 * Determine the operating mode capabilities of the device.
		 */
		if ((id->atap_config & ATAPI_CFG_CMD_MASK)
		    == ATAPI_CFG_CMD_16) {
			periph->periph_cap |= PERIPH_CAP_CMD16;

			/* configure port for packet length */
			PRWRITE(siic, PRX(chp->ch_channel, PRO_PCS),
			    PR_PC_PACKET_LENGTH);
		} else {
			PRWRITE(siic, PRX(chp->ch_channel, PRO_PCC),
			    PR_PC_PACKET_LENGTH);
		}

		/* XXX This is gross. */
		periph->periph_cap |= (id->atap_config & ATAPI_CFG_DRQ_MASK);

		drvp->drv_softc = atapi_probe_device(sc, target, periph, &sa);

		if (drvp->drv_softc)
			ata_probe_caps(drvp);
		else {
			s = splbio();
			drvp->drive_type &= ATA_DRIVET_NONE;
			splx(s);
		}
	} else {
		s = splbio();
		drvp->drive_type &= ATA_DRIVET_NONE;
		splx(s);
	}
}

static const struct ata_xfer_ops siisata_atapi_xfer_ops = {
	.c_start = siisata_atapi_start,
	.c_intr = siisata_atapi_complete,
	.c_poll = siisata_atapi_poll,
	.c_abort = siisata_atapi_abort,
	.c_kill_xfer = siisata_atapi_kill_xfer,
};

void
siisata_atapi_scsipi_request(struct scsipi_channel *chan,
    scsipi_adapter_req_t req, void *arg)
{
	struct scsipi_adapter *adapt = chan->chan_adapter;
	struct scsipi_periph *periph;
	struct scsipi_xfer *sc_xfer;
	struct siisata_softc *sc = device_private(adapt->adapt_dev);
	struct atac_softc *atac = &sc->sc_atac;
	struct ata_xfer *xfer;
	int channel = chan->chan_channel;
	int drive, s;

	switch (req) {
	case ADAPTER_REQ_RUN_XFER:
		sc_xfer = arg;
		periph = sc_xfer->xs_periph;
		drive = periph->periph_target;

		SIISATA_DEBUG_PRINT(("%s: %s:%d:%d\n", __func__,
		    device_xname(atac->atac_dev), channel, drive),
		    DEBUG_XFERS);

		if (!device_is_active(atac->atac_dev)) {
			sc_xfer->error = XS_DRIVER_STUFFUP;
			scsipi_done(sc_xfer);
			return;
		}
		xfer = ata_get_xfer(atac->atac_channels[channel], false);
		if (xfer == NULL) {
			sc_xfer->error = XS_RESOURCE_SHORTAGE;
			scsipi_done(sc_xfer);
			return;
		}

		if (sc_xfer->xs_control & XS_CTL_POLL)
			xfer->c_flags |= C_POLL;
		xfer->c_drive = drive;
		xfer->c_flags |= C_ATAPI;
		xfer->c_databuf = sc_xfer->data;
		xfer->c_bcount = sc_xfer->datalen;
		xfer->ops = &siisata_atapi_xfer_ops;
		xfer->c_scsipi = sc_xfer;
		xfer->c_atapi.c_dscpoll = 0;
		s = splbio();
		ata_exec_xfer(atac->atac_channels[channel], xfer);
#ifdef DIAGNOSTIC
		if ((sc_xfer->xs_control & XS_CTL_POLL) != 0 &&
		    (sc_xfer->xs_status & XS_STS_DONE) == 0)
			panic("%s: polled command not done", __func__);
#endif
		splx(s);
		return;

	default:
		/* Not supported, nothing to do. */
		;
	}
}

int
siisata_atapi_start(struct ata_channel *chp, struct ata_xfer *xfer)
{
	struct siisata_channel *schp = (struct siisata_channel *)chp;
	struct siisata_prb *prbp;

	struct scsipi_xfer *sc_xfer = xfer->c_scsipi;

	SIISATA_DEBUG_PRINT( ("%s: %s:%d:%d, scsi flags 0x%x\n", __func__,
	    SIISATANAME((struct siisata_softc *)chp->ch_atac), chp->ch_channel,
	    chp->ch_drive[xfer->c_drive].drive, sc_xfer->xs_control),
	    DEBUG_XFERS);

	ata_channel_lock_owned(chp);

	prbp = schp->sch_prb[xfer->c_slot];
	memset(prbp, 0, SIISATA_CMD_SIZE);

	/* fill in direction for ATAPI command */
	if ((sc_xfer->xs_control & XS_CTL_DATA_IN))
		prbp->prb_control |= htole16(PRB_CF_PACKET_READ);
	if ((sc_xfer->xs_control & XS_CTL_DATA_OUT))
		prbp->prb_control |= htole16(PRB_CF_PACKET_WRITE);

	satafis_rhd_construct_atapi(xfer, prbp->prb_fis);
	KASSERT(xfer->c_drive <= PMP_PORT_CTL);
	prbp->prb_fis[rhd_c] |= xfer->c_drive;

	/* copy over ATAPI command */
	memcpy(prbp->prb_atapi, sc_xfer->cmd, sc_xfer->cmdlen);

	if (siisata_dma_setup(chp, xfer->c_slot,
		(sc_xfer->xs_control & (XS_CTL_DATA_IN | XS_CTL_DATA_OUT)) ?
		xfer->c_databuf : NULL,
		xfer->c_bcount,
		(sc_xfer->xs_control & XS_CTL_DATA_IN) ?
		BUS_DMA_READ : BUS_DMA_WRITE)
	) {
		sc_xfer->error = XS_DRIVER_STUFFUP;
		return ATASTART_ABORT;
	}

	if (xfer->c_flags & C_POLL) {
		/* polled command, disable interrupts */
		prbp->prb_control |= htole16(PRB_CF_INTERRUPT_MASK);
		siisata_disable_port_interrupt(chp);
	}

	siisata_activate_prb(schp, xfer->c_slot);

	if ((xfer->c_flags & C_POLL) == 0) {
		callout_reset(&chp->c_timo_callout, mstohz(sc_xfer->timeout),
		    ata_timeout, chp);
		return ATASTART_STARTED;
	} else
		return ATASTART_POLL;
}

void
siisata_atapi_poll(struct ata_channel *chp, struct ata_xfer *xfer)
{
	struct siisata_channel *schp = (struct siisata_channel *)chp;

	/*
	 * polled command
	 */
	for (int i = 0; i < ATA_DELAY * 10; i++) {
		if (xfer->c_scsipi->xs_status & XS_STS_DONE)
			break;
		siisata_intr_port(schp);
		DELAY(100);
	}
	if ((xfer->c_scsipi->xs_status & XS_STS_DONE) == 0) {
		ata_timeout(xfer);
	}
	/* reenable interrupts */
	siisata_enable_port_interrupt(chp);

	SIISATA_DEBUG_PRINT(("%s: %s: done\n",
	    SIISATANAME((struct siisata_softc *)chp->ch_atac), __func__),
            DEBUG_FUNCS);
}

void
siisata_atapi_abort(struct ata_channel *chp, struct ata_xfer *xfer)
{
	siisata_atapi_complete(chp, xfer, 0);
}

int
siisata_atapi_complete(struct ata_channel *chp, struct ata_xfer *xfer,
    int tfd)
{
	struct siisata_softc *sc = (struct siisata_softc *)chp->ch_atac;
	struct siisata_channel *schp = (struct siisata_channel *)chp;
	struct scsipi_xfer *sc_xfer = xfer->c_scsipi;

	SIISATA_DEBUG_PRINT(("%s: %s()\n", SIISATANAME(sc), __func__),
	    DEBUG_INTR);

	if (ata_waitdrain_xfer_check(chp, xfer))
		return 0;

	if (xfer->c_flags & C_TIMEOU) {
		sc_xfer->error = XS_TIMEOUT;
	}

	bus_dmamap_sync(sc->sc_dmat, schp->sch_datad[xfer->c_slot], 0,
	    schp->sch_datad[xfer->c_slot]->dm_mapsize,
	    (sc_xfer->xs_control & XS_CTL_DATA_IN) ?
	    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->sc_dmat, schp->sch_datad[xfer->c_slot]);

	sc_xfer->resid = sc_xfer->datalen;
	sc_xfer->resid -= PRREAD(sc, PRSX(chp->ch_channel, xfer->c_slot,
	    PRSO_RTC));
	SIISATA_DEBUG_PRINT(("%s: %s datalen %d resid %d\n", SIISATANAME(sc),
	    __func__, sc_xfer->datalen, sc_xfer->resid), DEBUG_XFERS);
	if ((ATACH_ST(tfd) & WDCS_ERR) &&
	    ((sc_xfer->xs_control & XS_CTL_REQSENSE) == 0 ||
	    sc_xfer->resid == sc_xfer->datalen)) {
		sc_xfer->error = XS_SHORTSENSE;
		sc_xfer->sense.atapi_sense = ATACH_ERR(tfd);
		if ((sc_xfer->xs_periph->periph_quirks &
		    PQUIRK_NOSENSE) == 0) {
			/* request sense */
			sc_xfer->error = XS_BUSY;
			sc_xfer->status = SCSI_CHECK;
		}
	}

	siisata_deactivate_prb(schp, xfer->c_slot);
	ata_deactivate_xfer(chp, xfer);

	ata_free_xfer(chp, xfer);
	scsipi_done(sc_xfer);
	if ((ATACH_ST(tfd) & WDCS_ERR) == 0)
		atastart(chp);
	return 0;
}

#endif /* NATAPIBUS */
