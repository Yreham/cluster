TARGET= fsck.gfs

FORCESBINT= $(TARGET)

all: ${TARGET}

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

OBJS=	bio.o \
	bitmap.o \
	block_list.o \
	eattr.o \
	file.o \
	fs_bits.o \
	fs_bmap.o \
	fs_dir.o \
	fs_inode.o \
	fs_recovery.o \
	hash.o \
	initialize.o \
	inode.o \
	inode_hash.o \
	link.o \
	log.o \
	lost_n_found.o \
	main.o \
	metawalk.o \
	ondisk.o \
	pass1.o \
	pass1b.o \
	pass1c.o \
	pass2.o \
	pass3.o \
	pass4.o \
	pass5.o \
	rgrp.o \
	super.o \
	util.o

CFLAGS += -D_FILE_OFFSET_BITS=64 -DHELPER_PROGRAM
CFLAGS += -I$(S)/../include
CFLAGS += -I${incdir}

LDFLAGS += -L${libdir}

${TARGET}: $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean: generalclean

${TARGET}.pot: $(OBJS:.o=.c)
	xgettext -C -F --keyword=print_log --keyword=log_debug --keyword=log_info --keyword=_ \
	 --keyword=log_notice --keyword=log_warn --keyword=log_err --keyword=log_crit \
	 --keyword=log_debug --keyword=log_err --keyword=log_print -d - $(OBJS:.o=.c) > ${TARGET}.pot

-include $(OBJS:.o=.d)
