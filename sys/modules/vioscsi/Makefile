#	$NetBSD: Makefile,v 1.1 2017/03/25 18:15:31 jdolecek Exp $

.include "../Makefile.inc"

.PATH:	${S}/dev/pci

KMOD=	vioscsi
IOCONF=	vioscsi.ioconf

CFLAGS+= -Wno-error=sign-compare

SRCS+=	vioscsi.c vioscsi_module.c

DBG+=	-DDEBUG -DDIAGNOSTIC -DLOCKDEBUG

.include <bsd.kmodule.mk>
