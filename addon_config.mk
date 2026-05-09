meta:
	ADDON_NAME = ofxGgml
	ADDON_DESCRIPTION = Planned openFrameworks addon for ggml runtime, tensors, graphs, and local inference layers
	ADDON_AUTHOR = Jonathan Frank
	ADDON_TAGS = "ggml,ai,tensor,compute,inference"
	ADDON_URL = https://github.com/Jonathhhan/ofxGgml

common:
	ADDON_INCLUDES += src
	ADDON_INCLUDES += libs/ggml/include
	ADDON_INCLUDES += libs/sam3.cpp
	ADDON_INCLUDES += libs/sam3.cpp/stb
	ADDON_CFLAGS += -DGGML_MAX_NAME=128
	ADDON_SOURCES_EXCLUDE += build/%
	ADDON_SOURCES_EXCLUDE += libs/ggml/.source/%
	ADDON_SOURCES_EXCLUDE += libs/ggml/build/%
	ADDON_SOURCES_EXCLUDE += libs/ggml/build*/%
	ADDON_SOURCES_EXCLUDE += libs/llama.cpp/.source/%
	ADDON_SOURCES_EXCLUDE += libs/llama.cpp/build/%
	ADDON_SOURCES_EXCLUDE += libs/llama.cpp/build*/%
	ADDON_SOURCES_EXCLUDE += libs/sam3.cpp/.git/%
	ADDON_SOURCES_EXCLUDE += libs/sam3.cpp/sam3.cpp
	ADDON_SOURCES_EXCLUDE += libs/sam3.cpp/build/%
	ADDON_SOURCES_EXCLUDE += libs/sam3.cpp/build*/%
	ADDON_SOURCES_EXCLUDE += libs/sam3.cpp/ggml/%
	ADDON_SOURCES_EXCLUDE += libs/sam3.cpp/examples/%
	ADDON_SOURCES_EXCLUDE += libs/sam3.cpp/media/%
	ADDON_SOURCES_EXCLUDE += libs/sam3.cpp/scripts/%
	ADDON_SOURCES_EXCLUDE += libs/sam3.cpp/tests/%
	ADDON_INCLUDES_EXCLUDE += build/%
	ADDON_INCLUDES_EXCLUDE += libs/ggml/.source/%
	ADDON_INCLUDES_EXCLUDE += libs/ggml/build/%
	ADDON_INCLUDES_EXCLUDE += libs/ggml/build*/%
	ADDON_INCLUDES_EXCLUDE += libs/llama.cpp/.source/%
	ADDON_INCLUDES_EXCLUDE += libs/llama.cpp/build/%
	ADDON_INCLUDES_EXCLUDE += libs/llama.cpp/build*/%
	ADDON_INCLUDES_EXCLUDE += libs/sam3.cpp/.git/%
	ADDON_INCLUDES_EXCLUDE += libs/sam3.cpp/build/%
	ADDON_INCLUDES_EXCLUDE += libs/sam3.cpp/build*/%
	ADDON_INCLUDES_EXCLUDE += libs/sam3.cpp/ggml/%
	ADDON_INCLUDES_EXCLUDE += libs/sam3.cpp/examples/%
	ADDON_INCLUDES_EXCLUDE += libs/sam3.cpp/media/%
	ADDON_INCLUDES_EXCLUDE += libs/sam3.cpp/scripts/%
	ADDON_INCLUDES_EXCLUDE += libs/sam3.cpp/tests/%

vs:
	# @OFXGGML_LIBS_START vs
	ADDON_CFLAGS += -DOFXGGML_WITH_CUDA
	ADDON_CFLAGS += -DOFXGGML_WITH_VULKAN
	ADDON_LIBS += libs/ggml/lib/ggml.lib
	ADDON_LIBS += libs/ggml/lib/ggml-base.lib
	ADDON_LIBS += libs/ggml/lib/ggml-cpu.lib
	ADDON_LIBS += libs/ggml/lib/ggml-cuda.lib
	ADDON_LIBS += libs/ggml/lib/ggml-vulkan.lib
	ADDON_LIBS += Advapi32.lib
	ADDON_LIBS += $(CUDA_PATH)/lib/x64/cublas.lib
	ADDON_LIBS += $(CUDA_PATH)/lib/x64/cudart.lib
	ADDON_LIBS += $(CUDA_PATH)/lib/x64/cuda.lib
	ADDON_LIBS += $(VULKAN_SDK)/Lib/vulkan-1.lib
	# @OFXGGML_LIBS_END vs
	# @OFXGGML_SAM3_LIBS_START vs
	ADDON_CFLAGS += -DOFXGGML_ENABLE_SAM3_ADAPTER
	ADDON_LIBS += libs/sam3/lib/sam3.lib
	# @OFXGGML_SAM3_LIBS_END vs

linux64:
	# @OFXGGML_LIBS_START linux64
	ADDON_LIBS += libs/ggml/lib/libggml.a
	ADDON_LIBS += libs/ggml/lib/libggml-base.a
	ADDON_LIBS += libs/ggml/lib/libggml-cpu.a
	# @OFXGGML_LIBS_END linux64
	# @OFXGGML_SAM3_LIBS_START linux64
	# @OFXGGML_SAM3_LIBS_END linux64
	ADDON_LDFLAGS += -lpthread -ldl

osx:
	# @OFXGGML_LIBS_START osx
	ADDON_LIBS += libs/ggml/lib/libggml.a
	ADDON_LIBS += libs/ggml/lib/libggml-base.a
	ADDON_LIBS += libs/ggml/lib/libggml-cpu.a
	# @OFXGGML_LIBS_END osx
	# @OFXGGML_SAM3_LIBS_START osx
	# @OFXGGML_SAM3_LIBS_END osx
	ADDON_FRAMEWORKS += Accelerate
