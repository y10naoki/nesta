#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(nxsamples, 0.1.0, y10naoki@gmail.com)
AM_INIT_AUTOMAKE($PACKAGE_NAME, $PACKAGE_VERSION)
AC_CONFIG_SRCDIR([hello.c])
AC_CONFIG_HEADER([config.h])

# $host
AC_CANONICAL_HOST

# Checks for programs.
AC_PROG_CC

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h memory.h netdb.h netinet/in.h stdlib.h string.h sys/socket.h sys/timeb.h unistd.h])

AC_ARG_WITH([nestalib],
    [  --with-nestalib=path location of the nestalib headers (default is /usr/local/include/nestalib))],
    [NESTLIB_HEADERS=${withval}],
    [NESTALIB_HEADERS=/usr/local/include/nestalib])
AC_SUBST([NESTALIB_HEADERS])

AC_ARG_WITH([oci],
    [  --with-oci=path location of the Oracle OCI headers],
    [OCI_HEADERS=${withval}],
    [OCI_HEADERS=.])
AC_SUBST([OCI_HEADERS])
if test "$OCI_HEADERS" != "."; then
    AC_DEFINE([HAVE_ORACLE_OCI], 1, [Define supports the Oracle OCI], [])
fi

case $host in
   *-linux* )
     AC_MSG_RESULT([LINUX])
     LDFLAGS="-shared"
     ;;
   *-darwin* )
     AC_MSG_RESULT([DARWIN])
     LDFLAGS="-bundle -flat_namespace -undefined suppress"
     ;;
   *)
     AC_MSG_RESULT(no)
     ;;
esac

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_STRUCT_TM

# Checks for library functions.
AC_FUNC_ALLOCA
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_MMAP
AC_FUNC_REALLOC
AC_FUNC_SELECT_ARGTYPES
AC_FUNC_STAT
AC_FUNC_STRFTIME
AC_CHECK_FUNCS([clock_gettime gethostbyaddr gethostbyname getpagesize gettimeofday inet_ntoa memmove memset munmap select socket strchr strerror strstr])
AC_CHECK_FUNCS([iconv])

# check optional
AC_CHECK_LIB([iconv], [iconv])
AC_CHECK_LIB([z], [deflate])
AC_CHECK_LIB([xml2], [xmlReadMemory])
AC_CHECK_LIB([ssl], [SSL_library_init])

AC_CHECK_LIB([nesta], [nio_initialize], 
       [AC_DEFINE([HAVE_NESTALIB], 1, [nestalib is core library])], 
       [AC_MSG_ERROR(['nestalib' is not installed.])])
AC_CHECK_LIB([nesta], [nio_initialize])

AC_CHECK_LIB([clntsh], [OCIInitialize])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
