#	$NetBSD: Makefile,v 1.2 2018/02/21 08:36:36 pgoyette Exp $

.include "../Makefile.inc"

.PATH:	${S}/dev

KMOD=	ld
IOCONF= ld.ioconf

SRCS+=	ld.c ld_module.c

.include <bsd.kmodule.mk>
