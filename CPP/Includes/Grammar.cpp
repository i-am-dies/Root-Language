#pragma once

#include "Node.cpp"

namespace Grammar {
	struct NodeRule;
	struct TokenRule;
	struct LogicRule;
	struct VariantRule;
	struct SequenceRule;

	using RuleRef = string;
	using Rule = variant<RuleRef,
						 recursive_wrapper<NodeRule>,
						 recursive_wrapper<TokenRule>,
						 recursive_wrapper<LogicRule>,
						 recursive_wrapper<VariantRule>,
						 recursive_wrapper<SequenceRule>>;

	// ----------------------------------------------------------------

	struct Field {
		Field(const char* t, Rule r = "[placeholder]", bool o = false) : title(string(t)), rule(r), optional(o) {}
		Field(const string& t, Rule r = "[placeholder]", bool o = false) : title(t), rule(r), optional(o) {}
		Field(const nullopt_t& t, Rule r = "[placeholder]", bool o = false) : title(t), rule(r), optional(o) {}

		std::optional<string> title;  // Nil for implicit
		Rule rule;
		bool optional = false;

		bool operator==(const Field& f) const = default;
	};

	struct NodeRule {
		NodeRule(initializer_list<Field> f, u8 n = 0, function<void(Node&)> p = nullptr) : fields(f), normalize(n), post(p) {}

		vector<Field> fields;
		u8 normalize = 0;  // 0 - No normalization, 1 - Return nil if node is empty, 2 - Or unwrap its single field
		function<void(Node&)> post;

		bool operator==(const NodeRule& r) const {
			return fields == r.fields &&
				   normalize == r.normalize &&
				   post.target<void(Node&)>() == r.post.target<void(Node&)>();
		}
	};

	struct TokenRule {
		TokenRule() {}
		TokenRule(const string& type) : patterns{type}, regexes{regex(type)} {}
		TokenRule(const nullopt_t& type, const string& value) : patterns{type, value}, regexes{regex(), regex(value)} {}
		TokenRule(const string& type, const string& value) : patterns{type, value}, regexes{regex(type), regex(value)} {}

		optional<string> patterns[2];
		regex regexes[2];

		bool operator==(const TokenRule& r) const {
			return patterns[0] == r.patterns[0] &&
				   patterns[1] == r.patterns[1];
		}
	};

	struct LogicRule {
		Rule rule;
		optional<Rule> optionalizer;

		bool operator==(const LogicRule& r) const = default;
	};

	struct VariantRule : vector<Rule> {};

	struct SequenceRule {
		Rule rule;

		// Ascender and _descender_ can be used to enable the dirt-tolerance:
		// Parser will be aware of the nesting level and have a chance to close the rule before EOF.
		// Unrecognized tokens will be included into the result.
		optional<Rule> ascender,
					   delimiter,
					   descender;

		// Examples: 0/0, 1/1, 0/1, 1/max, 0/max.
		usize range[2] = {0, usize(-1)},
			  innerDelimitRange[2] = {0, usize(-1)},
			  outerDelimitRange[2] = {0, usize(-1)};

		bool delimited = false,  // Include delimiters if any
			 normalize = false;  // Return nil if sequence is empty or unwrap its single value

		bool operator==(const SequenceRule& r) const = default;
	};

	// ----------------------------------------------------------------

	unordered_map<RuleRef, Rule> rules = {
		{"argument", VariantRule({
			NodeRule({
				{"label", "labelClause"},
				{"value", "expressionsSequence", true},
			}),
			NodeRule({
				{"value", "expressionsSequence"}
			})
		})},
		{"arrayLiteral", NodeRule({
			{nullopt, TokenRule("bracketOpen")},
			{nullopt, TokenRule("delimiterImplicit"), true},
			{"values", SequenceRule {
				.rule = "expressionsSequence",
				.delimiter = NodeRule({
					{nullopt, TokenRule("delimiterImplicit"), true},
					{nullopt, TokenRule("operator.*", ",")},
					{nullopt, TokenRule("delimiterImplicit"), true}
				}),
				.innerDelimitRange = {1, usize(-1)}
			}},
			{nullopt, TokenRule("delimiterImplicit"), true},
			{nullopt, TokenRule("bracketClosed|endOfFile")}
		})},
		{"arrayType", NodeRule({
			{nullopt, TokenRule("bracketOpen")},
			{"value", "type"},
			{nullopt, TokenRule("bracketClosed|endOfFile")}
		})},
		{"asExpression", NodeRule({
			{"value", "expression"},
			{nullopt, TokenRule("keywordAs")},
			{"type_", "type"}
		})},
		{"asyncExpression", NodeRule({
			{nullopt, TokenRule("keywordAsync")},
			{"value", "expression"}
		})},
		{"awaitExpression", NodeRule({
			{nullopt, TokenRule("keywordAwait")},
			{"value", "expression"}
		})},
		{"booleanLiteral", NodeRule({
			{"value", TokenRule("keywordFalse|keywordTrue")}
		})},
		{"breakStatement", NodeRule({
			{nullopt, TokenRule("keywordBreak")},
			{"label", "identifier", true}
		})},
		{"callArgumentsClause", NodeRule({
			{nullopt, TokenRule("parenthesisOpen")},
			{"arguments", SequenceRule {
				.rule = "argument",
				.ascender = TokenRule("parenthesisOpen"),
				.delimiter = TokenRule("operator.*", ","),
				.descender = TokenRule("parenthesisClosed"),
				.innerDelimitRange = {1, usize(-1)}
			}},
			{nullopt, TokenRule("parenthesisClosed|endOfFile")}
		}, 2)},
		{"callExpression", VariantRule({
			NodeRule({
				{"callee", "postfixExpression"},
				{"genericArguments", "genericArgumentsClause", true},
				{"arguments", "callArgumentsClause"},
				{"closures", "closuresClause", true}
			}),
			NodeRule({
				{"callee", "postfixExpression"},
				{"genericArguments", "genericArgumentsClause", true},
				{"closures", "closuresClause"}
			})
		})},
		{"caseDeclaration", NodeRule({
			{nullopt, TokenRule("keywordCase")},
			{"identifiers", SequenceRule {
				.rule = "identifier",
				.delimiter = TokenRule("operator.*", ","),
				.innerDelimitRange = {1, usize(-1)}
			}}
		})},
		{"catchClause", NodeRule({
			{nullopt, TokenRule("keywordCatch")},
			{"types", SequenceRule {
				.rule = "type",
				.delimiter = TokenRule("operator.*", ","),
				.innerDelimitRange = {1, usize(-1)}
			}},
			{"body", "functionBody", true},
			{"catch", "catchClause", true}
		})},
		{"chainDeclaration", NodeRule({
			{"modifiers", "modifiers"},
			{nullopt, TokenRule("identifier", "chain")},
			{"body", "observersBody", true}
		})},
		{"chainExpression", NodeRule({
			{"composite", "postfixExpression"},
			{nullopt, TokenRule("operator|operatorInfix", "\\.")},
			{"member", VariantRule({
				"identifier",
				"stringLiteral"
			})}
		})},
		{"chainedIdentifier", NodeRule({
			{"supervalue", VariantRule({
				"chainedIdentifier",
				"identifier"
			})},
			{nullopt, TokenRule("operatorPrefix|operatorInfix", "\\.")},
			{"value", "identifier"}
		})},
		{"classBody", NodeRule({
			{nullopt, TokenRule("braceOpen")},
			{"statements", "classStatements"},
			{nullopt, TokenRule("braceClosed|endOfFile")}
		})},
		{"classDeclaration", NodeRule({
			{"modifiers", "modifiers"},
			{nullopt, TokenRule("keywordClass")},
			{"identifier", "identifier"},
			{"genericParameters", "genericParametersClause", true},
			{"inheritedTypes", "inheritedTypesClause", true},
			{"body", "classBody", true}
		})},
		{"classExpression", NodeRule({
			{nullopt, TokenRule("keywordClass")},
			{"genericParameters", "genericParametersClause", true},
			{"inheritedTypes", "inheritedTypesClause", true},
			{"body", "classBody", true}
		})},
		{"classStatements", SequenceRule {
			.rule = VariantRule({
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
			}),
			.ascender = TokenRule("braceOpen"),
			.delimiter = TokenRule("delimiter.*"),
			.descender = TokenRule("braceClosed")
		}},
		{"closuresClause", SequenceRule({
			.rule = "identifiedClosure",
			.range = {1, usize(-1)}
		})},
		{"closureExpression", NodeRule({
			{nullopt, TokenRule("braceOpen")},
			{"signature", "closureSignature", true},
			{"statements", "functionStatements", true},
			{nullopt, TokenRule("braceClosed|endOfFile")}
		})},
		{"closureSignature", NodeRule({
			{"signature", "functionSignature"},
			{nullopt, TokenRule("keywordIn")}
		}, 2)},
		{"conditionalOperator", NodeRule({
			{nullopt, TokenRule("operator.*", "\\?")},
			{nullopt, TokenRule("delimiterImplicit"), true},
			{"value", "expressionsSequence"},
			{nullopt, TokenRule("delimiterImplicit"), true},
			{nullopt, TokenRule("operator.*", ":")}
		})},
		{"continueStatement", NodeRule({
			{nullopt, TokenRule("keywordContinue")},
			{"label", "identifier", true}
		})},
		{"controlTransferStatement", VariantRule({
			"breakStatement",
			"continueStatement",
			"fallthroughStatement",
			"returnStatement",
			"throwStatement"
		})},
		{"declaration", VariantRule({
			"chainDeclaration",
			"classDeclaration",
			"deinitializerDeclaration",
			"enumerationDeclaration",
			"functionDeclaration",
			"importDeclaration",
			"initializerDeclaration",
			"namespaceDeclaration",
			"operatorDeclaration",
			"protocolDeclaration",
			"structureDeclaration",
			"subscriptDeclaration",
			"variableDeclaration"
		})},
		{"declarator", NodeRule({
			{"identifier", "identifier"},
			{"type_", "typeClause", true},
			{"value", "initializerClause", true},
			{"body", VariantRule({
				"observersBody",
				"functionBody"
			}), true}
		})},
		{"defaultExpression", NodeRule({
			{"value", "postfixExpression"},
			{nullopt, TokenRule("operatorPostfix", "!")}
		})},
		{"defaultType", NodeRule({
			{"value", "postfixType"},
			{nullopt, TokenRule("operatorPostfix", "!")}
		})},
		{"deinitializerDeclaration", NodeRule({
			{"modifiers", "modifiers"},
			{nullopt, TokenRule("identifier", "deinit")},
			{"body", "functionBody", true}
		})},
		{"deleteExpression", NodeRule({
			{nullopt, TokenRule("identifier", "delete")},
			{"value", "expression"}
		})},
		{"dictionaryLiteral", VariantRule({
			NodeRule({
				{nullopt, TokenRule("bracketOpen")},
				{nullopt, TokenRule("delimiterImplicit"), true},
				{"entries", SequenceRule {
					.rule = "entry",
					.delimiter = NodeRule({
						{nullopt, TokenRule("delimiterImplicit"), true},
						{nullopt, TokenRule("operator.*", ",")},
						{nullopt, TokenRule("delimiterImplicit"), true}
					}),
					.innerDelimitRange = {1, usize(-1)}
				}},
				{nullopt, TokenRule("delimiterImplicit"), true},
				{nullopt, TokenRule("bracketClosed|endOfFile")}
			}),
			NodeRule({
				{nullopt, TokenRule("bracketOpen")},
				{nullopt, TokenRule("operator.*", ":")},
				{nullopt, TokenRule("bracketClosed|endOfFile")}
			}),
		})},
		{"dictionaryType", NodeRule({
			{nullopt, TokenRule("bracketOpen")},
			{"key", "type"},
			{nullopt, TokenRule("operator.*", ":")},
			{"value", "type"},
			{nullopt, TokenRule("bracketClosed|endOfFile")}
		})},
		{"doStatement", NodeRule({
			{nullopt, TokenRule("keywordDo")},
			{"body", "functionBody", true},
			{"catch", "catchClause", true}
		})},
		{"elseClause", NodeRule({
			{nullopt, TokenRule("keywordElse")},
			{nullopt, TokenRule("delimiterImplicit"), true},
			{"then", VariantRule({
				"functionBody",
				"expressionsSequence",
				"ifStatement"
			})}
		}, 2)},
		{"entry", NodeRule({
			{"key", "expressionsSequence"},
			{nullopt, TokenRule("operator.*", ":")},
			{"value", "expressionsSequence"}
		})},
		{"enumerationBody", NodeRule({
			{nullopt, TokenRule("braceOpen")},
			{"statements", "enumerationStatements"},
			{nullopt, TokenRule("braceClosed|endOfFile")}
		})},
		{"enumerationDeclaration", NodeRule({
			{"modifiers", "modifiers"},
			{nullopt, TokenRule("keywordEnum")},
			{"identifier", "identifier"},
			{"inheritedTypes", "inheritedTypesClause", true},
			{"body", "enumerationBody", true}
		})},
		{"enumerationExpression", NodeRule({
			{nullopt, TokenRule("keywordEnum")},
			{"inheritedTypes", "inheritedTypesClause", true},
			{"body", "enumerationBody", true}
		})},
		{"enumerationStatements", SequenceRule {
			.rule = VariantRule({
				"caseDeclaration",
				"enumerationDeclaration"
			}),
			.ascender = TokenRule("braceOpen"),
			.delimiter = TokenRule("delimiter.*"),
			.descender = TokenRule("braceClosed")
		}},
		{"expression", VariantRule({
			"asExpression",
			"isExpression",
			"asyncExpression",
			"awaitExpression",
			"deleteExpression",
			"tryExpression",
			"prefixExpression"
		})},
		{"expressionsSequence", VariantRule({
			NodeRule({
				{"values", SequenceRule {
					.rule = "expression",
					.delimiter = "infixExpression",
					.range = {2, usize(-1)},
					.innerDelimitRange = {1, 1},
					.outerDelimitRange = {0, 0},
					.delimited = true
				}}
			}),
			"expression"
		})},
		{"fallthroughStatement", NodeRule({
			{nullopt, TokenRule("keywordFallthrough")},
			{"label", "identifier", true}
		})},
		{"floatLiteral", NodeRule({
			{"value", TokenRule("numberFloat")}
		})},
		{"forStatement", NodeRule({
			{nullopt, TokenRule("keywordFor")},
			{"identifier", "identifier", true},
			{"in", "inClause", true},
			{"where", "whereClause", true},
			{"value", VariantRule({
				"functionBody",
				"expressionsSequence"
			}), true}
		})},
		{"functionBody", NodeRule({
			{nullopt, TokenRule("braceOpen")},
			{"statements", "functionStatements"},
			{nullopt, TokenRule("braceClosed|endOfFile")}
		})},
		{"functionDeclaration", NodeRule({
			{"modifiers", "modifiers"},
			{nullopt, TokenRule("keywordFunc")},
			{"identifier", VariantRule({
				"identifier",
				"operator"
			})},
			{"signature", "functionSignature", true},
			{"body", "functionBody", true}
		})},
		{"functionExpression", NodeRule({
			{nullopt, TokenRule("keywordFunc")},
			{"signature", "functionSignature", true},
			{"body", "functionBody", true}
		})},
		{"functionSignature", NodeRule({
			{"genericParameters", "genericParametersClause", true},
			{"parameters", "parametersClause", true},
			{"awaits", LogicRule {
				.rule = TokenRule("keywordAwaits")
			}, true},
			{"throws", LogicRule {
				.rule = TokenRule("keywordThrows")
			}, true},
			{"returnType", "returnClause", true}
		}, 1)},
		{"functionStatement", VariantRule({
			"declaration",  // Must be parsed before expressions, otherwise composite declarations will be parsed as [anonymous composite, identifier] expressions
			"expressionsSequence",
			"controlTransferStatement",
			"doStatement",
			"ifStatement",
			"loopStatement"
		})},
		{"functionStatements", SequenceRule {
			.rule = "functionStatement",
			.ascender = TokenRule("braceOpen"),
			.delimiter = TokenRule("delimiter.*"),
			.descender = TokenRule("braceClosed")
		}},
		{"functionType", NodeRule({
			{"genericParameterTypes", "genericParametersClause", true},
			{"parameterTypes", "parametersClause"},
			{"awaits", LogicRule {
				.rule = TokenRule("keywordAwaits"),
				.optionalizer = TokenRule("operatorPostfix", "\\?")
			}, true},
			{"throws", LogicRule {
				.rule = TokenRule("keywordThrows"),
				.optionalizer = TokenRule("operatorPostfix", "\\?")
			}, true},
			{"returnType", "returnClause", true}
		})},
		{"genericArgument", "type"},
		{"genericArgumentsClause", NodeRule({
			{nullopt, TokenRule("operator.*", "<")},
			{"genericArguments", SequenceRule {
				.rule = "genericArgument",
				.delimiter = TokenRule("operator.*", ","),
				.innerDelimitRange = {1, usize(-1)}
			}},
			{nullopt, VariantRule({
				TokenRule("operator.*", ">"),
				TokenRule("endOfFile")
			})}
		}, 2)},
		{"genericParameter", NodeRule({
			{"identifier", "identifier"},
			{"type_", "typeClause", true}
		})},
		{"genericParametersClause", NodeRule({
			{nullopt, TokenRule("operator.*", "<")},
			{"genericParameters", SequenceRule {
				.rule = "genericParameter",
				.ascender = TokenRule("operator.*", "<"),
				.delimiter = TokenRule("operator.*", ","),
				.descender = TokenRule("operator.*", ">"),
				.innerDelimitRange = {1, usize(-1)}
			}},
			{nullopt, VariantRule({
				TokenRule("operator.*", ">"),
				TokenRule("endOfFile")
			})}
		}, 2)},
		{"identifiedClosure", NodeRule({
			{"identifier", "identifier"},
			{nullopt, TokenRule("operator.*", ":")},
			{"closure", "closureExpression"}
		})},
		{"identifier", NodeRule({
			{"value", TokenRule("identifier")}
		})},
		{"ifStatement", NodeRule({
			{nullopt, TokenRule("keywordIf")},
			{"condition", "expressionsSequence", true},
			{"then", VariantRule({
				"functionBody",
				"expressionsSequence"
			}), true},
			{"else", "elseClause", true}
		})},
		{"implicitChainExpression", NodeRule({
			{nullopt, TokenRule("operator|operatorPrefix|operatorInfix", "\\.")},
			{"member", VariantRule({
				"identifier",
				"stringLiteral"
			})}
		})},
		{"implicitChainIdentifier", NodeRule({
			{nullopt, TokenRule("operator|operatorPrefix|operatorInfix", "\\.")},
			{"value", "identifier"}
		})},
		{"importDeclaration", NodeRule({
			{nullopt, TokenRule("keywordImport")},
			{"value", VariantRule({
				"chainedIdentifier",
				"identifier"
			}), true}
		})},
		{"inClause", NodeRule({
			{nullopt, TokenRule("keywordIn")},
			{"value", "expressionsSequence", true}
		}, 2)},
		{"infixExpression", NodeRule({
			{nullopt, TokenRule("delimiterImplicit"), true},
			{"value", VariantRule({
				"conditionalOperator",
				"inOperator",
				"infixOperator"
			})},
			{nullopt, TokenRule("delimiterImplicit"), true}
		}, 2)},
		{"infixOperator", NodeRule({
			{"value", TokenRule("operator|operatorInfix", "[^,:]*")}  // Enclosing nodes can use operators from the exceptions list as delimiters
		})},
		{"inheritedTypesClause", NodeRule({
			{nullopt, TokenRule("operator.*", ":")},
			{"types", SequenceRule {
				.rule = "typeIdentifier",
				.delimiter = TokenRule("operator.*", ","),
				.innerDelimitRange = {1, usize(-1)}
			}}
		}, 2)},
		{"initializerClause", NodeRule({
			{nullopt, TokenRule("operator.*", "=")},
			{"value", "expressionsSequence", true}
		}, 2)},
		{"initializerDeclaration", NodeRule({
			{"modifiers", "modifiers"},
			{nullopt, TokenRule("identifier", "init")},
			{"nillable", LogicRule {
				.rule = TokenRule("operator.*", "\\?")
			}, true},
			{"signature", "functionSignature", true},
			{"body", "functionBody", true}
		})},
		{"inOperator", NodeRule({
			{"inverted", TokenRule("operatorPrefix", "!"), true},
			{nullopt, TokenRule("keywordIn")}
		})},
		{"inoutExpression", NodeRule({
			{nullopt, TokenRule("operatorPrefix", "&")},
			{"value", "postfixExpression"},
		})},
		{"inoutType", NodeRule({
			{nullopt, TokenRule("keywordInout")},
			{"value", "unionType"},
		})},
		{"integerLiteral", NodeRule({
			{"value", TokenRule("numberInteger")}
		})},
		{"intersectionType", VariantRule({
			NodeRule({
				{"subtypes", SequenceRule {
					.rule = "postfixType",
					.delimiter = TokenRule("operator.*", "&"),
					.range = {2, usize(-1)},
					.innerDelimitRange = {1, usize(-1)}
				}}
			}),
			"postfixType"
		})},
		{"isExpression", NodeRule({
			{"value", "expression"},
			{"inverted", TokenRule("operatorPrefix", "!"), true},
			{nullopt, TokenRule("keywordIs")},
			{"type_", "type"},
		})},
		{"labelClause", NodeRule({
			{"label", "identifier"},
			{nullopt, TokenRule("operator.*", ":")},
		}, 2)},
		{"literalExpression", VariantRule({
			"arrayLiteral",
			"booleanLiteral",
			"dictionaryLiteral",
			"floatLiteral",
			"integerLiteral",
			"nilLiteral",
			"stringLiteral"
		})},
		{"loopStatement", VariantRule({
			"forStatement",
			"repeatStatement",
			"whileStatement"
		})},
		{"modifiers", SequenceRule {
			.rule = VariantRule({
				TokenRule("keywordInfix"),
				TokenRule("keywordPostfix"),
				TokenRule("keywordPrefix"),

				TokenRule("keywordPrivate"),
				TokenRule("keywordProtected"),
				TokenRule("keywordPublic"),

				TokenRule("keywordFinal"),
				TokenRule("keywordLazy"),
				TokenRule("keywordStatic"),
				TokenRule("keywordVirtual")
			})
		}},
		{"module", NodeRule({
			{"statements", SequenceRule {
				.rule = "functionStatement",
				.delimiter = TokenRule("delimiter.*"),
				.descender = TokenRule("endOfFile")
			}}
		})},
		{"namespaceBody", NodeRule({
			{nullopt, TokenRule("braceOpen")},
			{"statements", "namespaceStatements"},
			{nullopt, TokenRule("braceClosed|endOfFile")}
		})},
		{"namespaceDeclaration", NodeRule({
			{"modifiers", "modifiers"},
			{nullopt, TokenRule("keywordNamespace")},
			{"identifier", "identifier"},
			{"body", "namespaceBody", true}
		})},
		{"namespaceExpression", NodeRule({
			{nullopt, TokenRule("keywordNamespace")},
			{"body", "namespaceBody", true}
		})},
		{"namespaceStatements", SequenceRule {
			.rule = "declaration",
			.ascender = TokenRule("braceOpen"),
			.delimiter = TokenRule("delimiter.*"),
			.descender = TokenRule("braceClosed")
		}},
		{"nillableExpression", NodeRule({
			{"value", "postfixExpression"},
			{nullopt, TokenRule("operatorPostfix", "\\?")}
		})},
		{"nillableType", NodeRule({
			{"value", "postfixType"},
			{nullopt, TokenRule("operatorPostfix", "\\?")}
		})},
		{"nilLiteral", NodeRule({
			{nullopt, TokenRule("keywordNil")}
		})},
		{"observerDeclaration", NodeRule({
			{"identifier", "observerIdentifier"},
			{"body", "functionBody", true}
		})},
		{"observerIdentifier", NodeRule({
			{"value", TokenRule("identifier", "willGet|get|didGet|willSet|set|didSet|willDelete|delete|didDelete")}
		})},
		{"observersBody", NodeRule({
			{nullopt, TokenRule("braceOpen")},
			{"statements", "observersStatements"},
			{nullopt, TokenRule("braceClosed|endOfFile")}
		})},
		{"observersStatements", SequenceRule {
			.rule = "observerDeclaration",
			.ascender = TokenRule("braceOpen"),
			.delimiter = TokenRule("delimiter.*"),
			.descender = TokenRule("braceClosed")
		}},
		{"operator", TokenRule("operator.*")},
		{"operatorAssociativity", NodeRule({
			{nullopt, TokenRule("identifier", "associativity")},
			{nullopt, TokenRule("operator.*", ":")},
			{"value", TokenRule("identifier", "left|right|none")}
		})},
		{"operatorBody", NodeRule({
			{nullopt, TokenRule("braceOpen")},
			{"statements", "operatorStatements"},
			{nullopt, TokenRule("braceClosed|endOfFile")}
		})},
		{"operatorDeclaration", NodeRule({
			{"modifiers", "modifiers"},
			{nullopt, TokenRule("keywordOperator")},
			{"operator", "operator", true},
			{"body", "operatorBody", true}
		})},
		{"operatorPrecendence", NodeRule({
			{nullopt, TokenRule("identifier", "precedence")},
			{nullopt, TokenRule("operator.*", ":")},
			{"value", "integerLiteral"}
		})},
		{"operatorStatements", SequenceRule {
			.rule = VariantRule({
				"operatorAssociativity",
				"operatorPrecendence",
			}),
			.ascender = TokenRule("braceOpen"),
			.delimiter = TokenRule("delimiter.*"),
			.descender = TokenRule("braceClosed")
		}},
		/*
		{"parameter", NodeRule({
			{"label", "identifier", true},
			{"identifier", "identifier"},
			{"type_", "typeClause", true},
			{"value", "initializerClause", true}
		})},
		*/
		{"parameter", VariantRule({
			NodeRule({
				{"label", "identifier"},
				{"identifier", "identifier"},
				{"type_", "typeClause", true},
				{"value", "initializerClause", true}
			}),
			NodeRule({
				{"identifier", "identifier"},
				{"type_", "typeClause", true},
				{"value", "initializerClause", true}
			})
		})},
		{"parametersClause", NodeRule({
			{nullopt, TokenRule("parenthesisOpen")},
			{"parameters", SequenceRule {
				.rule = "parameter",
				.ascender = TokenRule("parenthesisOpen"),
				.delimiter = TokenRule("operator.*", ","),
				.descender = TokenRule("parenthesisClosed"),
				.innerDelimitRange = {1, usize(-1)}
			}},
			{nullopt, TokenRule("parenthesisClosed|endOfFile")}
		}, 2)},
		{"parenthesizedExpression", NodeRule({
			{nullopt, TokenRule("parenthesisOpen")},
			{"value", SequenceRule {
				.rule = "expressionsSequence",
				.ascender = TokenRule("parenthesisOpen"),
				.descender = TokenRule("parenthesisClosed"),
				.range = {1, 1},
				.normalize = true
			}},
			{nullopt, TokenRule("parenthesisClosed|endOfFile")}
		})},
		{"parenthesizedType", NodeRule({
			{nullopt, TokenRule("parenthesisOpen")},
			{"value", "type"},
			{nullopt, TokenRule("parenthesisClosed|endOfFile")}
		})},
		{"postfixExpression", VariantRule({
			"callExpression",
			"chainExpression",
			"defaultExpression",
			"nillableExpression",
			"subscriptExpression",
			"primaryExpression",
			NodeRule({
				{"value", "postfixExpression"},
				{"operator", "postfixOperator"}
			})
		})},
		{"postfixOperator", NodeRule({
			{"value", TokenRule("operatorPostfix", "[^,:]*")}  // Enclosing nodes can use trailing operators from the exceptions list
		})},
		{"postfixType", VariantRule({
			"defaultType",
			"nillableType",
			"primaryType"
		})},
		{"predefinedType", NodeRule({
			{"value", VariantRule({
				TokenRule("keywordUnderscore"),
				TokenRule("keywordVoid"),

				TokenRule("keywordAny"),
				TokenRule("keywordBool"),
				TokenRule("keywordDict"),
				TokenRule("keywordFloat"),
				TokenRule("keywordInt"),
				TokenRule("keywordString"),
				TokenRule("keywordType"),

				TokenRule("keywordCapitalAny"),
				TokenRule("keywordCapitalClass"),
				TokenRule("keywordCapitalEnumeration"),
				TokenRule("keywordCapitalFunction"),
				TokenRule("keywordCapitalNamespace"),
				TokenRule("keywordCapitalObject"),
				TokenRule("keywordCapitalProtocol"),
				TokenRule("keywordCapitalStructure")
			})}
		})},
		{"prefixExpression", NodeRule({
			{"operator", "prefixOperator", true},
			{"value", "postfixExpression"},
		}, 2)},
		{"prefixOperator", NodeRule({
			{"value", TokenRule("operatorPrefix", "[^&.]*")}  // primaryExpressions can start with operators from the exceptions list
		})},
		{"primaryExpression", VariantRule({
			"classExpression",
			"closureExpression",
			"enumerationExpression",
			"functionExpression",
			"identifier",
			"implicitChainExpression",
			"inoutExpression",
			"literalExpression",
			"namespaceExpression",
			"parenthesizedExpression",
			"protocolExpression",
			"structureExpression",
			"typeExpression"
		})},
		{"primaryType", VariantRule({
			"arrayType",
			"dictionaryType",
			"functionType",
			"parenthesizedType",
			"predefinedType",
			"protocolType",
			"typeIdentifier"
		})},
		{"protocolBody", NodeRule({
			{nullopt, TokenRule("braceOpen")},
			{"statements", "protocolStatements"},
			{nullopt, TokenRule("braceClosed|endOfFile")}
		})},
		{"protocolDeclaration", NodeRule({
			{"modifiers", "modifiers"},
			{nullopt, TokenRule("keywordProtocol")},
			{"identifier", "identifier"},
			{"inheritedTypes", "inheritedTypesClause", true},
			{"body", "protocolBody", true}
		})},
		{"protocolExpression", NodeRule({
			{nullopt, TokenRule("keywordProtocol")},
			{"inheritedTypes", "inheritedTypesClause", true},
			{"body", "protocolBody", true}
		})},
		{"protocolStatements", SequenceRule {
			.rule = VariantRule({
				"classDeclaration",
				"enumerationDeclaration",
				"functionDeclaration",
				"namespaceDeclaration",
				"protocolDeclaration",
				"structureDeclaration",
				"variableDeclaration"
			}),
			.ascender = TokenRule("braceOpen"),
			.delimiter = TokenRule("delimiter.*"),
			.descender = TokenRule("braceClosed")
		}},
		{"protocolType", NodeRule({
			{"body", "protocolBody"}
		})},
		{"repeatStatement", NodeRule({
			{nullopt, TokenRule("keywordRepeat")},
			{"body", "functionBody", true},
			{"while", "whileClause", true}
		})},
		{"returnClause", NodeRule({
			{nullopt, TokenRule("operator.*", "->")},
			{"type_", "type"}
		}, 2)},
		{"returnStatement", NodeRule({
			{nullopt, TokenRule("keywordReturn")},
			{"value", "expressionsSequence", true}
		})},
		{"stringExpression", NodeRule({
			{nullopt, TokenRule("stringExpressionOpen")},
			{"value", SequenceRule {
				.rule = "expressionsSequence",
				.ascender = TokenRule("stringExpressionOpen"),
				.descender = TokenRule("stringExpressionClosed"),
				.range = {1, 1},
				.normalize = true
			}},
			{nullopt, TokenRule("stringExpressionClosed|endOfFile")}
		})},
		{"stringLiteral", NodeRule({
			{nullopt, TokenRule("stringOpen")},
			{"segments", SequenceRule {
				.rule = VariantRule({
					"stringSegment",
					"stringExpression"
				}),
				.ascender = TokenRule("stringOpen"),
				.descender = TokenRule("stringClosed")
			}},
			{nullopt, TokenRule("stringClosed|endOfFile")}
		})},
		{"stringSegment", NodeRule({
			{"value", TokenRule("stringSegment")}
		})},
		{"structureBody", NodeRule({
			{nullopt, TokenRule("braceOpen")},
			{"statements", "structureStatements"},
			{nullopt, TokenRule("braceClosed|endOfFile")}
		})},
		{"structureDeclaration", NodeRule({
			{"modifiers", "modifiers"},
			{nullopt, TokenRule("keywordStruct")},
			{"identifier", "identifier"},
			{"genericParameters", "genericParametersClause", true},
			{"inheritedTypes", "inheritedTypesClause", true},
			{"body", "structureBody", true}
		})},
		{"structureExpression", NodeRule({
			{nullopt, TokenRule("keywordStruct")},
			{"genericParameters", "genericParametersClause", true},
			{"inheritedTypes", "inheritedTypesClause", true},
			{"body", "structureBody", true}
		})},
		{"structureStatements", SequenceRule {
			.rule = VariantRule({
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
			}),
			.ascender = TokenRule("braceOpen"),
			.delimiter = TokenRule("delimiter.*"),
			.descender = TokenRule("braceClosed")
		}},
		{"subscriptArgumentsClause", NodeRule({
			{nullopt, TokenRule("bracketOpen")},
			{"arguments", SequenceRule {
				.rule = "argument",
				.ascender = TokenRule("bracketOpen"),
				.delimiter = TokenRule("operator.*", ","),
				.descender = TokenRule("bracketClosed"),
				.innerDelimitRange = {1, usize(-1)}
			}},
			{nullopt, TokenRule("bracketClosed|endOfFile")}
		}, 2)},
		{"subscriptDeclaration", NodeRule({
			{"modifiers", "modifiers"},
			{nullopt, TokenRule("identifier", "subscript")},
			{"signature", "functionSignature", true},
			{"body", VariantRule({
				"observersBody",
				"functionBody"
			}), true}
		})},
		{"subscriptExpression", NodeRule({
			{"composite", "postfixExpression"},
			{"genericArguments", "genericArgumentsClause", true},
			{"arguments", "subscriptArgumentsClause"},
			{"closure", "closureExpression", true}
		})},
		{"throwStatement", NodeRule({
			{nullopt, TokenRule("keywordThrow")},
			{"value", "expressionsSequence", true}
		})},
		{"tryExpression", NodeRule({
			{nullopt, TokenRule("keywordTry")},
			{"nillable", LogicRule {
				.rule = TokenRule("operatorPostfix", "\\?")
			}, true},
			{"value", "expression"},
		})},
		{"type", VariantRule({
			"variadicType",
			"inoutType",
			"unionType"
		})},
		{"typeClause", NodeRule({
			{nullopt, TokenRule("operator.*", ":")},
			{"type_", "type"}
		}, 2)},
		{"typeExpression", NodeRule({
			{nullopt, TokenRule("keywordType")},
			{"type_", "type"}
		})},
		{"typeIdentifier", NodeRule({
			{"identifier", VariantRule({
				"chainedIdentifier",
				"identifier"
			})},
			{"genericArguments", "genericArgumentsClause", true}
		})},
		{"unionType", VariantRule({
			NodeRule({
				{"subtypes", SequenceRule {
					.rule = "intersectionType",
					.delimiter = TokenRule("operator.*", "\\|"),
					.range = {2, usize(-1)},
					.innerDelimitRange = {1, usize(-1)}
				}}
			}),
			"intersectionType"
		})},
		{"variableDeclaration", NodeRule({
			{"modifiers", "modifiers"},
			{nullopt, TokenRule("keywordVar")},
			{"declarators", SequenceRule {
				.rule = "declarator",
				.delimiter = NodeRule({
					{nullopt, TokenRule("delimiterImplicit"), true},
					{nullopt, TokenRule("operator.*", ",")},
					{nullopt, TokenRule("delimiterImplicit"), true}
				}),
				.innerDelimitRange = {1, usize(-1)}
			}}
		})},
		{"variadicType", NodeRule({
			{"value", VariantRule({
				"inoutType",
				"unionType"
			})},
			{nullopt, TokenRule("operator.*", "\\.\\.\\.")}
		})},
		{"whereClause", NodeRule({
			{nullopt, TokenRule("keywordWhere")},
			{"value", "expressionsSequence", true}
		}, 2)},
		{"whileClause", NodeRule({
			{nullopt, TokenRule("keywordWhile")},
			{"condition", "expressionsSequence", true}
		}, 2)},
		{"whileStatement", NodeRule({
			{nullopt, TokenRule("keywordWhile")},
			{"condition", "expressionsSequence", true},
			{"value", VariantRule({
				"functionBody",
				"expressionsSequence"
			}), true}
		})}
	};
};