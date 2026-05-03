#include "ofxGgmlCodeAssistant.h"

#include "core/ofxGgmlHelpers.h"

#include <string>
#include <vector>

namespace {

using ofxGgmlHelpers::trim;

} // namespace

std::vector<ofxGgmlCodeLanguagePreset> ofxGgmlCodeAssistant::defaultLanguagePresets() {
	return {
		{"C++", ".cpp", "You are a C++ expert. Generate modern C++17 code."},
		{"Python", ".py", "You are a Python expert. Generate clean, idiomatic Python 3 code."},
		{"JavaScript", ".js", "You are a JavaScript expert. Generate modern ES6+ code."},
		{"Rust", ".rs", "You are a Rust expert. Generate safe, idiomatic Rust code."},
		{"GLSL", ".glsl", "You are a GLSL shader expert. Generate efficient GPU shader code."},
		{"Go", ".go", "You are a Go expert. Generate idiomatic Go code."},
		{"Bash", ".sh", "You are a Bash scripting expert. Generate portable shell scripts."},
		{"TypeScript", ".ts", "You are a TypeScript expert. Generate type-safe TypeScript code."}
	};
}

std::string ofxGgmlCodeAssistant::defaultActionBody(
	ofxGgmlCodeAssistantAction action,
	const std::string & userInput,
	bool hasFocusedFile,
	const std::string & lastTask,
	const std::string & lastOutput) {
	const std::string trimmedInput = trim(userInput);
	const std::string trimmedTask = trim(lastTask);
	const std::string trimmedOutput = trim(lastOutput);

	auto withExtraInstructions = [&](const std::string & defaultForFile,
		const std::string & prefixForInput) {
		if (hasFocusedFile) {
			if (!trimmedInput.empty()) {
				return defaultForFile + "\n\nExtra instructions:\n" + trimmedInput;
			}
			return defaultForFile;
		}
		return prefixForInput + trimmedInput;
	};

	switch (action) {
	case ofxGgmlCodeAssistantAction::Ask:
		return trimmedInput;
	case ofxGgmlCodeAssistantAction::Generate:
		if (hasFocusedFile && trimmedInput.empty()) {
			return "Generate improved, production-quality code for the focused file. "
				"Follow best practices, add appropriate error handling, and include clear comments.";
		}
		if (!trimmedInput.empty() && hasFocusedFile) {
			return "Generate code for the focused file based on these requirements:\n" + trimmedInput +
				"\n\nFollow best practices, add error handling, and maintain consistency with existing code.";
		}
		return trimmedInput;
	case ofxGgmlCodeAssistantAction::Explain:
		return withExtraInstructions(
			"Explain the focused file code clearly and thoroughly. "
			"Cover the purpose, key logic, data structures, algorithms, and important patterns. "
			"Use clear language suitable for developers unfamiliar with this code.",
			"Explain the following code clearly and thoroughly. "
			"Cover its purpose, key logic, data structures, algorithms, and patterns:\n");
	case ofxGgmlCodeAssistantAction::Debug:
		return withExtraInstructions(
			"Analyze the focused file for bugs, edge cases, and logical errors. "
			"Identify specific issues with line numbers, explain the problem, and suggest fixes.",
			"Find bugs, edge cases, and logical errors in the following code. "
			"Provide line numbers, explain each issue, and suggest fixes:\n");
	case ofxGgmlCodeAssistantAction::Optimize:
		return withExtraInstructions(
			"Optimize the focused file for performance and efficiency. "
			"Identify bottlenecks, propose improvements with benchmarks or complexity analysis, "
			"and show the optimized version with explanations.",
			"Optimize the following code for performance. "
			"Identify bottlenecks, suggest improvements, show the optimized version, "
			"and explain the expected performance gains:\n");
	case ofxGgmlCodeAssistantAction::Edit:
		return withExtraInstructions(
			"Edit the focused file to satisfy the request. "
			"Make only the necessary changes. Keep unrelated code unchanged. "
			"Preserve existing style, patterns, and conventions.",
			"Edit the following code to satisfy the request. "
			"Make only necessary changes. Keep unrelated code and style unchanged:\n");
	case ofxGgmlCodeAssistantAction::Refactor:
		return withExtraInstructions(
			"Refactor the focused file to improve code quality. "
			"Address readability, maintainability, testability, and structural issues. "
			"Extract helper functions, improve naming, reduce complexity, and eliminate code smells. "
			"Show the refactored version with clear explanations of improvements.",
			"Refactor the following code to improve quality. "
			"Extract helpers, improve naming, reduce complexity, eliminate code smells. "
			"Show the refactored version with explanations:\n");
	case ofxGgmlCodeAssistantAction::Review:
		return withExtraInstructions(
			"Perform a comprehensive code review of the focused file. "
			"Check for bugs, security vulnerabilities, performance issues, code style violations, "
			"maintainability concerns, and best practice violations. "
			"Return findings with severity (1=critical, 2=important, 3=minor), confidence, "
			"affected lines, and concrete fix suggestions.",
			"Review the following code comprehensively. "
			"Check for bugs, security issues, performance problems, style violations, "
			"and maintainability concerns. Return findings with severity, confidence, "
			"line numbers, and suggested fixes:\n");
	case ofxGgmlCodeAssistantAction::NextEdit:
		return withExtraInstructions(
			"Predict the most likely next edit in the focused file or nearby files. Return the next edit target, why it matters, and an optional patch or diff for that single next change.",
			"Predict the most likely next edit for the following code and return the next target plus an optional patch:\n");
	case ofxGgmlCodeAssistantAction::SummarizeChanges:
		return "Summarize the provided code changes professionally for reviewers. Return a concise summary, key files, risks, and verification notes.\n\n" +
			trimmedInput;
	case ofxGgmlCodeAssistantAction::FixBuild:
		if (!trimmedOutput.empty()) {
			return "Fix the build or test failure described below. "
				"Analyze the error messages, identify root causes, determine which files need changes, "
				"propose specific fixes with patches or diffs, and include verification commands to confirm the fix.\n\n" +
				trimmedOutput;
		}
		return "Fix the build or test failure for this request. "
			"Analyze error messages, identify root causes, determine affected files, "
			"propose fixes with patches, and include verification commands.\n\n" +
			trimmedInput;
	case ofxGgmlCodeAssistantAction::GroundedDocs:
		return "Answer the request using grounded documentation and source material only. Cite concrete supporting sources where possible.\n\n" +
			trimmedInput;
	case ofxGgmlCodeAssistantAction::ContinueTask: {
		std::string body =
			"Continue the task from the previous response. Keep the same intent and provide next concrete steps.\n\n";
		if (!trimmedOutput.empty()) {
			body += "Previous response:\n" + trimmedOutput;
		} else if (!trimmedTask.empty()) {
			body += "Previous task:\n" + trimmedTask;
		}
		return body;
	}
	case ofxGgmlCodeAssistantAction::Shorter: {
		std::string base = !trimmedTask.empty()
			? trimmedTask
			: std::string("Rewrite the previous response");
		base += "\n\nProvide a shorter answer. Keep only essential code and brief explanation.";
		return base;
	}
	case ofxGgmlCodeAssistantAction::MoreDetail: {
		std::string base = !trimmedTask.empty()
			? trimmedTask
			: std::string("Expand the previous response");
		base += "\n\nProvide a more detailed answer with reasoning, edge cases, and step-by-step implementation notes.";
		return base;
	}
	case ofxGgmlCodeAssistantAction::ContinueCutoff: {
		const std::string tail = !trimmedInput.empty() ? trimmedInput : trimmedOutput;
		return ofxGgmlInference::buildCutoffContinuationRequest(tail);
	}
	}

	return trimmedInput;
}

std::string ofxGgmlCodeAssistant::defaultActionLabel(
	ofxGgmlCodeAssistantAction action,
	const std::string & userInput,
	const std::string & focusedFileName) {
	const std::string trimmedInput = trim(userInput);
	const bool hasFocusedFile = !trim(focusedFileName).empty();

	auto appendInstructions = [&](std::string label) {
		if (!trimmedInput.empty()) {
			label += " Instructions: " + trimmedInput;
		}
		return label;
	};

	switch (action) {
	case ofxGgmlCodeAssistantAction::Ask:
		return trimmedInput;
	case ofxGgmlCodeAssistantAction::Generate:
		return appendInstructions(hasFocusedFile
			? "Generate focused file."
			: "Generate code.");
	case ofxGgmlCodeAssistantAction::Explain:
		return appendInstructions(hasFocusedFile
			? "Explain focused file."
			: "Explain code.");
	case ofxGgmlCodeAssistantAction::Debug:
		return appendInstructions(hasFocusedFile
			? "Debug focused file."
			: "Debug code.");
	case ofxGgmlCodeAssistantAction::Optimize:
		return appendInstructions(hasFocusedFile
			? "Optimize focused file."
			: "Optimize code.");
	case ofxGgmlCodeAssistantAction::Edit:
		return appendInstructions(hasFocusedFile
			? "Edit focused file."
			: "Edit code.");
	case ofxGgmlCodeAssistantAction::Refactor:
		return appendInstructions(hasFocusedFile
			? "Refactor focused file."
			: "Refactor code.");
	case ofxGgmlCodeAssistantAction::Review:
		return appendInstructions(hasFocusedFile
			? "Review focused file."
			: "Review code.");
	case ofxGgmlCodeAssistantAction::NextEdit:
		return appendInstructions(hasFocusedFile
			? "Suggest next edit for focused file."
			: "Suggest next edit.");
	case ofxGgmlCodeAssistantAction::SummarizeChanges:
		return appendInstructions("Summarize local changes.");
	case ofxGgmlCodeAssistantAction::FixBuild:
		return "Fix build or test failure.";
	case ofxGgmlCodeAssistantAction::GroundedDocs:
		return appendInstructions("Answer with grounded docs.");
	case ofxGgmlCodeAssistantAction::ContinueTask:
		return "Continue the previous task.";
	case ofxGgmlCodeAssistantAction::Shorter:
		return "Provide a shorter answer for the previous task.";
	case ofxGgmlCodeAssistantAction::MoreDetail:
		return "Provide more detail for the previous task.";
	case ofxGgmlCodeAssistantAction::ContinueCutoff:
		return "Continue from cutoff.";
	}

	return trimmedInput;
}


std::string ofxGgmlCodeAssistant::buildStructuredResponseInstructions() {
	return
		"Return a structured plan using one item per line with these tags:\n"
		"\n"
		"Planning tags:\n"
		"GOAL: concise summary of the objective (one sentence)\n"
		"APPROACH: high-level strategy (one sentence)\n"
		"STEP: concrete actionable step (be specific, ideally inspect -> patch -> verify)\n"
		"ACCEPT: acceptance criterion or invariant (testable condition)\n"
		"\n"
		"File context tags:\n"
		"FILE: relative/path | reason for touching | comma,separated,symbols\n"
		"Prefer real file paths and real symbol names over placeholders.\n"
		"\n"
		"Patch operation tags (prefer DIFF over PATCH when possible):\n"
		"PATCH: write|replace|append|delete | relative/path | brief summary\n"
		"SEARCH: escaped single-line search text (for replace operations only)\n"
		"REPLACE: escaped single-line replacement text (for replace operations only)\n"
		"CONTENT: escaped single-line file content (for write/append, use \\n for newlines)\n"
		"DIFF: unified diff format (preferred for multi-line changes)\n"
		"\n"
		"Verification tags:\n"
		"COMMAND: label | working-dir | executable | arg1 | arg2 | ...\n"
		"EXPECT: expected outcome for the previous COMMAND\n"
		"RETRY: true|false for the previous COMMAND\n"
		"TEST: test-name | relative/test/path | rationale | command-label | command-tag\n"
		"\n"
		"Review tags:\n"
		"REVIEWER: reviewer persona for subsequent FINDING lines\n"
		"FINDING: priority(1-3) | confidence(0.0-1.0) | relative/path | line | title\n"
		"DETAIL: detailed description for the previous FINDING\n"
		"FIX: concrete fix suggestion for the previous FINDING\n"
		"CATEGORY: category for the previous FINDING (e.g., security, performance, style)\n"
		"\n"
		"Risk assessment tags:\n"
		"RISK-SCORE: 0.0 to 1.0 overall change risk\n"
		"RISK-LEVEL: low|medium|high|critical\n"
		"RISK: specific possible risk or regression\n"
		"QUESTION: unresolved question requiring clarification\n"
		"\n"
		"Self-check expectations:\n"
		"- Make sure the plan answers the request directly.\n"
		"- Stay within the stated constraints and allowed files.\n"
		"- If verification cannot be run, say so explicitly in RISK or QUESTION.\n"
		"\n"
		"Important: Use escaped single-line values for SEARCH, REPLACE, CONTENT, and DIFF.\n"
		"For DIFF, prefer unified diff format. For multi-line content, use \\n as newline separator.\n"
		"Priority: 1=critical, 2=important, 3=minor. Always include at least GOAL and APPROACH.";
}

