# All variables and this file are optional, if they are not present the PG and the
# makefiles will try to parse the correct values from the file system.
#
# Variables can be specified using = or +=
# = will clear the contents of that variable both specified from the file or the ones parsed
# from the file system.
# += will add the values to the previous ones in the file or the ones parsed from the file
# system.

meta:
	ADDON_NAME = ofxGgml
	ADDON_DESCRIPTION = openFrameworks addon wrapping the ggml tensor library for machine-learning computation
	ADDON_AUTHOR = Jonathan Frank
	ADDON_TAGS = "ml,tensor,ggml,machine-learning,neural-network,compute"
	ADDON_URL = https://github.com/Jonathhhan/ofxGgml
	ADDON_LICENSE = MIT

common:
	ADDON_INCLUDES += libs/ggml/include

linux64:
	ADDON_PKG_CONFIG_LIBRARIES = ggml

linux:

linuxarmv6l:

linuxarmv7l:

msys2:
	ADDON_PKG_CONFIG_LIBRARIES = ggml

vs:
	# Link only ggml.lib — it transitively depends on ggml-base and
	# ggml-cpu.  Listing all three can cause duplicate static
	# initialisers (the GGML_ASSERT at ggml.cpp:22) when building
	# with static libraries.
	ADDON_LIBS += libs/ggml/lib/ggml.lib

android/armeabi:

android/armeabi-v7a:

osx:

ios:

emscripten:
