AC_PREREQ([2.61])
AC_INIT([thetime], [0.2], [http://reaktor42.de])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])

AC_LANG([C])
AC_PROG_CC_C99

AC_CONFIG_SRCDIR([src/thetime.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile src/Makefile])

AC_HEADER_STDC
AC_CHECK_HEADERS([locale.h string.h sys/select.h time.h])

PKG_CHECK_MODULES([X11], [x11])
AC_SUBST(X11_CFLAGS)
AC_SUBST(X11_LIBS)

PKG_CHECK_MODULES([XFT], [xft])
AC_SUBST(XFT_CFLAGS)
AC_SUBST(XFT_LIBS)

AM_CFLAGS="-Wall"
AM_LDFLAGS="-Wl,--as-needed"
AC_SUBST(AM_CFLAGS)
AC_SUBST(AM_LDFLAGS)

AC_OUTPUT
