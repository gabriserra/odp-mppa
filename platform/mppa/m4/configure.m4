k1_core=k1a
k1_hw=na
OS=linux
RUN_TARGET=NA

AS_CASE([$host],
  [k1a-kalray-*], [ARCH=k1a
				   k1_core=k1a
				   EXEEXT=.kelf
				  ],
  [k1b-kalray-*], [ARCH=k1b
				   k1_core=k1b
				   EXEEXT=.kelf
				  ],
  [*]
)
AS_CASE([$host],
  [k1*-nodeos*], [OS=nodeos],
  [k1*-mos*],    [OS=mos],
  [k1*-rtems*],  [OS=rtems],
  [*],           [OS=linux]
)
AS_CASE([$host],
  [k1*simu], [k1_hw=no
			  RUN_TARGET=k1-cluster],
  [k1*],     [k1_hw=yes
  			  RUN_TARGET=k1-jtag],
  [*]
)

AM_CFLAGS   = "-Wno-cast-qual $AM_CFLAGS"
AM_CPPFLAGS = "$AM_CPPFLAGS $AM_CFLAGS"

AC_SUBST([RUN_TARGET])

AC_CONFIG_FILES([platform/mppa/Makefile
				 platform/mppa/test/Makefile
				 platform/mppa/test/pktio/Makefile])

AM_CFLAGS="$AM_CFLAGS -ffunction-sections -fdata-sections"
AM_LDFLAGS="$AM_LDFLAGS -Wl,--gc-sections"

m4_include([platform/mppa/m4/odp_openssl.m4])
