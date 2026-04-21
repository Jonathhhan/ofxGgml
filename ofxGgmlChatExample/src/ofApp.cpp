#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup() {
    ofSetWindowTitle("ofxGgml Chat Example");
    ofSetFrameRate(60);
    ofBackground(30);

    // Load font
    font.load("fonts/default.ttf", 12, true, true);
    if (!font.isLoaded()) {
        lineHeight = 16;
    }

    // Configure AI
    statusMessage = "Setting up AI...";

    ofxGgmlEasyTextConfig config;
    config.modelPath = ofToDataPath("models/qwen2.5-1.5b-instruct-q4_k_m.gguf");
    config.serverUrl = "http://127.0.0.1:8080";
    config.completionExecutable = "llama-completion";

    try {
        ai.configureText(config);
        aiReady = true;
        statusMessage = "AI ready! Type a message and press Enter.";
        ofLogNotice("ofApp") << "Chat example ready";
    } catch (const std::exception& e) {
        statusMessage = "Error: " + string(e.what());
        ofLogError("ofApp") << "Setup failed: " << e.what();
    }
}

//--------------------------------------------------------------
void ofApp::update() {
    // Auto-scroll to bottom when new messages arrive
    if (!chatHistory.empty()) {
        float totalHeight = 0;
        for (auto& msg : chatHistory) {
            msg.y = totalHeight;
            string wrapped = wrapText(msg.text, wrapWidth);
            int lines = std::count(wrapped.begin(), wrapped.end(), '\n') + 1;
            totalHeight += (lines + 1) * lineHeight + padding;
        }

        float chatAreaHeight = ofGetHeight() - inputHeight - 40;
        if (totalHeight > chatAreaHeight) {
            scrollOffset = -(totalHeight - chatAreaHeight);
        }
    }
}

//--------------------------------------------------------------
void ofApp::draw() {
    // Draw chat history
    ofPushMatrix();
    ofTranslate(padding, padding + scrollOffset);

    for (auto& msg : chatHistory) {
        // Role label
        ofSetColor(msg.role == "user" ? ofColor(100, 150, 255) : ofColor(100, 255, 150));
        string roleLabel = (msg.role == "user" ? "You: " : "AI: ");

        if (font.isLoaded()) {
            font.drawString(roleLabel, 0, msg.y + lineHeight);
        } else {
            ofDrawBitmapString(roleLabel, 0, msg.y + lineHeight);
        }

        // Message text
        ofSetColor(220);
        string wrapped = wrapText(msg.text, wrapWidth);

        if (font.isLoaded()) {
            font.drawString(wrapped, 60, msg.y + lineHeight);
        } else {
            ofDrawBitmapString(wrapped, 60, msg.y + lineHeight);
        }
    }

    ofPopMatrix();

    // Draw input area
    float inputY = ofGetHeight() - inputHeight;

    ofSetColor(50);
    ofDrawRectangle(0, inputY, ofGetWidth(), inputHeight);

    ofSetColor(100);
    ofDrawLine(0, inputY, ofGetWidth(), inputY);

    // Input prompt
    ofSetColor(180);
    string prompt = isWaitingForResponse ? "Thinking..." : "Message: ";

    if (font.isLoaded()) {
        font.drawString(prompt, padding, inputY + 25);
        font.drawString(userInput + "_", padding + 70, inputY + 25);
    } else {
        ofDrawBitmapString(prompt, padding, inputY + 25);
        ofDrawBitmapString(userInput + "_", padding + 70, inputY + 25);
    }

    // Status message
    ofSetColor(150);
    if (font.isLoaded()) {
        font.drawString(statusMessage, padding, inputY + 45);
    } else {
        ofDrawBitmapString(statusMessage, padding, inputY + 45);
    }

    // Instructions
    ofSetColor(100);
    string instructions = "Press 'c' to clear chat | 'q' to quit";
    if (font.isLoaded()) {
        font.drawString(instructions, padding, ofGetHeight() - 10);
    } else {
        ofDrawBitmapString(instructions, padding, ofGetHeight() - 10);
    }
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
    if (!aiReady) return;

    if (key == 'c' || key == 'C') {
        // Clear chat
        chatHistory.clear();
        scrollOffset = 0;
        statusMessage = "Chat cleared.";
        ofLogNotice("ofApp") << "Chat history cleared";
    }
    else if (key == 'q' || key == 'Q') {
        ofExit();
    }
    else if (key == OF_KEY_RETURN) {
        if (!userInput.empty() && !isWaitingForResponse) {
            sendMessage();
        }
    }
    else if (key == OF_KEY_BACKSPACE) {
        if (!userInput.empty()) {
            userInput.pop_back();
        }
    }
    else if (key >= 32 && key <= 126) {
        // Printable characters
        userInput += (char)key;
    }
}

//--------------------------------------------------------------
void ofApp::sendMessage() {
    string message = userInput;
    userInput = "";

    // Add user message to chat
    addChatMessage("user", message);

    // Get AI response
    isWaitingForResponse = true;
    statusMessage = "AI is thinking...";

    try {
        auto result = ai.chat(message, "English");

        isWaitingForResponse = false;

        if (result.inference.success) {
            addChatMessage("assistant", result.inference.text);
            statusMessage = "AI ready! Type a message and press Enter.";
        } else {
            statusMessage = "Error: " + result.inference.error;
            ofLogError("ofApp") << "Inference failed: " << result.inference.error;
        }
    } catch (const std::exception& e) {
        isWaitingForResponse = false;
        statusMessage = "Error: " + string(e.what());
        ofLogError("ofApp") << "Exception: " << e.what();
    }
}

//--------------------------------------------------------------
void ofApp::addChatMessage(const string& role, const string& message) {
    ChatMessage msg;
    msg.role = role;
    msg.text = message;
    chatHistory.push_back(msg);

    ofLogNotice("ofApp") << role << ": " << message;
}

//--------------------------------------------------------------
string ofApp::wrapText(const string& text, int width) {
    if (text.empty()) return "";

    string result;
    string line;

    istringstream words(text);
    string word;

    while (words >> word) {
        string testLine = line.empty() ? word : line + " " + word;

        float lineWidth = font.isLoaded() ?
            font.stringWidth(testLine) : testLine.length() * 8;

        if (lineWidth > width - 60) {  // Account for role label
            if (!line.empty()) {
                result += line + "\n";
                line = word;
            } else {
                result += word + "\n";
            }
        } else {
            line = testLine;
        }
    }

    if (!line.empty()) {
        result += line;
    }

    return result;
}
