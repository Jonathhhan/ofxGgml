# All variables and this file are optional, if they are not present the PG and the
# makefiles will try to parse the correct values from the file system.
#
# Variables can be specified using = or +=
# = will clear the contents of that variable both specified from the file or the ones parsed
# from the file system.
# += will add the values to the previous ones in the file or the ones parsed from the file
# system.
#
# ggml is bundled in libs/ggml/ and compiled via CMake as a static
# library.  Run ./scripts/build-ggml.sh before building your OF project.
# GPU backends (CUDA, Vulkan, Metal) are auto-detected during the build.

meta:
	ADDON_NAME = ofxGgml
	ADDON_DESCRIPTION = openFrameworks addon wrapping the ggml tensor library for machine-learning computation
	ADDON_AUTHOR = Jonathan Frank
	ADDON_TAGS = "ml,tensor,ggml,machine-learning,neural-network,compute"
	ADDON_URL = https://github.com/Jonathhhan/ofxGgml
	ADDON_LICENSE = MIT

common:
	ADDON_INCLUDES += libs/ggml/include
	# Exclude bundled ggml source from the oF build — it is compiled
	# separately via CMake (scripts/build-ggml.sh).
	ADDON_SOURCES_EXCLUDE += libs/ggml/src/%
	ADDON_SOURCES_EXCLUDE += libs/ggml/build/%

linux64:
	ADDON_LIBS += libs/ggml/build/src/libggml.a
	ADDON_LIBS += libs/ggml/build/src/libggml-base.a
	ADDON_LIBS += libs/ggml/build/src/libggml-cpu.a
	ADDON_LDFLAGS += -lpthread -ldl

linux:

linuxarmv6l:

linuxarmv7l:

msys2:
	ADDON_LIBS += libs/ggml/build/src/libggml.a
	ADDON_LIBS += libs/ggml/build/src/libggml-base.a
	ADDON_LIBS += libs/ggml/build/src/libggml-cpu.a
	ADDON_LDFLAGS += -lpthread

vs:
	ADDON_LIBS += libs/ggml/build/src/Release/ggml.lib
	ADDON_LIBS += libs/ggml/build/src/Release/ggml-base.lib
	ADDON_LIBS += libs/ggml/build/src/Release/ggml-cpu.lib

android/armeabi:

android/armeabi-v7a:

osx:
	ADDON_LIBS += libs/ggml/build/src/libggml.a
	ADDON_LIBS += libs/ggml/build/src/libggml-base.a
	ADDON_LIBS += libs/ggml/build/src/libggml-cpu.a
	ADDON_FRAMEWORKS += Accelerate

ios:

emscripten:
