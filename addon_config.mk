# All variables and this file are optional, if they are not present the PG and the
# makefiles will try to parse the correct values from the file system.
#
# Variables that specify exclusions can use % as a wildcard to specify that anything in
# that position will match. A partial path can also be specified to, for example, exclude
# a whole folder from the parsed paths from the file system.
#
# Variables can be specified using = or +=
# = will clear the contents of that variable both specified from the file or the ones parsed
# from the file system.
# += will add the values to the previous ones in the file or the ones parsed from the file
# system.
#
# ggml is fetched and built into libs/ggml/include and libs/ggml/lib
# via ./scripts/build-ggml.sh.  GPU backends (CUDA, Vulkan, Metal) are
# auto-detected by default (use --cpu-only to disable, or --cuda/--vulkan/--metal
# to force one).
meta:
	ADDON_NAME = ofxGgml
	ADDON_DESCRIPTION = openFrameworks addon wrapping the ggml tensor library for machine-learning computation
	ADDON_AUTHOR = Jonathan Frank
	ADDON_TAGS = "ml,tensor,ggml,machine-learning,neural-network,compute"
	ADDON_URL = https://github.com/Jonathhhan/ofxGgml
common:
	ADDON_INCLUDES += src
	ADDON_INCLUDES += libs/ggml/include
	# Exclude bundled ggml source from the oF build - it is compiled
	# separately via CMake (scripts/build-ggml.sh).
	ADDON_SOURCES_EXCLUDE += build/%
	ADDON_SOURCES_EXCLUDE += libs/ggml/build/%
	ADDON_SOURCES_EXCLUDE += libs/ggml/.download/%
	ADDON_SOURCES_EXCLUDE += libs/acestep/%
	ADDON_SOURCES_EXCLUDE += libs/mojo/%
	ADDON_SOURCES_EXCLUDE += libs/llama/bin/%
	ADDON_SOURCES_EXCLUDE += libs/whisper/bin/%
	ADDON_INCLUDES_EXCLUDE += build/%
	ADDON_INCLUDES_EXCLUDE += libs/ggml/.download/%
	ADDON_INCLUDES_EXCLUDE += libs/acestep/%
	ADDON_INCLUDES_EXCLUDE += libs/mojo/%
	ADDON_INCLUDES_EXCLUDE += libs/llama/bin/%
	ADDON_INCLUDES_EXCLUDE += libs/whisper/bin/%
	ADDON_LIBS_EXCLUDE += libs/acestep/%
	ADDON_LIBS_EXCLUDE += libs/mojo/%
	ADDON_LIBS_EXCLUDE += libs/llama/bin/%
	ADDON_LIBS_EXCLUDE += libs/whisper/bin/%
linux64:
	# @DIFFUSION_LIBS_START linux64
	ADDON_LIBS += libs/ggml/lib/libggml.a
	ADDON_LIBS += libs/ggml/lib/libggml-base.a
	ADDON_LIBS += libs/ggml/lib/libggml-cpu.a
	# @DIFFUSION_LIBS_END linux64
	ADDON_LDFLAGS += -lpthread -ldl
linux:
linuxarmv6l:
linuxarmv7l:
msys2:
	# @DIFFUSION_LIBS_START msys2
	ADDON_LIBS += libs/ggml/lib/libggml.a
	ADDON_LIBS += libs/ggml/lib/libggml-base.a
	ADDON_LIBS += libs/ggml/lib/libggml-cpu.a
	# @DIFFUSION_LIBS_END msys2
	ADDON_LDFLAGS += -lpthread
vs:
	ADDON_INCLUDES += src
	ADDON_INCLUDES += libs/ggml/include
	# @DIFFUSION_LIBS_START vs
	ADDON_LIBS += libs/ggml/lib/ggml.lib
	ADDON_LIBS += libs/ggml/lib/ggml-base.lib
	ADDON_LIBS += libs/ggml/lib/ggml-cpu.lib
	ADDON_LIBS += libs/ggml/lib/ggml-cuda.lib
	ADDON_LIBS += libs/ggml/lib/ggml-vulkan.lib
	ADDON_LIBS += "$(CUDA_PATH)\lib\x64\cublas.lib"
	ADDON_LIBS += "$(CUDA_PATH)\lib\x64\cudart.lib"
	ADDON_LIBS += "$(CUDA_PATH)\lib\x64\cuda.lib"
	ADDON_LIBS += "$(VULKAN_SDK)\Lib\vulkan-1.lib"
	# @DIFFUSION_LIBS_END vs
android/armeabi:
android/armeabi-v7a:
osx:
	# @DIFFUSION_LIBS_START osx
	ADDON_LIBS += libs/ggml/lib/libggml.a
	ADDON_LIBS += libs/ggml/lib/libggml-base.a
	ADDON_LIBS += libs/ggml/lib/libggml-cpu.a
	# @DIFFUSION_LIBS_END osx
	ADDON_FRAMEWORKS += Accelerate
ios:
emscripten:

