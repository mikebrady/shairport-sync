#!/bin/sh

# These tests check that the requested configuration can be made and can be built
# In many cases it will check that the Shairport Sync configuration string
# contains or omits the relevant string
# If doesn't check for the presence or absence of products except
# when it checks for the configuration string

# To get it to work first time, and assuming you have already build Shairport Sync according to the standard
# you need the following extra libraries in Linux:
# libdaemon-dev libmbedtls-dev libsndio-dev libao-dev libpulse-dev libsndfile1-dev libavahi-compat-libdnssd-dev libglib2.0-dev libmosquitto-dev
# Also, you'll need to build the ALAC library -- see https://github.com/mikebrady/alac.

# At present, it is Linux-only.
check_configuration_string_includes()
{
	echo -n " checking configuration string includes \"$1\"..."
	./shairport-sync -V | grep -q $1
	if [ "$?" -eq "1" ] ; then
		echo "\nError: \"$1\" not included in configuration string. See \"../configure_test.log\"."
		exit 1
	fi
	echo -n "ok"
}

check_configuration_string_excludes()
{
	echo -n " checking configuration string excludes \"$1\"..."
	./shairport-sync -V | grep -q $1
	if [ "$?" -eq "0" ] ; then
		echo "\nError: \"$1\" is unexpectedly included in the configuration string. See \"../configure_test.log\"."
		exit 1
	fi
	echo -n "ok"
}
check_for_success()
{
	if [ "$2" = "x" ] ; then
		A2=""
	else
		A2="$2"
	fi
	if [ "$3" = "x" ] ; then
		A3=""
	else
		A3="$3"
	fi
	if [ "$4" = "x" ] ; then
		A4=""
	else
		A4="$4"
	fi
	if [ "$5" = "x" ] ; then
		A5=""
	else
		A5=$5
	fi
	if [ "$1" = "x" -o "$1" = "x$A2" ] ; then
		TESTCOUNT="$(expr "$TESTCOUNT" '+' '1')"
		echo -n "Checking \"$A2\": "
		echo -n "configuring..."
		echo "./configure $A3 $A2" > $LOGFILE
		./configure $A3 $A2 >> $LOGFILE 2>&1
		if [ "$?" -eq "0" ] ; then
			echo -n "ok making..."
			echo "make clean" >> $LOGFILE
			make clean >> $LOGFILE 2>&1
			echo "make -j $((`nproc`*2))" >> $LOGFILE
			make -j $((`nproc`*2)) >> $LOGFILE 2>&1
			if [ "$?" -ne "0" ] ; then
			  echo "\nError at build step with arg \"$A2\". See \"../configure_test.log\"."
			  exit 1
			fi
			echo -n "ok"
		else
			echo "\nError at configure step with arg \"$A2\". See \"../configure_test.log\"."
			exit 1
		fi
		if [ "$A4" != "" ] ; then
			check_configuration_string_includes $A4
		fi
		if [ "$A5" != "" ] ; then
			check_configuration_string_excludes $A5
		fi
		echo "."
	fi
}

check_for_configuration_fail()
{
	if [ "$1" = "x" -o "$1" = "x$2" ] ; then
		echo -n "Checking \"$2\" fails during configuration... "
		TESTCOUNT="$(expr "$TESTCOUNT" '+' '1')"
		./configure $3 $2 > $LOGFILE 2>&1
		if [ "$?" -eq "0" ] ; then
			echo "\nError: configuration did not fail with arg \"$2\". See \"../configure_test.log\"."
			exit 1
		fi
		echo " done."
	fi
	return 0
}

echo -n "Preparing..."
LOGFILE=configure_test.log
CWD=`pwd`
cd ..
autoreconf -fi > $LOGFILE 2>&1
if [ "$?" -ne "0" ] ; then
	echo " error running \"autoreconf -fi\" -- see \"../configure_test.log\"."
	exit 1
fi
echo "ok."
TESTCOUNT=0
check_for_success x$1 --with-pkg-config --with-ssl=mbedtls
check_for_success x$1 --with-ssl=openssl x OpenSSL
check_for_success x$1 --with-ssl=mbedtls x mbedTLS
# check_for_success x$1 --with-ssl=polarssl x PolarSSL
check_for_configuration_fail x$1 --with-ssl
check_for_configuration_fail x$1 --without-ssl=openssl
check_for_configuration_fail x$1 --without-ssl=mbedtls
# check_for_configuration_fail x$1 --without-ssl=polarssl
check_for_configuration_fail x$1
check_for_success x$1 --with-alsa --with-ssl=mbedtls ALSA
check_for_success x$1 --without-alsa --with-ssl=mbedtls x ALSA

check_for_success x$1 --with-dummy --with-ssl=mbedtls dummy
check_for_success x$1 --without-dummy --with-ssl=mbedtls x dummy

check_for_success x$1 --with-stdout --with-ssl=mbedtls stdout
check_for_success x$1 --without-stdout --with-ssl=mbedtls x stdout

check_for_success x$1 --with-pipe --with-ssl=mbedtls pipe
check_for_success x$1 --without-pipe --with-ssl=mbedtls x pipe

check_for_success x$1 --with-external-mdns --with-ssl=mbedtls external_mdns
check_for_success x$1 --without-external-mdns --with-ssl=mbedtls x external_mdns

check_for_success x$1 --with-apple-alac --with-ssl=mbedtls alac
check_for_success x$1 --without-apple-alac --with-ssl=mbedtls x alac

check_for_success x$1 --with-piddir=/var --with-ssl=mbedtls
check_for_success x$1 --without-piddir --with-ssl=mbedtls

check_for_success x$1 --with-libdaemon --with-ssl=mbedtls
check_for_success x$1 --without-libdaemon --with-ssl=mbedtls

check_for_success x$1 --with-soxr --with-ssl=mbedtls soxr
check_for_success x$1 --without-soxr --with-ssl=mbedtls x soxr

check_for_success x$1 --with-metadata --with-ssl=mbedtls metadata
check_for_success x$1 --without-metadata --with-ssl=mbedtls x metadata

check_for_success x$1 --with-avahi --with-ssl=mbedtls Avahi
check_for_success x$1 --without-avahi --with-ssl=mbedtls x Avahi

check_for_success x$1 --with-tinysvcmdns --with-ssl=mbedtls tinysvcmdns
check_for_success x$1 --without-tinysvcmdns --with-ssl=mbedtls x tinysvcmdns

# check_for_success x$1 --with-jack --with-ssl=mbedtls jack
# check_for_success x$1 --without-jack --with-ssl=mbedtls x jack

check_for_success x$1 --with-sndio --with-ssl=mbedtls sndio
check_for_success x$1 --without-sndio --with-ssl=mbedtls x sndio

check_for_success x$1 --with-ao --with-ssl=mbedtls ao
check_for_success x$1 --without-ao --with-ssl=mbedtls x ao

# the following is disabled because there is no soundio library for Raspberry OS
#check_for_success x$1 --with-soundio --with-ssl=mbedtls soundio
# check_for_success x$1 --without-soundio --with-ssl=mbedtls x soundio

check_for_success x$1 --with-pulseaudio --with-ssl=mbedtls PulseAudio
check_for_success x$1 --without-pulseaudio --with-ssl=mbedtls x PulseAudio

check_for_success x$1 --with-pipewire --with-ssl=mbedtls PipeWire
check_for_success x$1 --without-pipewire --with-ssl=mbedtls x PipeWire

check_for_success x$1 --with-convolution --with-ssl=mbedtls convolution
check_for_success x$1 --without-convolution --with-ssl=mbedtls x convolution

check_for_success x$1 --with-dns_sd --with-ssl=mbedtls dns_sd
check_for_success x$1 --without-dns_sd --with-ssl=mbedtls x dns_sd

check_for_success x$1 --with-dbus-interface --with-ssl=mbedtls metadata-dbus
check_for_success x$1 --without-dbus-interface --with-ssl=mbedtls x dbus

check_for_success x$1 --with-dbus-test-client --with-ssl=mbedtls
check_for_success x$1 --without-dbus-test-client --with-ssl=mbedtls

check_for_success x$1 --with-mpris-interface --with-ssl=mbedtls metadata-mpris
check_for_success x$1 --without-mpris-interface --with-ssl=mbedtls x mpris

check_for_success x$1 --with-mpris-test-client --with-ssl=mbedtls
check_for_success x$1 --without-mpris-test-client --with-ssl=mbedtls

check_for_success x$1 --with-mqtt-client --with-ssl=mbedtls metadata-mqtt
check_for_success x$1 --without-mqtt-client --with-ssl=mbedtls x mqtt

check_for_success x$1 --with-configfiles '--sysconfdir=/etc --with-alsa --with-soxr --with-avahi --with-ssl=openssl --with-systemd' OpenSSL-Avahi-ALSA-soxr-sysconfdir:/etc
check_for_success x$1 --without-configfiles '--sysconfdir=/etc --with-alsa --with-soxr --with-avahi --with-ssl=openssl --with-systemd' OpenSSL-Avahi-ALSA-soxr-sysconfdir:/etc

check_for_success x$1 --with-systemd-startup '--sysconfdir=/etc --with-alsa --with-soxr --with-avahi --with-ssl=openssl' OpenSSL-Avahi-ALSA-soxr-sysconfdir:/etc
check_for_success x$1 --without-systemd-startup '--sysconfdir=/etc --with-alsa --with-soxr --with-avahi --with-ssl=openssl' OpenSSL-Avahi-ALSA-soxr-sysconfdir:/etc
check_for_success x$1 --with-systemv '--sysconfdir=/etc --with-libdaemon --with-alsa --with-soxr --with-avahi --with-ssl=openssl' OpenSSL-Avahi-ALSA-soxr-sysconfdir:/etc
check_for_success x$1 --without-systemv '--sysconfdir=/etc --with-libdaemon --with-alsa --with-soxr --with-avahi --with-ssl=openssl' OpenSSL-Avahi-ALSA-soxr-sysconfdir:/etc

cd $CWD
echo "$TESTCOUNT tests completed."

