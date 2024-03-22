LIST=CPU
LIBS+=egl
LIBS+=glesv2
LIBS+=gf
LIBS+=m
ifndef QRECURSE
QRECURSE=recurse.mk
ifdef QCONFIG
QRDIR=$(dir $(QCONFIG))
endif
endif
include $(QRDIR)$(QRECURSE)
