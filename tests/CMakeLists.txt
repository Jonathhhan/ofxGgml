cmake_minimum_required(VERSION 3.18)
project(ofxGgml_v2_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ofxGgml_v2_tests
	test_main.cpp
	test_server.cpp
	test_chat_session.cpp
	test_public_headers.cpp
	../src/server/ofxGgmlServer.cpp
	../src/chat/ofxGgmlChatSession.cpp)

target_include_directories(ofxGgml_v2_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/../src)

enable_testing()
add_test(NAME ofxGgml_v2_tests COMMAND ofxGgml_v2_tests)
