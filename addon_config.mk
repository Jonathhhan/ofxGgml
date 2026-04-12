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
	ADDON_LIBS += libs/ggml/lib/ggml.lib
	ADDON_LIBS += libs/ggml/lib/ggml-base.lib
	ADDON_LIBS += libs/ggml/lib/ggml-cpu.lib

android/armeabi:

android/armeabi-v7a:

osx:

ios:

emscripten:
