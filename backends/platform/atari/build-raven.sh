#!/bin/bash -eux
# -e: Exit immediately if a command exits with a non-zero status.
# -u: Treat unset variables as an error when substituting.
# -x: Display expanded script commands

mkdir -p build-raven
cd build-raven

PLATFORM=m68k-atari-mintelf
FASTCALL=false
PLUGINS=false
export ASFLAGS="-m68060"
export CXXFLAGS="-m68060 -DATARI_RAVEN -DDISABLE_FANCY_THEMES -DDISABLE_DOSBOX_OPL -DDISABLE_MAME_OPL"
export LDFLAGS="-m68060"

#-DUSE_MOVE16 

export PKG_CONFIG_LIBDIR="$(${PLATFORM}-gcc -print-sysroot)/usr/lib/m68020-60/pkgconfig"

if $FASTCALL
then
	ASFLAGS="$ASFLAGS -mfastcall"
	CXXFLAGS="$CXXFLAGS -mfastcall"
	LDFLAGS="$LDFLAGS -mfastcall"
fi

if $PLUGINS
then
	PLUGINS_FLAGS="--enable-plugins --default-dynamic --enable-detection-dynamic"
else
	PLUGINS_FLAGS=""
fi

if [ ! -f config.log ]
then
../configure \
	--backend=atari \
	--host=${PLATFORM} \
	--enable-release \
	--disable-png \
	--disable-enet \
	--disable-mt32emu \
	--disable-lua \
	--disable-nuked-opl \
	--disable-16bit \
	--disable-scalers \
	--disable-translation \
	--disable-eventrecorder \
	--disable-tts \
	--disable-highres \
	--disable-bink \
	--enable-verbose-build \
	--disable-all-engines \
	--enable-engine=scumm \
	--enable-engine=sci \
	--enable-engine=dgds \
	${PLUGINS_FLAGS}
fi

#	--disable-engine=hugo,director,cine,ultima \


make -j$(getconf _NPROCESSORS_CONF) ravendist
