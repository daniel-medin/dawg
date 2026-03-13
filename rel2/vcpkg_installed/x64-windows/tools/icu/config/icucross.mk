CROSS_ICU_VERSION=78.2
TOOLEXEEXT=.exe
TOOLBINDIR=$(cross_buildroot)/bin
TOOLLIBDIR=$(cross_buildroot)/lib
INVOKE=PATH=$(TOOLLIBDIR):$(cross_buildroot)/stubdata:$(cross_buildroot)/tools/ctestfw:$$PATH
PKGDATA_INVOKE=PATH=$(cross_buildroot)/stubdata:$(cross_buildroot)/tools/ctestfw:$(TOOLLIBDIR):$$PATH 

