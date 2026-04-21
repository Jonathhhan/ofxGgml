#pragma once

#include "ofMain.h"
#include "ofxGgmlBasic.h"

class ofApp : public ofBaseApp {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

    void sendMessage();
    void addChatMessage(const string& role, const string& message);
    string wrapText(const string& text, int width);

private:
    // AI components
    ofxGgmlEasy ai;
    bool aiReady = false;
    string statusMessage;

    // Chat state
    struct ChatMessage {
        string role;  // "user" or "assistant"
        string text;
        float y = 0;
    };
    vector<ChatMessage> chatHistory;

    // UI state
    string userInput;
    bool isWaitingForResponse = false;
    float scrollOffset = 0;

    // Visual settings
    ofTrueTypeFont font;
    float lineHeight = 20;
    float padding = 20;
    float inputHeight = 60;
    int wrapWidth = 600;
};
