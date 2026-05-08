#pragma once

#include <map>
#include <string>
#include <utility>

struct ofxGgmlHttpRequest {
	std::string method = "POST";
	std::string url;
	std::map<std::string, std::string> headers;
	std::string body;
	int timeoutMs = 60000;
};

struct ofxGgmlHttpResponse {
	int status = 0;
	std::string body;
	std::string error;

	bool ok() const {
		return status >= 200 && status < 300 && error.empty();
	}
};

class ofxGgmlHttpTransport {
public:
	virtual ~ofxGgmlHttpTransport() = default;
	virtual ofxGgmlHttpResponse send(const ofxGgmlHttpRequest & request) = 0;
};

class ofxGgmlUnavailableHttpTransport final : public ofxGgmlHttpTransport {
public:
	ofxGgmlHttpResponse send(const ofxGgmlHttpRequest &) override {
		return {0, "", "No HTTP transport has been configured."};
	}
};
