# $NetBSD: Makefile,v 1.4 2020/01/25 11:24:20 jmcneill Exp $

PROG=		bootarm.efi
OBJFMT=		binary
NEWVERSWHAT=	"efiboot (arm)"
GNUEFIARCH=	arm
LIBGNUEFI_ARCH=	arm

EXTRA_SOURCES=	efibootarm.c
EXTRA_SOURCES+=	cache.S

COPTS+=		-mfloat-abi=soft -mno-unaligned-access -ffreestanding -fno-unwind-tables
CFLAGS+=	-DEFIBOOT_ALIGN=0x1000000
LDFLAGS+=	-N

.include "${.CURDIR}/../Makefile.efiboot"

release: check_RELEASEDIR
	${HOST_INSTALL_FILE} -m ${BINMODE} ${PROG} \
		${RELEASEDIR}/${RELEASEMACHINEDIR}/installation/misc
