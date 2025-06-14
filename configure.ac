AC_MSG_NOTICE([AnimX is licensed under the GNU General Public License v2 or later.])
AC_MSG_NOTICE([It uses FFmpeg libraries, licensed under GPL v2+.])

AC_INIT([AnimX], [1.0], [zdhdev@yahoo.com], [AnimX], [https://github.com/malloc-nbytes/AnimX])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_PROG_CC
AC_CONFIG_HEADERS([include/config.h])

# Check for pkg-config
AC_CHECK_PROG([PKG_CONFIG], [pkg-config], [yes], [no])
AS_IF([test "x$PKG_CONFIG" = "xno"], [AC_MSG_ERROR([pkg-config is required])])

# Check for required programs
AC_CHECK_PROGS([FFMPEG], [ffmpeg], [no])
AS_IF([test "x$FFMPEG" = "xno"], [AC_MSG_ERROR([ffmpeg is required])])

# Check for required libraries using pkg-config
PKG_CHECK_MODULES([DEPS], [
  libavcodec
  libavformat
  libavutil
  libswscale
  imlib2
  x11
  xrandr
], [], [AC_MSG_ERROR([Required libraries not found])])

# Add include directory and optimization flags
AC_SUBST([AM_CFLAGS], ["-I${srcdir}/include -O3"])
AC_SUBST([AM_LDFLAGS], ["$DEPS_LIBS"])

# Enable debug mode if --enable-debug is passed
AC_ARG_ENABLE([debug],
  AS_HELP_STRING([--enable-debug], [Enable debug build (default=no)]),
  [case "${enableval}" in
     yes) AM_CFLAGS="$AM_CFLAGS -ggdb -O0" ;;
     no)  ;;
     *) AC_MSG_ERROR([bad value ${enableval} for --enable-debug]) ;;
   esac])

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
