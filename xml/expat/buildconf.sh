#! /bin/sh

#
# Build aclocal.m4 from libtool's libtool.m4
#
libtoolize=`conftools/PrintPath glibtoolize libtoolize`
if [ "x$libtoolize" = "x" ]; then
    echo "libtoolize not found in path"
    exit 1
fi
ltpath=`dirname $libtoolize`
ltfile=`cd $ltpath/../share/aclocal ; pwd`/libtool.m4
echo "Incorporating $ltfile into aclocal.m4 ..."
echo "dnl THIS FILE IS AUTOMATICALLY GENERATED BY buildconf.sh" > aclocal.m4
echo "dnl edits here will be lost" >> aclocal.m4
cat $ltfile >> aclocal.m4

cross_compile_warning="warning: AC_TRY_RUN called without default to allow cross compiling"

#
# Create the libtool helper files
#
# Note: we copy (rather than link) the files.
#
# Note: This bundled version of expat will not always replace the
# files since we have a special config.guess/config.sub that we
# want to ensure is used.
echo "Copying libtool helper files ..."

# Remove any libtool files so one can switch between libtool 1.3
# and libtool 1.4 by simply rerunning the buildconf script.
(cd conftools ; rm -f ltconfig ltmain.sh)

$libtoolize --copy --automake

#
# Generate the autoconf header template (config.h.in) and ./configure
#
echo "Creating config.h.in ..."
autoheader 2>&1 | grep -v "$cross_compile_warning"

echo "Creating configure ..."
### do some work to toss config.cache?
autoconf 2>&1 | grep -v "$cross_compile_warning"

exit 0
