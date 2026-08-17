meta:
	ADDON_NAME = ofxGgml
	ADDON_DESCRIPTION = OpenAI-compatible chat and tool workflows for openFrameworks
	ADDON_AUTHOR = Jonathan Frank
	ADDON_TAGS = "ai,llm,local,chat,tools"
	ADDON_URL = https://github.com/Jonathhhan/ofxGgml

common:
	ADDON_INCLUDES = src
	ADDON_SOURCES = src/server/ofxGgmlServer.cpp
	ADDON_SOURCES += src/chat/ofxGgmlChatSession.cpp
	ADDON_SOURCES += src/documents/ofxGgmlDocumentIndex.cpp
	ADDON_SOURCES += src/tools/ofxGgmlToolRegistry.cpp
	ADDON_SOURCES += src/tools/ofxGgmlToolLoop.cpp
