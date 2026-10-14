TARGET = Verity
OBJS = src/main.o src/gfx.o src/net.o src/logic.o src/player.o src/assets.o

CFLAGS = -O2 -G0 -Wall -std=gnu11
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

# curl + mbedTLS (pacotes do PSPDEV) antes das libs de rede do PSP
LIBS = -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz \
       -lpspgu -lpspge -lpspdisplay -lpspctrl -lpsputility -lpsppower \
       -lpspnet_resolver -lpspnet_apctl -lpspnet_inet -lpspnet -lm

PSP_FW_VERSION = 371
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Verity
PSP_EBOOT_ICON = ICON0.PNG

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
