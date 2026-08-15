meta:
	ADDON_NAME = ofxGgml
	ADDON_DESCRIPTION = Local model-server chat and tool workflows for openFrameworks
	ADDON_AUTHOR = Jonathan Frank
	ADDON_TAGS = "ai,llm,local,chat,tools"
	ADDON_URL = https://github.com/Jonathhhan/ofxGgml

common:
	ADDON_INCLUDES = src
	ADDON_SOURCES = src/server/ofxGgmlServer.cpp
	ADDON_SOURCES += src/chat/ofxGgmlChatSession.cpp
