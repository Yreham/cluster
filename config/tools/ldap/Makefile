TARGET= confdb2ldif

SBINDIRT=$(TARGET)

SUBDIRS=rng2ldif

all: ${TARGET}

include ../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk
include $(OBJDIR)/make/passthrough.mk

OBJS=	confdb2ldif.o

CFLAGS += -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE
CFLAGS += -I${corosyncincdir}
CFLAGS += -I${incdir}

LDFLAGS += -L${corosynclibdir} -lconfdb
LDFLAGS += -L${libdir}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

clean: generalclean

-include $(OBJS:.o=.d)
