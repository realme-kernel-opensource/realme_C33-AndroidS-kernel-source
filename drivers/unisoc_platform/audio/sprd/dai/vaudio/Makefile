# SPRD Vaudio Support
SPRD_SOUND_TREE := drivers/unisoc_platform/audio/sprd
KBUILD_CFLAGS   +=	-I$(SPRD_SOUND_TREE) \
					-I$(SPRD_SOUND_TREE)/dai/ \
					-I$(SPRD_SOUND_TREE)/dai/vbc/ \
					-I$(SPRD_SOUND_TREE)/platform/

snd-soc-sprd-vaudio-objs       := vaudio.o
obj-$(CONFIG_SND_SOC_UNISOC_VAUDIO)       += snd-soc-sprd-vaudio.o
