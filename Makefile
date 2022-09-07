rootPath = .
include ${rootPath}/include.mk

libSources = impl/*.c
libHeaders = inc/*.h
libTests = tests/*.c

stTafDependencies =  ${LIBDEPENDS}
stTafLibs = ${commonTafLibs} ${LDLIBS}

all: all_libs all_progs
all_libs: dependencies ${LIBDIR}/stTaf.a

all_progs: all_libs ${BINDIR}/stTafTests ${BINDIR}/maf_to_taf ${BINDIR}/taf_to_maf ${BINDIR}/maf_norm

dependencies:
	mkdir -p ${LIBDIR} ${BINDIR}
	cd submodules/sonLib && PKG_CONFIG_PATH=${CWD}/lib/pkgconfig:${PKG_CONFIG_PATH} ${MAKE}
	mkdir -p ${BINDIR} ${LIBDIR} ${INCLDIR}
	rm -rf submodules/sonLib/bin/*.dSYM
	ln -f submodules/sonLib/bin/[a-zA-Z]* ${BINDIR}
	ln -f submodules/sonLib/lib/*.a ${LIBDIR}

${LIBDIR}/stTaf.a : ${libSources} ${libHeaders}  ${stTafDependencies}
	${CC} ${CPPFLAGS} ${CFLAGS} ${LDFLAGS} -c ${libSources}
	${AR} rc stTaf.a *.o
	mv stTaf.a ${LIBDIR}/

${BINDIR}/stTafTests : ${libTests} ${LIBDIR}/stTaf.a ${stTafDependencies}
	${CC} ${CPPFLAGS} ${CFLAGS} ${LDFLAGS} -o ${BINDIR}/stTafTests ${libTests} ${libSources} ${LIBDIR}/stTaf.a ${stTafLibs} ${LDLIBS}

${BINDIR}/maf_to_taf : maf_to_taf.c ${LIBDIR}/stTaf.a ${stTafDependencies}
	${CC} ${CPPFLAGS} ${CFLAGS} -o ${BINDIR}/maf_to_taf maf_to_taf.c ${libSources} ${LIBDIR}/stTaf.a ${stTafLibs} ${LDLIBS}

${BINDIR}/taf_to_maf : taf_to_maf.c ${LIBDIR}/stTaf.a ${stTafDependencies}
	${CC} ${CPPFLAGS} ${CFLAGS} -o ${BINDIR}/taf_to_maf taf_to_maf.c ${libSources} ${LIBDIR}/stTaf.a ${stTafLibs} ${LDLIBS}

${BINDIR}/maf_norm : maf_norm.c ${LIBDIR}/stTaf.a ${stTafDependencies}
	${CC} ${CPPFLAGS} ${CFLAGS} -o ${BINDIR}/maf_norm maf_norm.c ${libSources} ${LIBDIR}/stTaf.a ${stTafLibs} ${LDLIBS}


clean :
	cd submodules/sonLib && ${MAKE} clean
	rm -rf *.o ${LIBDIR} ${BINDIR}
