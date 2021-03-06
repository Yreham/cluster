include ../../make/defines.mk

TARGET = dlm_controld
SBINDIRT = dlm_controld
ifdef enable_pacemaker
TARGET += dlm_controld.pcmk
SBINDIRT += dlm_controld.pcmk
endif

all: depends ${TARGET}

include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

SHAREDOBJS=	action.o \
		cpg.o \
		crc.o \
		deadlock.o \
		main.o \
		netlink.o \
		plock.o \
		group.o

PCMK_OBJS=	pacemaker-pcmk.o \
		$(SHAREDOBJS:.o=-pcmk.o)

OBJS=		$(SHAREDOBJS) \
		config.o \
		member_cman.o \
		logging.o

CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${logtincdir} 
CFLAGS += -I${dlmincdir} -I${dlmcontrolincdir}
CFLAGS += -I${corosyncincdir} -I${openaisincdir}
CFLAGS += -I${fencedincdir}
CFLAGS += -I${KERNEL_SRC}/include/
CFLAGS += -I$(S)/../lib/ -I$(S)/../include/
CFLAGS += -I${incdir}

PCMK_CFLAGS += -DENABLE_PACEMAKER=1
PCMK_CFLAGS += -I${incdir}/heartbeat -I${incdir}/pacemaker
PCMK_CFLAGS += `pkg-config glib-2.0 --cflags`
PCMK_CFLAGS += `xml2-config --cflags`

LDFLAGS += -lpthread
LDFLAGS += -L${dlmlibdir} -ldlm 
LDFLAGS += -L${logtlibdir} -llogthread
LDFLAGS += -L${openaislibdir} -lSaCkpt
LDFLAGS += -L${corosynclibdir} -lcpg -lconfdb
LDFLAGS += -L../lib -lgroup
LDFLAGS += -L${libdir}

LDDEPS += ../lib/libgroup.a

PCMK_LDFLAGS += -lcib -lcrmcommon -lcrmcluster -ltotem_pg
PCMK_LDFLAGS += `pkg-config glib-2.0 --libs`
PCMK_LDFLAGS += `xml2-config --libs`

CMAN_LDFLAGS += -L${ccslibdir} -L${cmanlibdir} -lccs -lcman
CMAN_LDFLAGS += -L${fencedlibdir} -lfenced

dlm_controld: ${OBJS} ${LDDEPS}
	$(CC) -o $@ $^ $(LDFLAGS) $(CMAN_LDFLAGS)

dlm_controld.pcmk: ${PCMK_OBJS} ${LDDEPS}
	$(CC) -o $@ $^ $(LDFLAGS) $(PCMK_LDFLAGS)

depends:
	$(MAKE) -C ../lib all

clean: generalclean

-include $(OBJS:.o=.d)
-include $(PCMK_OBJS:.o=.d)
