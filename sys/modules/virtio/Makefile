#	$NetBSD: Makefile,v 1.2 2018/06/06 16:11:36 jakllsch Exp $

.include "../Makefile.inc"

.PATH:	${S}/dev/pci

KMOD=	virtio
IOCONF= virtio.ioconf

CFLAGS+= -Wno-error=sign-compare

SRCS+=	virtio.c virtio_module.c

CPPFLAGS+=	-DDIAGNOSTIC

.include <bsd.kmodule.mk>
