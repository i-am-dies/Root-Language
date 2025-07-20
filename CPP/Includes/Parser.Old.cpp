#pragma once

#include "Lexer.cpp"

struct Parser {
	using Location = Lexer::Location;
	using Token = Lexer::Token;

	deque<Token> tokens;

	Parser(deque<Token> tokens) : tokens(filter(tokens, [](auto& t) { return !t.trivia; })) {}

	template<typename... Args>
	NodeValue rules(const string& type, Args... args) {
		vector<any> arguments = {args...};

		if(type == "argument") {
			Node node = {
				{"type", "argument"},
				{"range", {
					{"start" , position}
				}},
				{"label", rules("identifier")},
				{"value", nullptr}
			};

			if(!node.empty("label") && token().type.starts_with("operator") && token().value == ":") {
				position++;
			} else {
				position = node.get<Node&>("range").get("start");
				node["label"] = nullptr;
			}

			node["value"] = rules("expressionsSequence");

			if(node.empty("label") && node.empty("value")) {
				return nullptr;
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "arrayLiteral") {
			Node node = {
				{"type", "arrayLiteral"},
				{"range", Node {}},
				{"values", NodeArray {}}
			};

			if(token().type != "bracketOpen") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["values"] = helpers_sequentialNodes(
				{"expressionsSequence"},
				[this]() { return token().type.starts_with("operator") && token().value == ","; }
			);

			if(token().type != "bracketClosed") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			node.get<Node&>("range")["end"] = position++;

			return node;
		} else
		if(type == "arrayType") {
			Node node = {
				{"type", "arrayType"},
				{"range", Node {}},
				{"value", nullptr}
			};

			if(token().type != "bracketOpen") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["value"] = rules("type");

			if(token().type != "bracketClosed") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			node.get<Node&>("range")["end"] = position++;

			return node;
		} else
		if(type == "asyncExpression") {
			Node node = {
				{"type", "asyncExpression"},
				{"range", Node {}},
				{"value", nullptr}
			};

			if(token().type != "keywordAsync") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["value"] = rules("expression");

			if(node.empty("value")) {
				position--;

				return nullptr;
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "asOperator") {
			Node node = {
				{"type", "asOperator"},
				{"range", {
					{"start", position}
				}},
				{"type_", nullptr}
			};

			if(token().type != "keywordAs") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			position++;
			node["type_"] = rules("type");

			if(node.empty("type_")) {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "awaitExpression") {
			Node node = {
				{"type", "awaitExpression"},
				{"range", Node {}},
				{"value", nullptr}
			};

			if(token().type != "keywordAwait") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["value"] = rules("expression");

			if(node.empty("value")) {
				position--;

				return nullptr;
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "body") {
			string type = any_cast<const char*>(arguments[0]);
			Node node = {
				{"type", type+"Body"},
				{"range", Node {}},
				{"statements", NodeArray {}}
			};

			if(token().type != "braceOpen") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["statements"] = rules(type+"Statements");

			if(node.get<NodeArray&>("statements").empty()) {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No statements.");
			}

			if(!tokensEnd()) {
				node.get<Node&>("range")["end"] = position++;
			} else {
				node.get<Node&>("range")["end"] = position-1;

				report(1, node.get<Node&>("range").get("start"), node.get("type"), "Node doesn't have the closing brace and was decided to be autoclosed at the end of stream.");
			}

			return node;
		} else
		if(type == "booleanLiteral") {
			Node node = {
				{"type", "booleanLiteral"},
				{"range", Node {}},
				{"value", nullptr}
			};

			if(!set<string> {"keywordFalse", "keywordTrue"}.contains(token().type)) {
				return nullptr;
			}

			node["value"] = token().value;
			node.get<Node&>("range")["start"] =
			node.get<Node&>("range")["end"] = position++;

			return node;
		} else
		if(type == "breakStatement") {
			Node node = {
				{"type", "breakStatement"},
				{"range", Node {}},
				{"label", nullptr}
			};

			if(token().type != "keywordBreak") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["label"] = rules("identifier");
			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "callExpression") {
			NodeSP node_ = any_cast<NodeSP>(arguments[0]);
			Node node = {
				{"type", "callExpression"},
				{"range", {
					{"start", node_->get<Node&>("range").get("start")}
				}},
				{"callee", node_},
				{"genericArguments", NodeArray {}},
				{"arguments", NodeArray {}},
				{"closure", nullptr}
			};

			if(token().type.starts_with("operator") && token().value == "<") {
				position++;
				node["genericArguments"] = helpers_sequentialNodes(
					{"type"},
					[this]() { return token().type.starts_with("operator") && token().value == ","; }
				);

				if(token().type.starts_with("operator") && token().value == ">") {
					position++;
				} else {
					position = node_->get<Node&>("range").get<int>("end")+1;
				}
			}

			node.get<Node&>("range")["end"] = position-1;

			if(token().type == "parenthesisOpen") {
				position++;
				node["arguments"] = helpers_skippableNodes(
					{"argument"},
					[this]() { return token().type == "parenthesisOpen"; },
					[this]() { return token().type == "parenthesisClosed"; },
					[this]() { return token().type.starts_with("operator") && token().value == ","; }
				);

				if(token().type == "parenthesisClosed") {
					position++;
				} else {
					node.get<Node&>("range")["end"] = position-1;

					report(1, node.get<Node&>("range").get("start"), node.get("type"), "Node doesn't have the closing parenthesis and was decided to be autoclosed at the end of stream.");

					return node;
				}
			}

			node["closure"] = rules("closureExpression");

			if(node.empty("closure") && node.get<Node&>("range").get("end") == position-1) {
				position = node_->get<Node&>("range").get<int>("end")+1;

				return nullptr;
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "caseDeclaration") {
			Node node = {
				{"type", "caseDeclaration"},
				{"range", Node {}},
				{"identifiers", NodeArray {}}
			};

			if(token().type != "keywordCase") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["identifiers"] = helpers_sequentialNodes(
				{"identifier"},
				[this]() { return token().type.starts_with("operator") && token().value == ","; }
			);

			if(node.get<NodeArray&>("identifiers").empty()) {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No identifiers(s).");
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "catchClause") {
			Node node = {
				{"type", "catchClause"},
				{"range", Node {}},
				{"typeIdentifiers", NodeArray {}},
				{"body", nullptr},
				{"catch", nullptr}
			};

			if(token().type != "keywordCatch") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["typeIdentifiers"] = helpers_sequentialNodes(
				{"typeIdentifier"},
				[this]() { return token().type.starts_with("operator") && token().value == ","; }
			);
			node["body"] = rules("functionBody");
			node["catch"] = rules("catchClause");

			if(node.get<NodeArray&>("typeIdentifiers").empty()) {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No type identifiers.");
			}
			if(node.empty("body")) {
				report(1, node.get<Node&>("range").get("start"), node.get("type"), "No body.");
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "chainDeclaration") {
			Node node = {
				{"type", "chainDeclaration"},
				{"range", {
					{"start", position}
				}},
				{"modifiers", rules("modifiers")},
				{"body", nullptr}
			};

			if(token().type != "identifier" || token().value != "chain") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["body"] = rules("observersBody");

			if(some(node.get<NodeArray&>("modifiers"), [](auto& v) { return v != "static"; })) {
				report(1, node.get<Node&>("range").get("start"), node.get("type"), "Can only have specific modifier (static).");
			}
			if(node.empty("body")) {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No body.");
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "chainExpression") {
			NodeSP node_ = any_cast<NodeSP>(arguments[0]);
			Node node = {
				{"type", "chainExpression"},
				{"range", {
					{"start", node_->get<Node&>("range").get("start")}
				}},
				{"composite", node_},
				{"member", nullptr}
			};

			if(!set<string> {"operator", "operatorInfix"}.contains(token().type) || token().value != ".") {
				return nullptr;
			}

			position++;
			node["member"] = rules("identifier") ?:
							 rules("stringLiteral");

			if(node.empty("member")) {
				position = node_->get<Node&>("range").get<int>("end")+1;

				return nullptr;
			}

			node.get<Node&>("range")["end"] = node.get<Node&>("member").get<Node&>("range").get("end");

			return node;
		} else
		if(type == "chainIdentifier") {
			NodeSP node_ = any_cast<NodeSP>(arguments[0]);
			Node node = {
				{"type", "chainIdentifier"},
				{"range", {
					{"start", node_->get<Node&>("range").get("start")}
				}},
				{"supervalue", node_},
				{"value", nullptr}
			};

			if(!set<string> {"operatorPrefix", "operatorInfix"}.contains(token().type) || token().value != ".") {
				return nullptr;
			}

			position++;
			node["value"] = rules("identifier");

			if(node.empty("value")) {
				position = node_->get<Node&>("range").get<int>("end")+1;

				return nullptr;
			}

			node.get<Node&>("range")["end"] = node.get<Node&>("value").get<Node&>("range").get("end");

			return node;
		} else
		if(type == "chainStatements") {
			return rules("statements", vector<string> {"observerDeclaration"});
		} else
		if(type == "classBody") {
			return rules("body", "class");
		} else
		if(type == "classDeclaration") {
			bool anonymous = !arguments.empty() && any_cast<bool>(arguments[0]);
			Node node = {
				{"type", string("class")+(!anonymous ? "Declaration" : "Expression")},
				{"range", {
					{"start", position}
				}},
				{"modifiers", rules("modifiers")},
				{"identifier", nullptr},
				{"genericParameters", nullptr},
				{"inheritedTypes", nullptr},
				{"body", nullptr}
			};

			if(token().type != "keywordClass") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			position++;
			node["identifier"] = rules("identifier");

			if(anonymous) {
				if(!node.get<NodeArray&>("modifiers").empty() || !node.empty("identifier")) {
					position = node.get<Node&>("range").get("start");

					return nullptr;
				}

				node.remove("modifiers");
				node.remove("identifier");
			} else
			if(node.empty("identifier")) {
				report(2, node.get<Node&>("range").get("start"), node.get("type"), "No identifier.");
			}

			node["genericParameters"] = rules("genericParametersClause");
			node["inheritedTypes"] = rules("inheritedTypesClause");
			node["body"] = rules("classBody");

			if(!node.empty("modifiers") && some(node.get<NodeArray&>("modifiers"), [](auto& v) { return !set<string> {"private", "protected", "public", "static", "final"}.contains(v); })) {
				report(1, node.get<Node&>("range").get("start"), node.get("type"), "Wrong modifier(s).");
			}
			if(node.empty("body")) {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No body.");
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "classExpression") {
			return rules("classDeclaration", true);
		} else
		if(type == "classStatements") {
			return rules("statements", vector<string> {
				"chainDeclaration",
				"classDeclaration",
				"deinitializerDeclaration",
				"enumerationDeclaration",
				"functionDeclaration",
				"initializerDeclaration",
				"namespaceDeclaration",
				"protocolDeclaration",
				"structureDeclaration",
				"subscriptDeclaration",
				"variableDeclaration"
			});
		} else
		if(type == "closureExpression") {
			Node node = {
				{"type", "closureExpression"},
				{"range", Node {}},
				{"signature", nullptr},
				{"statements", NodeArray {}}
			};

			if(token().type != "braceOpen") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["signature"] = rules("functionSignature");

			if(!node.empty("signature")) {
				if(token().type != "keywordIn") {
					position = node.get<Node&>("range").get("start");

					return nullptr;
				}

				position++;
			} else {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No signature.");
			}

			node["statements"] = rules("functionStatements");

			if(node.get<NodeArray&>("statements").empty()) {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No statements.");
			}

			if(!tokensEnd()) {
				node.get<Node&>("range")["end"] = position++;
			} else {
				node.get<Node&>("range")["end"] = position-1;

				report(1, node.get<Node&>("range").get("start"), node.get("type"), "Node doesn't have the closing brace and was decided to be autoclosed at the end of stream.");
			}

			return node;
		} else
		if(type == "conditionalOperator") {
			Node node = {
				{"type", "conditionalOperator"},
				{"range", Node {}},
				{"expression", nullptr}
			};

			if(!token().type.starts_with("operator") || token().value != "?") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["expression"] = rules("expressionsSequence");

			if(!token().type.starts_with("operator") || token().value != ":") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			node.get<Node&>("range")["end"] = position++;

			return node;
		} else
		if(type == "continueStatement") {
			Node node = {
				{"type", "continueStatement"},
				{"range", Node {}},
				{"label", nullptr}
			};

			if(token().type != "keywordContinue") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["label"] = rules("identifier");
			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "controlTransferStatement") {
			return (
				rules("breakStatement") ?:
				rules("continueStatement") ?:
				rules("fallthroughStatement") ?:
				rules("returnStatement") ?:
				rules("throwStatement")
			);
		} else
		if(type == "declaration") {
			return (
				rules("chainDeclaration") ?:
				rules("classDeclaration") ?:
				rules("deinitializerDeclaration") ?:
				rules("enumerationDeclaration") ?:
				rules("functionDeclaration") ?:
				rules("importDeclaration") ?:
				rules("initializerDeclaration") ?:
				rules("namespaceDeclaration") ?:
				rules("operatorDeclaration") ?:
				rules("protocolDeclaration") ?:
				rules("structureDeclaration") ?:
				rules("subscriptDeclaration") ?:
				rules("variableDeclaration")
			);
		} else
		if(type == "declarator") {
			Node node = {
				{"type", "declarator"},
				{"range", {
					{"start", position}
				}},
				{"identifier", rules("identifier")},
				{"type_", nullptr},
				{"value", nullptr},
				{"body", nullptr}
			};

			if(node.empty("identifier")) {
				return nullptr;
			}

			node["type_"] = rules("typeClause");
			node["value"] = rules("initializerClause");

			helpers_bodyTrailedValue(node, "value", "body", false, [this]() { return rules("observersBody", true) ?: rules("functionBody"); });

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "defaultExpression") {
			NodeSP node_ = any_cast<NodeSP>(arguments[0]);
			Node node = {
				{"type", "defaultExpression"},
				{"range", {
					{"start", node_->get<Node&>("range").get("start")}
				}},
				{"value", node_}
			};

			if(token().type != "operatorPostfix" || token().value != "!") {
				return nullptr;
			}

			node.get<Node&>("range")["end"] = position++;

			return node;
		} else
		if(type == "defaultType") {
			NodeSP node_ = any_cast<NodeSP>(arguments[0]);
			Node node = {
				{"type", "defaultType"},
				{"range", {
					{"start", node_->get<Node&>("range").get("start")}
				}},
				{"value", node_}
			};

			if(token().type != "operatorPostfix" || token().value != "!") {
				return nullptr;
			}

			node.get<Node&>("range")["end"] = position++;

			return node;
		} else
		if(type == "deinitializerDeclaration") {
			Node node = {
				{"type", "deinitializerDeclaration"},
				{"range", Node {}},
				{"body", nullptr}
			};

			if(token().type != "identifier" || token().value != "deinit") {
				return nullptr;
			}

			position++;
			node["body"] = rules("functionBody");

			if(node.empty("body")) {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No body.");
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "deleteExpression") {
			Node node = {
				{"type", "deleteExpression"},
				{"range", Node {}},
				{"value", nullptr}
			};

			if(token().type != "identifier" || token().value != "delete") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["value"] = rules("expression");

			if(node.empty("value")) {
				position--;

				return nullptr;
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "dictionaryLiteral") {
			Node node = {
				{"type", "dictionaryLiteral"},
				{"range", Node {}},
				{"entries", NodeArray {}}
			};

			if(token().type != "bracketOpen") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["entries"] = helpers_sequentialNodes(
				{"entry"},
				[this]() { return token().type.starts_with("operator") && token().value == ","; }
			);

			if(node.get<NodeArray&>("entries").empty() && token().type.starts_with("operator") && token().value == ":") {
				position++;
			}

			if(token().type != "bracketClosed") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			node.get<Node&>("range")["end"] = position++;

			return node;
		} else
		if(type == "dictionaryType") {
			Node node = {
				{"type", "dictionaryType"},
				{"range", Node {}},
				{"key", nullptr},
				{"value", nullptr}
			};

			if(token().type != "bracketOpen") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["key"] = rules("type");

			if(!token().type.starts_with("operator") || token().value != ":") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			position++;
			node["value"] = rules("type");

			if(token().type != "bracketClosed") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			node.get<Node&>("range")["end"] = position++;

			return node;
		} else
		if(type == "doStatement") {
			Node node = {
				{"type", "doStatement"},
				{"range", Node {}},
				{"body", nullptr},
				{"catch", nullptr}
			};

			if(token().type != "keywordDo") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["body"] = rules("functionBody");
			node["catch"] = rules("catchClause");

			if(node.empty("body")) {
				report(2, node.get<Node&>("range").get("start"), node.get("type"), "No body.");
			}
			if(node.empty("catch")) {
				report(1, node.get<Node&>("range").get("start"), node.get("type"), "No catch.");
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "elseClause") {
			NodeSP node;
			int start = position;

			if(token().type != "keywordElse") {
				return nullptr;
			}

			position++;
			node =
				rules("functionBody") ?:
				rules("expressionsSequence") ?:
				rules("ifStatement");

			if(!node) {
				report(0, start, "elseClause", "No value.");
			}

			return node;
		} else
		if(type == "entry") {
			Node node = {
				{"type", "entry"},
				{"range", {
					{"start", position}
				}},
				{"key", rules("expressionsSequence")},
				{"value", nullptr}
			};

			if(node.empty("key") || !token().type.starts_with("operator") || token().value != ":") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			position++;
			node["value"] = rules("expressionsSequence");

			if(node.empty("value")) {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "enumerationBody") {
			return rules("body", "enumeration");
		} else
		if(type == "enumerationDeclaration") {
			bool anonymous = !arguments.empty() && any_cast<bool>(arguments[0]);
			Node node = {
				{"type", string("enumeration")+(!anonymous ? "Declaration" : "Expression")},
				{"range", {
					{"start", position}
				}},
				{"modifiers", rules("modifiers")},
				{"identifier", nullptr},
				{"inheritedTypes", nullptr},
				{"body", nullptr}
			};

			if(token().type != "keywordEnum") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			position++;
			node["identifier"] = rules("identifier");

			if(anonymous) {
				if(!node.get<NodeArray&>("modifiers").empty() || !node.empty("identifier")) {
					position = node.get<Node&>("range").get("start");

					return nullptr;
				}

				node.remove("modifiers");
				node.remove("identifier");
			} else
			if(node.empty("identifier")) {
				report(2, node.get<Node&>("range").get("start"), node.get("type"), "No identifier.");
			}

			node["inheritedTypes"] = rules("inheritedTypesClause");
			node["body"] = rules("enumerationBody");

			if(!node.empty("modifiers") && some(node.get<NodeArray&>("modifiers"), [](auto& v) { return !set<string> {"private", "protected", "public", "static", "final"}.contains(v); })) {
				report(1, node.get<Node&>("range").get("start"), node.get("type"), "Wrong modifier(s).");
			}
			if(node.empty("body")) {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No body.");
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "enumerationExpression") {
			return rules("enumerationDeclaration", true);
		} else
		if(type == "enumerationStatements") {
			return rules("statements", vector<string> {
				"caseDeclaration",
				"enumerationDeclaration"
			});
		} else
		if(type == "expression") {
			return (
				rules("asyncExpression") ?:
				rules("awaitExpression") ?:
				rules("deleteExpression") ?:
				rules("tryExpression") ?:
				rules("prefixExpression")
			);
		} else
		if(type == "expressionsSequence") {
			Node node = {
				{"type", "expressionsSequence"},
				{"range", {
					{"start", position}
				}},
				{"values", NodeArray {}}
			};

			set<string> subsequentialTypes = {"asOperator", "inOperator", "isOperator"};
			NodeArray& values = node["values"] = helpers_sequentialNodes({"expression", "infixExpression"}, nullptr, subsequentialTypes);

			if(values.empty()) {
				return nullptr;
			}
			if(filter(values, [&](const Node& v) { return !subsequentialTypes.contains(v.get("type")); }).size()%2 == 0) {
				position = values.back().get<NodeSP>()->get<Node&>("range").get("start");

				values.pop_back();
			}
			if(values.size() == 1) {
				return values.at(0);
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "fallthroughStatement") {
			Node node = {
				{"type", "fallthroughStatement"},
				{"range", Node {}},
				{"label", nullptr}
			};

			if(token().type != "keywordFallthrough") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["label"] = rules("identifier");
			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "floatLiteral") {
			Node node = {
				{"type", "floatLiteral"},
				{"range", Node {}},
				{"value", nullptr}
			};

			if(token().type != "numberFloat") {
				return nullptr;
			}

			node["value"] = token().value;
			node.get<Node&>("range")["start"] =
			node.get<Node&>("range")["end"] = position++;

			return node;
		} else
		if(type == "forStatement") {
			Node node = {
				{"type", "forStatement"},
				{"range", Node {}},
				{"identifier", nullptr},
				{"in", nullptr},
				{"where", nullptr},
				{"value", nullptr}
			};

			if(token().type != "keywordFor") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["identifier"] = rules("identifier");

			if(token().type == "keywordIn") {
				position++;
				node["in"] = rules("expressionsSequence");
			}
			if(token().type == "keywordWhere") {
				position++;
				node["where"] = rules("expressionsSequence");
			}

			helpers_bodyTrailedValue(node, !node.empty("where") ? "where" : "in", "value");

			if(node.empty("identifier")) {
				report(1, node.get<Node&>("range").get("start"), node.get("type"), "No identifier.");
			}
			if(node.empty("in")) {
				report(2, node.get<Node&>("range").get("start"), node.get("type"), "No in.");
			}
			if(node.empty("value")) {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No value.");
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "functionBody") {
			return rules("body", "function");
		} else
		if(type == "functionDeclaration") {
			bool anonymous = !arguments.empty() && any_cast<bool>(arguments[0]);
			Node node = {
				{"type", string("function")+(!anonymous ? "Declaration" : "Expression")},
				{"range", {
					{"start", position}
				}},
				{"modifiers", rules("modifiers")},
				{"identifier", nullptr},
				{"signature", nullptr},
				{"body", nullptr}
			};

			if(token().type != "keywordFunc") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			position++;
			node["identifier"] =
				rules("identifier") ?:
				rules("operator");

			if(anonymous) {
				if(!node.get<NodeArray&>("modifiers").empty() || !node.empty("identifier")) {
					position = node.get<Node&>("range").get("start");

					return nullptr;
				}

				node.remove("modifiers");
				node.remove("identifier");
			} else
			if(node.empty("identifier")) {
				report(2, node.get<Node&>("range").get("start"), node.get("type"), "No identifier.");
			}

			node["signature"] = rules("functionSignature");
			node["body"] = rules("functionBody");

			if(node.empty("signature")) {
				report(1, node.get<Node&>("range").get("start"), node.get("type"), "No signature.");
			}
			if(node.empty("body")) {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No body.");
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "functionExpression") {
			return rules("functionDeclaration", true);
		} else
		if(type == "functionSignature") {
			Node node = {
				{"type", "functionSignature"},
				{"range", {
					{"start", position}
				}},
				{"genericParameters", rules("genericParametersClause")},
				{"parameters", NodeArray {}},
				{"inits", -1},
				{"deinits", -1},
				{"awaits", -1},
				{"throws", -1},
				{"returnType", nullptr}
			};

			if(token().type == "parenthesisOpen") {
				position++;
				node["parameters"] = helpers_skippableNodes(
					{"parameter"},
					[this]() { return token().type == "parenthesisOpen"; },
					[this]() { return token().type == "parenthesisClosed"; },
					[this]() { return token().type.starts_with("operator") && token().value == ","; }
				);

				if(token().type == "parenthesisClosed") {
					position++;
				} else {
					node.get<Node&>("range")["end"] = position-1;

					report(1, node.get<Node&>("range").get("start"), node.get("type"), "Parameters don't have the closing parenthesis and were decided to be autoclosed at the end of stream.");

					return node;
				}
			}

			while(set<string> {"keywordAwaits", "keywordThrows"}.contains(token().type)) {
				if(token().type == "keywordAwaits") {
					position++;
					node["awaits"] = 1;
				}
				if(token().type == "keywordThrows") {
					position++;
					node["throws"] = 1;
				}
			}

			if(token().type.starts_with("operator") && token().value == "->") {
				position++;
				node["returnType"] = rules("type");
			}

			node.get<Node&>("range")["end"] = position-1;

			if(node.get<Node&>("range").get<int>("end") < node.get<Node&>("range").get<int>("start")) {
				return nullptr;
			}

			return node;
		} else
		if(type == "functionStatement") {
			NodeArraySP types = rules("functionStatements", true);

			for(const string& type : *types) {
				NodeSP node = rules(type);

				if(node) {
					return node;
				}
			}
		} else
		if(type == "functionStatements") {
			bool list = !arguments.empty() && any_cast<bool>(arguments[0]);
			NodeValue types = {
				"expressionsSequence",  // Expressions must be parsed first as they may include (anonymous) declarations
				"declaration",
				"controlTransferStatement",
				"doStatement",
				"forStatement",
				"ifStatement",
				"whileStatement"
			};

			return !list ? rules("statements", types) : types;
		} else
		if(type == "functionType") {
			Node node = {
				{"type", "functionType"},
				{"range", {
					{"start", position},
					{"end", position}
				}},
				{"genericParameterTypes", rules("genericParametersClause")},
				{"parameterTypes", NodeArray {}},
				{"awaits", -1},
				{"throws", -1},
				{"returnType", nullptr}
			};

			if(token().type.starts_with("operator") && token().value == "<") {
				position++;
				node["genericParameterTypes"] = helpers_sequentialNodes(
					{"type"},
					[this]() { return token().type.starts_with("operator") && token().value == ","; }
				);

				if(token().type.starts_with("operator") && token().value == ">") {
					node.get<Node&>("range")["end"] = position++;
				} else {
					position = node.get<Node&>("range").get("start");

					return nullptr;
				}
			}

			if(token().type != "parenthesisOpen") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			position++;
			node["parameterTypes"] = helpers_sequentialNodes(
				{"type"},
				[this]() { return token().type.starts_with("operator") && token().value == ","; }
			);

			if(token().type != "parenthesisClosed") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			position++;

			while(set<string> {"keywordAwaits", "keywordThrows"}.contains(token().type)) {
				if(token().type == "keywordAwaits") {
					position++;
					node["awaits"] = 1;

					if(token().type == "operatorPostfix" && token().value == "?") {
						position++;
						node["awaits"] = 0;
					}
				}
				if(token().type == "keywordThrows") {
					position++;
					node["throws"] = 1;

					if(token().type == "operatorPostfix" && token().value == "?") {
						position++;
						node["throws"] = 0;
					}
				}
			}

			if(!token().type.starts_with("operator") || token().value != "->") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			position++;
			node["returnType"] = rules("type");

			if(node.empty("returnType")) {
				report(1, node.get<Node&>("range").get("start"), node.get("type"), "No return type.");
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "genericParameter") {
			Node node = {
				{"type", "genericParameter"},
				{"range", {
					{"start", position}
				}},
				{"identifier", rules("identifier")},
				{"type_", nullptr}
			};

			if(node.empty("identifier")) {
				return nullptr;
			}

			node["type_"] = rules("typeClause");
			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "genericParametersClause") {
			NodeArray nodes;
			int start = position;

			if(!token().type.starts_with("operator") || token().value != "<") {
				return nodes;
			}

			position++;
			nodes = helpers_skippableNodes(
				{"genericParameter"},
				[this]() { return token().type.starts_with("operator") && token().value == "<"; },
				[this]() { return token().type.starts_with("operator") && token().value == ">"; },
				[this]() { return token().type.starts_with("operator") && token().value == ","; }
			);

			if(!token().type.starts_with("operator") || token().value != ">") {
				report(1, start, "genericParametersClause", "Node doesn't have the closing angle and was decided to be autoclosed at the end of stream.");

				return nodes;
			}

			position++;

			return nodes;
		} else
		if(type == "identifier") {
			Node node = {
				{"type", "identifier"},
				{"range", Node {}},
				{"value", nullptr}
			};

			if(token().type != "identifier") {
				return nullptr;
			}

			node["value"] = token().value;
			node.get<Node&>("range")["start"] =
			node.get<Node&>("range")["end"] = position++;

			return node;
		} else
		if(type == "ifStatement") {
			Node node = {
				{"type", "ifStatement"},
				{"range", Node {}},
				{"condition", nullptr},
				{"then", nullptr},
				{"else", nullptr}
			};

			if(token().type != "keywordIf") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["condition"] = rules("expressionsSequence");

			helpers_bodyTrailedValue(node, "condition", "then");

			node["else"] = rules("elseClause");

			if(node.empty("condition")) {
				report(2, node.get<Node&>("range").get("start"), node.get("type"), "No condition.");
			}
			if(node.empty("then")) {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No value.");
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "implicitChainExpression") {
			Node node = {
				{"type", "implicitChainExpression"},
				{"range", Node {}},
				{"member", nullptr}
			};

			if(!token().type.starts_with("operator") || token().type.ends_with("Postfix") || token().value != ".") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["member"] =
				rules("identifier") ?:
				rules("stringLiteral");

			if(node.empty("member")) {
				position--;

				return nullptr;
			}

			node.get<Node&>("range")["end"] = node.get<Node&>("member").get<Node&>("range").get("end");

			return node;
		} else
		if(type == "implicitChainIdentifier") {
			Node node = {
				{"type", "implicitChainIdentifier"},
				{"range", Node {}},
				{"value", nullptr}
			};

			if(!token().type.starts_with("operator") || token().type.ends_with("Postfix") || token().value != ".") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["value"] = rules("identifier");

			if(node.empty("value")) {
				position--;

				return nullptr;
			}

			node.get<Node&>("range")["end"] = node.get<Node&>("value").get<Node&>("range").get("end");

			return node;
		} else
		if(type == "importDeclaration") {
			Node node = {
				{"type", "importDeclaration"},
				{"range", Node {}},
				{"value", nullptr}
			};

			if(token().type != "keywordImport") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] =
			node.get<Node&>("range")["end"] = position++;
			node["value"] = rules("identifier");

			if(!node.empty("value")) {
				while(!tokensEnd()) {
					NodeSP node_ = rules("chainIdentifier", node.get<NodeSP>("value"));

					if(!node_) {
						break;
					}

					node["value"] = node_;
				}

				node.get<Node&>("range")["end"] = node.get<Node&>("value").get<Node&>("range").get("end");
			} else {
				report(1, node.get<Node&>("range").get("start"), node.get("type"), "No value.");
			}

			return node;
		} else
		if(type == "infixExpression") {
			return (
				rules("asOperator") ?:
				rules("conditionalOperator") ?:
				rules("inOperator") ?:
				rules("isOperator") ?:
				rules("infixOperator")
			);
		} else
		if(type == "infixOperator") {
			Node node = {
				{"type", "infixOperator"},
				{"range", Node {}},
				{"value", nullptr}
			};

			set<string> exceptions = {",", ":"};  // Enclosing nodes can use operators from the list as delimiters

			if(!set<string> {"operator", "operatorInfix"}.contains(token().type) || exceptions.contains(token().value)) {
				return nullptr;
			}

			node["value"] = token().value;
			node.get<Node&>("range")["start"] =
			node.get<Node&>("range")["end"] = position++;

			return node;
		} else
		if(type == "inheritedTypesClause") {
			NodeArray nodes;
			int start = position;

			if(!token().type.starts_with("operator") || token().value != ":") {
				return nodes;
			}

			position++;
			nodes = helpers_sequentialNodes(
				{"typeIdentifier"},
				[this]() { return token().type.starts_with("operator") && token().value == ","; }
			);

			if(nodes.empty()) {
				report(0, start, "inheritedTypesClause", "No type identifiers.");
			}

			return nodes;
		} else
		if(type == "initializerClause") {
			NodeSP node;
			int start = position;

			if(!token().type.starts_with("operator") || token().value != "=") {
				return nullptr;
			}

			position++;
			node = rules("expressionsSequence");

			if(!node) {
				report(0, start, "initializerClause", "No value.");
			}

			return node;
		} else
		if(type == "initializerDeclaration") {
			Node node = {
				{"type", "initializerDeclaration"},
				{"range", {
					{"start", position}
				}},
				{"modifiers", rules("modifiers")},
				{"nillable", false},
				{"signature", nullptr},
				{"body", nullptr}
			};

			if(token().type != "identifier" || token().value != "init") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			position++;

			if(token().type.starts_with("operator") && token().value == "?") {
				position++;
				node["nillable"] = true;
			}

			node["signature"] = rules("functionSignature");
			node["body"] = rules("functionBody");

			if(some(node.get<NodeArray&>("modifiers"), [](auto& v) { return !set<string> {"private", "protected", "public"}.contains(v); })) {
				report(1, node.get<Node&>("range").get("start"), node.get("type"), "Wrong modifier(s).");
			}
			if(node.empty("signature")) {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No signature.");
			} else
			if(!node.get<Node&>("signature").empty("returnType")) {
				report(1, node.get<Node&>("range").get("start"), node.get("type"), "Signature shouldn't have a return type.");
			}
			if(node.empty("body")) {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No body.");
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "inOperator") {
			Node node = {
				{"type", "inOperator"},
				{"range", {
					{"start", position}
				}},
				{"inverted", false},
				{"composite", nullptr}
			};

			if(token().type == "operatorPrefix" && token().value == "!") {
				position++;
				node["inverted"] = true;
			}

			if(token().type != "keywordIn") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			position++;
			node["composite"] = rules("expressionsSequence");

			if(node.empty("composite")) {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "inoutExpression") {
			Node node = {
				{"type", "inoutExpression"},
				{"range", Node {}},
				{"value", nullptr}
			};

			if(token().type != "operatorPrefix" || token().value != "&") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["value"] = rules("postfixExpression");

			if(node.empty("value")) {
				position--;

				return nullptr;
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "inoutType") {
			Node node = {
				{"type", "inoutType"},
				{"range", Node {}},
				{"value", nullptr}
			};

			if(token().type != "keywordInout") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["value"] = rules("unionType");
			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "integerLiteral") {
			Node node = {
				{"type", "integerLiteral"},
				{"range", Node {}},
				{"value", nullptr}
			};

			if(token().type != "numberInteger") {
				return nullptr;
			}

			node["value"] = token().value;
			node.get<Node&>("range")["start"] =
			node.get<Node&>("range")["end"] = position++;

			return node;
		} else
		if(type == "intersectionType") {
			Node node = {
				{"type", "intersectionType"},
				{"range", {
					{"start", position}
				}},
				{"subtypes", helpers_sequentialNodes(
					{"postfixType"},
					[this]() { return token().type.starts_with("operator") && token().value == "&"; }
				)}
			};

			NodeArray& subtypes = node.get("subtypes");

			if(subtypes.empty()) {
				return nullptr;
			}
			if(subtypes.size() == 1) {
				return subtypes.at(0);
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "isOperator") {
			Node node = {
				{"type", "isOperator"},
				{"range", {
					{"start", position}
				}},
				{"type_", nullptr},
				{"inverted", false}
			};

			if(token().type == "operatorPrefix" && token().value == "!") {
				position++;
				node["inverted"] = true;
			}

			if(token().type != "keywordIs") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			position++;
			node["type_"] = rules("type");

			if(node.empty("type_")) {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "literalExpression") {
			return (
				rules("arrayLiteral") ?:
				rules("booleanLiteral") ?:
				rules("dictionaryLiteral") ?:
				rules("floatLiteral") ?:
				rules("integerLiteral") ?:
				rules("nilLiteral") ?:
				rules("stringLiteral")
			);
		} else
		if(type == "modifiers") {
			NodeArray values;
			int start = position;
			set<string> keywords = {
				"keywordInfix",
				"keywordPostfix",
				"keywordPrefix",

				"keywordPrivate",
				"keywordProtected",
				"keywordPublic",

				"keywordFinal",
				"keywordLazy",
				"keywordStatic",
				"keywordVirtual"
			};

			while(keywords.contains(token().type)) {
				values.push_back(token().value);
				position++;
			}

			set<string> operator_ = {"infix", "postfix", "prefix"},
						access = {"private", "protected", "public"};

			if(
				filter(values, [&](const string& v) { return operator_.contains(v); }).size() > 1 ||
				filter(values, [&](const string& v) { return access.contains(v); }).size() > 1 ||
				contains(values, "final") && contains(values, "virtual")
			) {
				report(1, start, "modifiers", "Mutual exclusion.");
			}

			return values;
		} else
		if(type == "module") {
			Node node = {
				{"type", "module"},
				{"range", {
					{"start", position}
				}},
				{"statements", rules("functionStatements")}
			};

			node.get<Node&>("range")["end"] = !node.get<NodeArray&>("statements").empty() ? position-1 : 0;

			return node;
		} else
		if(type == "namespaceBody") {
			return rules("body", "namespace");
		} else
		if(type == "namespaceDeclaration") {
			bool anonymous = !arguments.empty() && any_cast<bool>(arguments[0]);
			Node node = {
				{"type", string("namespace")+(!anonymous ? "Declaration" : "Expression")},
				{"range", {
					{"start", position}
				}},
				{"modifiers", rules("modifiers")},
				{"identifier", nullptr},
				{"body", nullptr}
			};

			if(token().type != "keywordNamespace") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			position++;
			node["identifier"] = rules("identifier");

			if(anonymous) {
				if(!node.get<NodeArray&>("modifiers").empty() || !node.empty("identifier")) {
					position = node.get<Node&>("range").get("start");

					return nullptr;
				}

				node.remove("modifiers");
				node.remove("identifier");
			} else
			if(node.empty("identifier")) {
				report(2, node.get<Node&>("range").get("start"), node.get("type"), "No identifier.");
			}

			node["body"] = rules("namespaceBody");

			if(!node.empty("modifiers") && some(node.get<NodeArray&>("modifiers"), [](auto& v) { return !set<string> {"private", "protected", "public", "static", "final"}.contains(v); })) {
				report(1, node.get<Node&>("range").get("start"), node.get("type"), "Wrong modifier(s).");
			}
			if(node.empty("body")) {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No body.");
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "namespaceExpression") {
			return rules("namespaceDeclaration", true);
		} else
		if(type == "namespaceStatements") {
			return rules("statements", vector<string> {"declaration"});
		} else
		if(type == "nillableExpression") {
			NodeSP node_ = any_cast<NodeSP>(arguments[0]);
			Node node = {
				{"type", "nillableExpression"},
				{"range", {
					{"start", node_->get<Node&>("range").get("start")}
				}},
				{"value", node_}
			};

			if(!set<string> {"operatorInfix", "operatorPostfix"}.contains(token().type) || token().value != "?") {
				return nullptr;
			}

			node.get<Node&>("range")["end"] = position++;

			return node;
		} else
		if(type == "nillableType") {
			NodeSP node_ = any_cast<NodeSP>(arguments[0]);
			Node node = {
				{"type", "nillableType"},
				{"range", {
					{"start", node_->get<Node&>("range").get("start")}
				}},
				{"value", node_}
			};

			if(token().type != "operatorPostfix" || token().value != "?") {
				return nullptr;
			}

			node.get<Node&>("range")["end"] = position++;

			return node;
		} else
		if(type == "nilLiteral") {
			Node node = {
				{"type", "nilLiteral"},
				{"range", Node {}}
			};

			if(token().type != "keywordNil") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] =
			node.get<Node&>("range")["end"] = position++;

			return node;
		} else
		if(type == "observerDeclaration") {
			Node node = {
				{"type", "observerDeclaration"},
				{"range", {
					{"start", position}
				}},
				{"identifier", rules("identifier")},
				{"body", nullptr}
			};

			NodeSP identifier = node.get("identifier");

			if(!set<string> {
				"willGet",
				"get",
				"didGet",
				"willSet",
				"set",
				"didSet",
				"willDelete",
				"delete",
				"didDelete"
			}.contains(identifier ? identifier->get("value") : "")) {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			node["body"] = rules("functionBody");

			if(node.empty("body")) {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No body.");
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "observersBody") {
			bool strict = !arguments.empty() && any_cast<bool>(arguments[0]);
			NodeSP node = rules("body", "observers");

			if(node && strict && !some(node->get<NodeArray&>("statements"), [](const Node& v) { return v.get("type") != "unsupported"; })) {
				position = node->get<Node&>("range").get("start");

				return nullptr;
			}

			return node;
		} else
		if(type == "observersStatements") {
			return rules("statements", vector<string> {"observerDeclaration"});
		} else
		if(type == "operator") {
			Node node = {
				{"type", "operator"},
				{"range", Node {}},
				{"value", nullptr}
			};

			if(!token().type.starts_with("operator")) {
				return nullptr;
			}

			node["value"] = token().value;
			node.get<Node&>("range")["start"] =
			node.get<Node&>("range")["end"] = position++;

			return node;
		} else
		if(type == "operatorBody") {
			return rules("body", "operator");
		} else
		if(type == "operatorDeclaration") {
			Node node = {
				{"type", "operatorDeclaration"},
				{"range", {
					{"start", position}
				}},
				{"modifiers", rules("modifiers")},
				{"operator", nullptr},
				{"body", nullptr}
			};

			if(token().type != "keywordOperator") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			position++;
			node["operator"] = rules("operator");
			node["body"] = rules("operatorBody");

			if(!some(node.get<NodeArray&>("modifiers"), [](auto& v) { return set<string> {"infix", "postfix", "prefix"}.contains(v); })) {
				report(2, node.get<Node&>("range").get("start"), node.get("type"), "Should have specific modifier (infix, postfix, prefix).");
			}
			if(node.empty("operator")) {
				report(2, node.get<Node&>("range").get("start"), node.get("type"), "No operator.");
			}
			if(node.empty("body")) {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No body.");
			} else {
				string type = node.get("type");

				for(const Node& entry : filter(node.get<Node&>("body").get<NodeArray&>("statements"), [](const Node& v) { return v.get("type") == "entry"; })) {
					Node& key = entry.get("key"),
						  value = entry.get("value"),
						  entryRange = entry.get("range");
					int start = entryRange.get("start"),
						end = entryRange.get("end");

					if(key.get("type") != "identifier" || !set<string> {"associativity", "precedence"}.contains(key.get("value"))) {
						report(1, start, type, "Should have only identifiers as keys (associativity, precedence).");
					} else {
						if(key.get("value") == "associativity") {
							if(value.get("type") != "identifier") {
								report(2, start, type, "Associativity value should be identifier.");
							}
							if(!set<string> {"left", "right", "none"}.contains(value.get("value"))) {
								report(2, start, type, "Associativity accepts only one of following values: left, right, none.");
							}
						}
						if(key.get("value") == "precedence" && value.get("type") != "integerLiteral") {
							report(2, start, type, "Precedence value should be integer.");
						}
					}
				}
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "operatorStatements") {
			return rules("statements", vector<string> {"entry"});
		} else
		if(type == "parameter") {
			Node node = {
				{"type", "parameter"},
				{"range", {
					{"start", position}
				}},
				{"label", rules("identifier")},
				{"identifier", nullptr},
				{"type_", nullptr},
				{"value", nullptr}
			};

			if(node.empty("label")) {
				return nullptr;
			}

			node["identifier"] = rules("identifier");

			if(node.empty("identifier")) {
				node["identifier"] = node.get("label");
				node["label"] = nullptr;
			}

			node["type_"] = rules("typeClause");
			node["value"] = rules("initializerClause");
			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "parenthesizedExpression") {
			Node node = {
				{"type", "parenthesizedExpression"},
				{"range", Node {}},
				{"value", nullptr}
			};

			if(token().type != "parenthesisOpen") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["value"] = helpers_skippableNode(
				"expressionsSequence",
				[this]() { return token().type == "parenthesisOpen"; },
				[this]() { return token().type == "parenthesisClosed"; }
			);

			if(token().type == "parenthesisClosed") {
				node.get<Node&>("range")["end"] = position++;
			} else
			if(tokensEnd()) {
				node.get<Node&>("range")["end"] = position-1;

				report(1, node.get<Node&>("range").get("start"), node.get("type"), "Node doesn't have the closing parenthesis and was decided to be autoclosed at the end of stream.");
			} else {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			return node;
		} else
		if(type == "parenthesizedType") {
			Node node = {
				{"type", "parenthesizedType"},
				{"range", Node {}},
				{"value", nullptr}
			};

			if(token().type != "parenthesisOpen") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["value"] = rules("type");

			if(token().type != "parenthesisClosed") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			node.get<Node&>("range")["end"] = position++;

			return node;
		} else
		if(type == "postfixExpression") {
			Node node = {
				{"type", "postfixExpression"},
				{"range", {
					{"start", position}
				}},
				{"value", rules("primaryExpression")},
				{"operator", nullptr}
			};

			if(node.empty("value")) {
				return nullptr;
			}

			while(!tokensEnd()) {
				NodeSP value = node.get("value"),
					   node_ =
					rules("callExpression", value) ?:
					rules("chainExpression", value) ?:
					rules("defaultExpression", value) ?:
					rules("nillableExpression", value) ?:
					rules("subscriptExpression", value);

				if(!node_) {
					break;
				}

				node["value"] = node_;
			}

			node["operator"] = rules("postfixOperator");

			if(node.empty("operator")) {
				return node.get("value");
			}

			node.get<Node&>("range")["end"] = node.get<Node&>("operator").get<Node&>("range").get("end");

			return node;
		} else
		if(type == "postfixOperator") {
			Node node = {
				{"type", "postfixOperator"},
				{"range", Node {}},
				{"value", nullptr}
			};

			set<string> exceptions = {",", ":"};  // Enclosing nodes can use trailing operators from the list

			if(token().type != "operatorPostfix" || exceptions.contains(token().value)) {
				return nullptr;
			}

			node["value"] = token().value;
			node.get<Node&>("range")["start"] =
			node.get<Node&>("range")["end"] = position++;

			return node;
		} else
		if(type == "postfixType") {
			NodeSP node = rules("primaryType");

			if(!node) {
				return nullptr;
			}

			while(!tokensEnd()) {
				NodeSP node_ =
					rules("defaultType", node) ?:
					rules("nillableType", node);

				if(!node_) {
					break;
				}

				node = node_;
			}

			return node;
		} else
		if(type == "predefinedType") {
			Node node = {
				{"type", "predefinedType"},
				{"range", Node {}},
				{"value", nullptr}
			};

			if(!set<string> {
				"keywordUnderscore",
				"keywordVoid",

				"keywordAny",
				"keywordBool",
				"keywordDict",
				"keywordFloat",
				"keywordInt",
				"keywordString",
				"keywordType",

				"keywordCapitalAny",
				"keywordCapitalClass",
				"keywordCapitalEnumeration",
				"keywordCapitalFunction",
				"keywordCapitalNamespace",
				"keywordCapitalObject",
				"keywordCapitalProtocol",
				"keywordCapitalStructure"
			}.contains(token().type)) {
				return nullptr;
			}

			node["value"] = token().value;
			node.get<Node&>("range")["start"] =
			node.get<Node&>("range")["end"] = position++;

			return node;
		} else
		if(type == "prefixExpression") {
			Node node = {
				{"type", "prefixExpression"},
				{"range", {
					{"start", position}
				}},
				{"operator", rules("prefixOperator")},
				{"value", nullptr}
			};

			node["value"] = rules("postfixExpression");

			if(node.empty("value")) {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}
			if(node.empty("operator")) {
				return node.get("value");
			}

			node.get<Node&>("range")["end"] = node.get<Node&>("value").get<Node&>("range").get("end");

			return node;
		} else
		if(type == "prefixOperator") {
			Node node = {
				{"type", "prefixOperator"},
				{"range", Node {}},
				{"value", nullptr}
			};

			set<string> exceptions = {"&", "."};  // primaryExpressions can start with operators from the list

			if(token().type != "operatorPrefix" || exceptions.contains(token().value)) {
				return nullptr;
			}

			node["value"] = token().value;
			node.get<Node&>("range")["start"] =
			node.get<Node&>("range")["end"] = position++;

			return node;
		} else
		if(type == "primaryExpression") {
			return (
				rules("classExpression") ?:
				rules("closureExpression") ?:
				rules("enumerationExpression") ?:
				rules("functionExpression") ?:
				rules("identifier") ?:
				rules("implicitChainExpression") ?:
				rules("inoutExpression") ?:
				rules("literalExpression") ?:
				rules("namespaceExpression") ?:
				rules("parenthesizedExpression") ?:
				rules("protocolExpression") ?:
				rules("structureExpression") ?:
				rules("typeExpression")
			);
		} else
		if(type == "primaryType") {
			return (
				rules("arrayType") ?:
				rules("dictionaryType") ?:
				rules("functionType") ?:
				rules("parenthesizedType") ?:
				rules("predefinedType") ?:
				rules("protocolType") ?:
				rules("typeIdentifier")
			);
		} else
		if(type == "protocolBody") {
			return rules("body", "protocol");
		} else
		if(type == "protocolDeclaration") {
			bool anonymous = !arguments.empty() && any_cast<bool>(arguments[0]);
			Node node = {
				{"type", string("protocol")+(anonymous ? "Expression" : "Declaration")},
				{"range", {
					{"start", position}
				}},
				{"modifiers", rules("modifiers")},
				{"identifier", nullptr},
				{"inheritedTypes", nullptr},
				{"body", nullptr}
			};

			if(token().type != "keywordProtocol") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			position++;
			node["identifier"] = rules("identifier");

			if(anonymous) {
				if(!node.get<NodeArray&>("modifiers").empty() || node.empty("identifier")) {
					position = node.get<Node&>("range").get("start");

					return nullptr;
				}

				node.remove("modifiers");
				node.remove("identifier");
			} else
			if(node.empty("identifier")) {
				report(2, node.get<Node&>("range").get("start"), node.get("type"), "No identifier.");
			}

			node["inheritedTypes"] = rules("inheritedTypesClause");
			node["body"] = rules("protocolBody");

			if(!node.empty("modifiers") && some(node.get<NodeArray&>("modifiers"), [](auto& v) { return !set<string>{"private", "protected", "public", "static", "final"}.contains(v); })) {
				report(1, node.get<Node&>("range").get("start"), node.get("type"), "Wrong modifier(s).");
			}
			if(node.empty("body")) {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No body.");
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "protocolExpression") {
			return rules("protocolDeclaration", true);
		} else
		if(type == "protocolStatements") {
			return rules("statements", vector<string> {
				"classDeclaration",
				"enumerationDeclaration",
				"functionDeclaration",
				"namespaceDeclaration",
				"protocolDeclaration",
				"structureDeclaration",
				"variableDeclaration"
			});
		} else
		if(type == "protocolType") {
			Node node = {
				{"type", "protocolType"},
				{"range", {
					{"start", position}
				}},
				{"body", rules("protocolBody")}
			};

			if(node.empty("body")) {
				return nullptr;
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "returnStatement") {
			Node node = {
				{"type", "returnStatement"},
				{"range", Node {}},
				{"value", nullptr}
			};

			if(token().type != "keywordReturn") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["value"] = rules("expressionsSequence");
			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "statements") {
			vector<string> types;

			if(arguments[0].type() == typeid(vector<string>)) {
				types = any_cast<vector<string>>(arguments[0]);
			}
			if(arguments[0].type() == typeid(NodeValue)) {
				NodeArray& types_ = any_cast<NodeValue>(arguments[0]);

				for(const string& type : types_) {
					types.push_back(type);
				}
			}

			return helpers_skippableNodes(
				types,
				[this]() { return token().type == "braceOpen"; },
				[this]() { return token().type == "braceClosed"; },
				[this]() { return token().type == "delimiter"; },
				true
			);
		} else
		if(type == "stringExpression") {
			Node node = {
				{"type", "stringExpression"},
				{"range", Node {}},
				{"value", nullptr}
			};

			if(token().type != "stringExpressionOpen") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["value"] = helpers_skippableNode(
				"expressionsSequence",
				[this]() { return token().type == "stringExpressionOpen"; },
				[this]() { return token().type == "stringExpressionClosed"; }
			);

			if(token().type == "stringExpressionClosed") {
				node.get<Node&>("range")["end"] = position++;
			} else
			if(tokensEnd()) {
				node.get<Node&>("range")["end"] = position-1;

				report(1, node.get<Node&>("range").get("start"), node.get("type"), "Node doesn't have the closing parenthesis and was decided to be autoclosed at the end of stream.");
			} else {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			return node;
		} else
		if(type == "stringLiteral") {
			Node node = {
				{"type", "stringLiteral"},
				{"range", Node {}},
				{"segments", NodeArray {}}
			};

			if(token().type != "stringOpen") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["segments"] = helpers_skippableNodes(
				{"stringSegment", "stringExpression"},
				[this]() { return token().type == "stringOpen"; },
				[this]() { return token().type == "stringClosed"; }
			);

			if(token().type == "stringClosed") {
				node.get<Node&>("range")["end"] = position++;
			} else {
				node.get<Node&>("range")["end"] = position-1;

				report(1, node.get<Node&>("range").get("start"), node.get("type"), "Node doesn't have the closing apostrophe and decided to be autoclosed at the end of stream.");
			}

			return node;
		} else
		if(type == "stringSegment") {
			Node node = {
				{"type", "stringSegment"},
				{"range", Node {}},
				{"value", nullptr}
			};

			if(token().type != "stringSegment") {
				return nullptr;
			}

			node["value"] = token().value;
			node.get<Node&>("range")["start"] =
			node.get<Node&>("range")["end"] = position++;

			return node;
		} else
		if(type == "structureBody") {
			return rules("body", "structure");
		} else
		if(type == "structureDeclaration") {
			bool anonymous = !arguments.empty() && any_cast<bool>(arguments[0]);
			Node node = {
				{"type", string("structure")+(!anonymous ? "Declaration" : "Expression")},
				{"range", {
					{"start", position}
				}},
				{"modifiers", rules("modifiers")},
				{"identifier", nullptr},
				{"genericParameters", nullptr},
				{"inheritedTypes", nullptr},
				{"body", nullptr}
			};

			if(token().type != "keywordStruct") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			position++;
			node["identifier"] = rules("identifier");

			if(anonymous) {
				if(!node.get<NodeArray&>("modifiers").empty() || !node.empty("identifier")) {
					position = node.get<Node&>("range").get("start");

					return nullptr;
				}

				node.remove("modifiers");
				node.remove("identifier");
			} else
			if(node.empty("identifier")) {
				report(2, node.get<Node&>("range").get("start"), node.get("type"), "No identifier.");
			}

			node["genericParameters"] = rules("genericParametersClause");
			node["inheritedTypes"] = rules("inheritedTypesClause");
			node["body"] = rules("structureBody");

			if(!node.empty("modifiers") && some(node.get<NodeArray&>("modifiers"), [](auto& v) { return !set<string> {"private", "protected", "public", "static", "final"}.contains(v); })) {
				report(1, node.get<Node&>("range").get("start"), node.get("type"), "Wrong modifier(s).");
			}
			if(node.empty("body")) {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No body.");
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "structureExpression") {
			return rules("structureDeclaration", true);
		} else
		if(type == "structureStatements") {
			return rules("statements", vector<string> {
				"chainDeclaration",
				"classDeclaration",
				"deinitializerDeclaration",
				"enumerationDeclaration",
				"functionDeclaration",
				"initializerDeclaration",
				"namespaceDeclaration",
				"protocolDeclaration",
				"structureDeclaration",
				"subscriptDeclaration",
				"variableDeclaration"
			});
		} else
		if(type == "subscriptDeclaration") {
			Node node = {
				{"type", "subscriptDeclaration"},
				{"range", {
					{"start", position}
				}},
				{"modifiers", rules("modifiers")},
				{"signature", nullptr},
				{"body", nullptr}
			};

			if(token().type != "identifier" || token().value != "subscript") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["signature"] = rules("functionSignature");
			node["body"] = rules("observersBody", true) ?: rules("functionBody");

			if(some(node.get<NodeArray&>("modifiers"), [](auto& v) { return !set<string> {"private", "protected", "public", "static"}.contains(v); })) {
				report(1, node.get<Node&>("range").get("start"), node.get("type"), "Wrong modifier(s).");
			}
			if(node.empty("signature")) {
				report(2, node.get<Node&>("range").get("start"), node.get("type"), "No signature.");
			}
			if(node.empty("body")) {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No body.");
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "subscriptExpression") {
			NodeSP node_ = any_cast<NodeSP>(arguments[0]);
			Node node = {
				{"type", "subscriptExpression"},
				{"range", {
					{"start", node_->get<Node&>("range").get("start")}
				}},
				{"composite", node_},
				{"genericArguments", NodeArray {}},
				{"arguments", NodeArray {}},
				{"closure", nullptr}
			};

			if(token().type.starts_with("operator") && token().value == "<") {
				position++;
				node["genericArguments"] = helpers_sequentialNodes(
					{"type"},
					[this]() { return token().type.starts_with("operator") && token().value == ","; }
				);

				if(token().type.starts_with("operator") && token().value == ">") {
					position++;
				} else {
					position = node_->get<Node&>("range").get<int>("end")+1;
				}
			}

			node.get<Node&>("range")["end"] = position-1;

			if(token().type == "bracketOpen") {
				position++;
				node["arguments"] = helpers_skippableNodes(
					{"argument"},
					[this]() { return token().type == "bracketOpen"; },
					[this]() { return token().type == "bracketClosed"; },
					[this]() { return token().type.starts_with("operator") && token().value == ","; }
				);

				if(token().type == "bracketClosed") {
					position++;
				} else {
					node.get<Node&>("range")["end"] = position-1;

					report(1, node.get<Node&>("range").get("start"), node.get("type"), "Node doesn't have the closing bracket and was decided to be autoclosed at the end of stream.");

					return node;
				}
			}

			node["closure"] = rules("closureExpression");

			if(node.empty("closure") && node.get<Node&>("range").get("end") == position-1) {
				position = node_->get<Node&>("range").get<int>("end")+1;

				return nullptr;
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "throwStatement") {
			Node node = {
				{"type", "throwStatement"},
				{"range", Node {}},
				{"value", nullptr}
			};

			if(token().type != "keywordThrow") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["value"] = rules("expressionsSequence");
			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "tryExpression") {
			Node node = {
				{"type", "tryExpression"},
				{"range", Node {}},
				{"nillable", false},
				{"value", nullptr}
			};

			if(token().type != "keywordTry") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;

			if(token().type == "operatorPostfix" && token().value == "?") {
				position++;
				node["nillable"] = true;
			}

			node["value"] = rules("expression");

			if(node.empty("value")) {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "type") {
			return (
				rules("variadicType") ?:
				rules("inoutType") ?:
				rules("unionType")
			);
		} else
		if(type == "typeClause") {
			NodeSP node;
			int start = position;

			if(!token().type.starts_with("operator") || token().value != ":") {
				return nullptr;
			}

			position++;
			node = rules("type");

			if(!node) {
				report(0, start, "typeClause", "No value.");
			}

			return node;
		} else
		if(type == "typeExpression") {
			Node node = {
				{"type", "typeExpression"},
				{"range", {
					{"start", position}
				}},
				{"type_", nullptr}
			};

			if(token().type != "keywordType") {
				return nullptr;
			}

			position++;
			node["type_"] = rules("type");

			if(node.empty("type_")) {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "typeIdentifier") {
			Node node = {
				{"type", "typeIdentifier"},
				{"range", {
					{"start", position}
				}},
				{"identifier",
					rules("identifier") ?:
					rules("implicitChainIdentifier")
				},
				{"genericArguments", NodeArray {}}
			};

			if(node.empty("identifier")) {
				return nullptr;
			}

			while(!tokensEnd()) {
				NodeSP node_ = rules("chainIdentifier", node.get<NodeSP>("identifier"));

				if(!node_) {
					break;
				}

				node["identifier"] = node_;
			}

			node.get<Node&>("range")["end"] = position-1;

			if(!token().type.starts_with("operator") || token().value != "<") {
				return node;
			}

			position++;
			node["genericArguments"] = helpers_sequentialNodes(
				{"type"},
				[this]() { return token().type.starts_with("operator") && token().value == ","; }
			);

			if(!token().type.starts_with("operator") || token().value != ">") {
				position = node.get<Node&>("range").get<int>("end")+1;
				node["genericArguments"] = NodeArray();
			} else {
				node.get<Node&>("range")["end"] = position++;
			}

			return node;
		} else
		if(type == "unionType") {
			Node node = {
				{"type", "unionType"},
				{"range", {
					{"start", position}
				}},
				{"subtypes", helpers_sequentialNodes(
					{"intersectionType"},
					[this]() { return token().type.starts_with("operator") && token().value == "|"; }
				)}
			};

			NodeArray& subtypes = node.get("subtypes");

			if(subtypes.empty()) {
				return nullptr;
			}
			if(subtypes.size() == 1) {
				return subtypes.at(0);
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "variableDeclaration") {
			Node node = {
				{"type", "variableDeclaration"},
				{"range", {
					{"start", position}
				}},
				{"modifiers", rules("modifiers")},
				{"declarators", NodeArray {}}
			};

			if(token().type != "keywordVar") {
				position = node.get<Node&>("range").get("start");

				return nullptr;
			}

			position++;
			node["declarators"] = helpers_sequentialNodes(
				{"declarator"},
				[this]() { return token().type.starts_with("operator") && token().value == ","; }
			);

			if(some(node.get<NodeArray&>("modifiers"), [](auto& v) { return !set<string> {"private", "protected", "public", "final", "lazy", "static", "virtual"}.contains(v); })) {
				report(1, node.get<Node&>("range").get("start"), node.get("type"), "Wrong modifier(s).");
			}
			if(node.get<NodeArray&>("declarators").empty()) {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No declarator(s).");
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "variadicType") {
			Node node = {
				{"type", "variadicType"},
				{"range", {
					{"start", position}
				}},
				{"value", rules("inoutType") ?: rules("unionType")}
			};

			if(!token().type.starts_with("operator") || token().value != "...") {
				return node.get("value");
			}

			position++;
			node.get<Node&>("range")["end"] = position-1;

			return node;
		} else
		if(type == "whileStatement") {
			Node node = {
				{"type", "whileStatement"},
				{"range", Node {}},
				{"condition", nullptr},
				{"value", nullptr}
			};

			if(token().type != "keywordWhile") {
				return nullptr;
			}

			node.get<Node&>("range")["start"] = position++;
			node["condition"] = rules("expressionsSequence");

			helpers_bodyTrailedValue(node, "condition", "value");

			if(node.empty("condition")) {
				report(2, node.get<Node&>("range").get("start"), node.get("type"), "No condition.");
			}
			if(node.empty("value")) {
				report(0, node.get<Node&>("range").get("start"), node.get("type"), "No value.");
			}

			node.get<Node&>("range")["end"] = position-1;

			return node;
		}

		return nullptr;
	}

	/**
	 * Trying to set the node's value and body basing on its value.
	 *
	 * Value that have unsignatured trailing closure will be divided to:
	 * - Value - a left-hand-side (exported, if closure goes right after it).
	 * - Body - the closure.
	 *
	 * Closures, nested into expressionsSequence, prefixExpression and inOperator, are also supported.
	 *
	 * Useful for unwrapping trailing bodies and completing preconditional statements, such as if or for.
	 */
	void helpers_bodyTrailedValue(Node& node, const string& valueKey, const string& bodyKey, bool statementTrailed = true, function<NodeSP()> body = nullptr) {
		if(!body) {
			body = [this]() { return rules("functionBody"); };
		}

		node[bodyKey] = body();

		if(node.empty(valueKey) || !node.empty(bodyKey)) {
			return;
		}

		int end = -1;

		function<void(Node&)> parse = [&](Node& n) {
			if(&n == &node) {
				parse(n.get(valueKey));

				if(end != -1) {
					n[bodyKey] = body();
				}
			} else
			if(n.get("type") == "expressionsSequence") {
				parse(n.get<NodeArray&>("values").back());
			} else
			if(n.get("type") == "prefixExpression") {
				parse(n.get("value"));
			} else
			if(n.get("type") == "inOperator") {
				parse(n.get("composite"));
			} else
			if(!n.empty("closure") && n.get<Node&>("closure").empty("signature")) {
				position = n.get<Node&>("closure").get<Node&>("range").get("start");
				n["closure"] = nullptr;
				end = position-1;

				NodeSP lhs = n.get("callee") ?: n.get("composite");
				bool exportable = lhs->get<Node&>("range").get("end") == end;

				if(exportable) {
					n.clear();

					for(const auto& [k, v] : *lhs) {
						n[k] = v;
					}
				}
			}

			if(end != -1) {
				n.get<Node&>("range")["end"] = end;
			}
		};

		parse(node);

		if(statementTrailed && node.empty(bodyKey)) {
			node[bodyKey] = rules("functionStatement");
		}
	}

	/**
	 * Returns a list of nodes of the types in sequential order like [1, 2, 3, 1...].
	 *
	 * Additionally a separator between the nodes can be set.
	 * Also types can be marked subsequential and therefore have no impact on offset in iterations.
	 *
	 * Stops when an unsupported type occurs.
	 *
	 * Useful for precise sequences lookup.
	 */
	NodeArray helpers_sequentialNodes(const vector<string>& types, function<bool()> separating = nullptr, optional<set<string>> subsequentialTypes = nullopt) {
		NodeArray nodes;
		int offset = 0;

		while(!tokensEnd()) {
			string type = types[offset%types.size()];
			NodeSP node = rules(type);

			if(!node) {
				break;
			}

			nodes.push_back(node);

			if(!subsequentialTypes || !subsequentialTypes->contains(node->get("type"))) {
				offset++;
			}

			if(separating && offset > 0) {
				if(separating()) {
					position++;
				} else {
					break;
				}
			}
		}

		return nodes;
	}

	/**
	 * Returns a node of the type or the "unsupported" node if not closed immediately.
	 *
	 * Stops if node found, at 0 (relatively to 1 at start) scope level or at the .tokensEnd.
	 *
	 * Warns about "unsupported" nodes.
	 *
	 * Useful for imprecise single enclosed(ing) nodes lookup.
	 */
	NodeSP helpers_skippableNode(const string& type, function<bool()> opening, function<bool()> closing) {
		NodeSP node = rules(type);
		int scopeLevel = 1;

		if(node || closing() || tokensEnd()) {
			return node;
		}

		node = SP<Node>({
			{"type", "unsupported"},
			{"range", {
				{"start", position},
				{"end", position}
			}},
			{"tokens", NodeArray {}}
		});

		while(!tokensEnd()) {
			scopeLevel += opening();
			scopeLevel -= closing();

			if(scopeLevel == 0) {
				break;
			}

			node->get<NodeArray&>("tokens").push_back(NodeParser(Lexer::to_string(token())).parse());
			node->get<Node&>("range")["end"] = position++;
		}

		Node& nodeRange = node->get("range");
		string type_ = node->get("type");
		int start = nodeRange.get("start"),
			end = nodeRange.get("end");
		bool range = start != end;
		string message = range ? "range of tokens ["+to_string(start)+":"+to_string(end)+"]" : "token ["+to_string(start)+"]";

		report(1, start, type_, "At "+message+".");

		return node;
	}

	/**
	 * Returns a list of nodes of the types, including the "unsupported" nodes if any occur.
	 *
	 * Additionally a (optional) separator between the nodes can be set.
	 *
	 * Stops at 0 (relatively to 1 at start) scope level or at the .tokensEnd.
	 *
	 * Warns about invalid separators and "unsupported" nodes.
	 *
	 * Useful for imprecise multiple enclosed(ing) nodes lookup.
	 */
	NodeArray helpers_skippableNodes(const vector<string>& types, function<bool()> opening, function<bool()> closing, function<bool()> separating = nullptr, bool optionalSeparator = false) {
		NodeArray nodes;
		int scopeLevel = 1;

		while(!tokensEnd()) {
			NodeSP node;

			if(!separating || nodes.empty() || nodes.back().get<NodeSP>()->get("type") == "separator" || optionalSeparator) {
				for(const string& type : types) {
					node = rules(type);

					if(node) {
						nodes.push_back(node);

						break;
					}
				}
			}

			if(closing() && scopeLevel == 1 || tokensEnd()) {
				break;
			}

			if(separating && separating()) {
				node = !nodes.empty() ? nodes.back().get<NodeSP>() : nullptr;

				if(node) {
					if(node->get("type") != "separator") {
						node = SP<Node>({
							{"type", "separator"},
							{"range", {
								{"start", position},
								{"end", position}
							}}
						});

						nodes.push_back(node);
					} else {
						node->get<Node&>("range")["end"] = position;
					}
				}

				position++;
			}

			if(node) {
				continue;
			}

			node = !nodes.empty() ? nodes.back().get<NodeSP>() : nullptr;

			if(!node || node->get("type") != "unsupported") {
				node = SP<Node>({
					{"type", "unsupported"},
					{"range", {
						{"start", position},
						{"end", position}
					}},
					{"tokens", NodeArray {}}
				});
			}

			scopeLevel += opening();
			scopeLevel -= closing();

			if(scopeLevel == 0) {
				break;
			}

			node->get<NodeArray&>("tokens").push_back(NodeParser(Lexer::to_string(token())).parse());
			node->get<Node&>("range")["end"] = position++;

			if(node != (!nodes.empty() ? nodes.back().get<NodeSP>() : nullptr)) {
				nodes.push_back(node);
			}
		}

		for(int key = 0; key < nodes.size(); key++) {
			NodeSP node = nodes[key];
			string type = node->get("type");
			NodeSP nodeRange = node->get("range");
			int start = nodeRange->get("start"),
				end = nodeRange->get("end");
			bool range = start != end;

			if(type == "separator") {
				if(key == 0) {
					report(0, start, type, "Met before any supported type at token ["+to_string(start)+"].");
				}
				if(range) {
					report(0, start, type, "Sequence at range of tokens ["+to_string(start)+":"+to_string(end)+"].");
				}
				if(key == nodes.size()-1) {
					report(0, start, type, "Excess at token ["+to_string(start)+"].");
				}
			}
			if(type == "unsupported") {
				string message = range ? "range of tokens ["+to_string(start)+":"+to_string(end)+"]" : "token ["+to_string(start)+"]";

				report(1, start, type, "At "+message+".");
			}
		}

		erase_if(nodes, [](const Node& v) { return v.get("type") == "separator"; });

		return nodes;
	}

	struct Position {
	private:
		Parser& self;
		int value;

		void update(int previous) {
			if(value < previous) {  // Rollback global changes
				Interface::sendToClients({
					{"type", "notification"},
					{"source", "parser"},
					{"action", "removeAfterPosition"},
					{"position", value-1}
				});
			}
		}

	public:
		Position(Parser& s, int v = 0) : self(s), value(v) {}

		operator NodeValue() const {
			return value;
		}

		operator int() const {
			return value;
		}

		template <typename T>
		Position& operator=(const T& v) {
			int previous = value; value = v; update(previous); return *this;
		}

		Position& operator+=(int v) {
			value += v; update(value-v); return *this;
		}

		Position& operator-=(int v) {
			value -= v; update(value+v); return *this;
		}

		Position& operator++() {  // Prefix
			value++; /*update(value-1);*/ return *this;
		}

		Position& operator--() {  // Prefix
			value--; update(value+1); return *this;
		}

		Position operator++(int) {  // Postfix
			Position previous = *this; value++; /*update(previous);*/ return previous;
		}

		Position operator--(int) {  // Postfix
			Position previous = *this; value--; update(previous); return previous;
		}
	};

	Position position = Position(*this, 0);

	Token& token() {
		static Token dummy;

		return tokens.size() > position
			 ? tokens[position]
			 : dummy = Token();
	}

	inline bool tokensEnd() {
		return position == tokens.size();
	}

	void report(int level, int position, const string& type, string string) {
		Location& location = tokens[position].location;

		string = type+" -> "+string;

		Interface::sendToClients({
			{"type", "notification"},
			{"source", "parser"},
			{"action", "add"},
			{"level", level},
			{"position", position},
			{"location", Node {
				{"line", location.line},
				{"column", location.column}
			}},
			{"string", string}
		});
	}

	NodeSP parse() {
		Interface::sendToClients({
			{"type", "notification"},
			{"source", "parser"},
			{"action", "removeAll"},
			{"moduleID", -1}
		});

		NodeSP tree = rules("module");

		Interface::sendToClients({
			{"type", "notification"},
			{"source", "parser"},
			{"action", "parsed"},
			{"tree", tree}
		});

		return tree;
	}
};