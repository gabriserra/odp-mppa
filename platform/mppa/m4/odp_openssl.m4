##########################################################################
# Enable/disable crypto support
##########################################################################
crypto=no
AC_ARG_ENABLE([crypto],
    [  --disable-crypto        disable crypto support],
    [if test x$enableval = xyes; then
        crypto=yes
    fi])

##########################################################################
# Set optional OpenSSL path
##########################################################################
AC_ARG_WITH([openssl-path],
AC_HELP_STRING([--with-openssl-path=DIR path to openssl libs and headers],
               [(or in the default path if not specified).]),
    [OPENSSL_PATH=$withval
    AM_CPPFLAGS="$AM_CPPFLAGS -I$OPENSSL_PATH/include"
    AM_LDFLAGS="$AM_LDFLAGS -L$OPENSSL_PATH/lib"
    ],[])

##########################################################################
# Save and set temporary compilation flags
##########################################################################
OLD_LDFLAGS=$LDFLAGS
OLD_CPPFLAGS=$CPPFLAGS
LDFLAGS="$AM_LDFLAGS $LDFLAGS"
CPPFLAGS="$AM_CPPFLAGS $CPPFLAGS"

##########################################################################
# Check for OpenSSL availability
##########################################################################
if test x$crypto = xyes
then
    AC_CHECK_LIB([crypto], [EVP_EncryptInit], [],
        [crypto=no])
    AC_CHECK_HEADERS([openssl/des.h openssl/rand.h openssl/hmac.h openssl/evp.h], [],
        [crypto=no])
else
    crypto=no
fi

AM_CONDITIONAL([crypto], [test x$crypto = xyes])

if test x$crypto = xno
then
    AM_CPPFLAGS="$AM_CPPFLAGS -DNO_CRYPTO"
    AM_CFLAGS="$AM_CFLAGS -DNO_CRYPTO"
fi

##########################################################################
# Restore old saved variables
##########################################################################
LDFLAGS=$OLD_LDFLAGS
CPPFLAGS=$OLD_CPPFLAGS
