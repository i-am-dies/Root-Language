#pragma once

#include "Lexer.cpp"
#include "Grammar.cpp"

struct Parser {
	using Location = Lexer::Location;
	using Token = Lexer::Token;

	using NodeRule = Grammar::NodeRule;
	using TokenRule = Grammar::TokenRule;
	using LogicRule = Grammar::LogicRule;
	using VariantRule = Grammar::VariantRule;
	using SequenceRule = Grammar::SequenceRule;
	using RuleRef = Grammar::RuleRef;
	using Rule = Grammar::Rule;

	struct Key {
		Rule rule;
		usize start = 0;

		bool operator==(const Key& k) const = default;
	};

	struct Hasher {
		usize operator()(const Key& k) const {
			return hash<usize>()(k.rule.index())^hash<usize>()(k.start);
		}
	};

	struct Frame {
		Frame() {}
		Frame(const Rule& rule, usize start = 0, bool permitsDirt = false) : key(Key(rule, start)), permitsDirt(permitsDirt) {}

		Key key;
		NodeValue value;
		usize cleanTokens = 0,
			  dirtyTokens = 0,
			  version = 0;
		bool permitsDirt = false,  // Allow the frame parser to add a dirt into the value
			 isInitialized = false,
			 isCalled = false,
			 isRecursive = false;

		string title() const {
			switch(key.rule.index()) {
				case 0:  return get<0>(key.rule);
				case 1:  return "[node]";
				case 2:  return "[token]";
				case 3:  return "[variant]";
				case 4:  return "[sequence]";
				default: return string();
			}
		}

		usize size() const {
			return cleanTokens+dirtyTokens;
		}

		void addSize(const Frame& other) {
			cleanTokens += other.cleanTokens;
			dirtyTokens += other.dirtyTokens;
		}

		void removeSize(const Frame& other) {
			if(other.cleanTokens > cleanTokens) {
				cleanTokens = 0;
			} else {
				cleanTokens -= other.cleanTokens;
			}

			if(other.dirtyTokens > dirtyTokens) {
				dirtyTokens = 0;
			} else {
				dirtyTokens -= other.dirtyTokens;
			}
		}

		usize start() const {
			return key.start;
		}

		usize end() const {
			return start()+size();
		}

		// Should be used for non-proxy rules with own values and accumulative sizes before parsing.
		// Rules that do just _set_ their value/size behave fine without that, but other can unintentionally mislead the "greater" check.
		void clearResult() {
			value = nullptr;
			cleanTokens = 0;
			dirtyTokens = 0;
		}

		void apply(const Frame& other) {
			value = move(other.value);
			cleanTokens = other.cleanTokens;
			dirtyTokens = other.dirtyTokens;
			permitsDirt = other.permitsDirt;
		}

		bool empty() const {
			bool filled = cleanTokens > 0 || permitsDirt && dirtyTokens > 0 || cleanTokens == 0 && dirtyTokens == 0 && !value.empty();

			return !filled;
		}

		bool greater(const Frame& other) const {
			if(empty() != other.empty())			return !empty();
			if(cleanTokens != other.cleanTokens)	return cleanTokens > other.cleanTokens;
			if(dirtyTokens != other.dirtyTokens)	return dirtyTokens < other.dirtyTokens;

			return false;
		}
	};

	deque<Token> tokens;
	unordered_map<Key, Frame, Hasher> cache;
	unordered_map<usize, usize> versions;  // [Position : Version]
	usize calls = 0;

	Parser(deque<Token> tokens) : tokens(filter(tokens, [](auto& t) { return !t.trivia; })) {}

	// ----------------------------------------------------------------

	optional<Frame> parseReference(Frame& refFrame) {
		RuleRef& ruleRef = get<0>(refFrame.key.rule);

		if(!Grammar::rules.contains(ruleRef)) {
			return nullopt;
		}

		Rule& rule = Grammar::rules[ruleRef];
		Frame& ruleFrame = parse({rule, refFrame.start()});

		if(ruleFrame.value.type() == 5) {
			Node& node = ruleFrame.value;

			if(!node.contains("type")) {
				node["type"] = ruleRef;
			}
		}

		return ruleFrame;
	}

	// NOTE:
	// There is no support for optional fields branching (which is rare problem), e.g. you can't use non-optional field after optional with the same rule.
	// This can be avoided at a cost of an enclosing variant rule and some redundancy (look at the "parameter" rule as an example).
	optional<Frame> parseNode(Frame& nodeFrame) {
		NodeRule& nodeRule = get<1>(nodeFrame.key.rule);
		Node& node = nodeFrame.value = Node();

		for(auto& field : nodeRule.fields) {
			Frame& fieldFrame = parse({field.rule, nodeFrame.end()});

			if(fieldFrame.empty() && !field.optional) {
				return nullopt;
			}

			nodeFrame.addSize(fieldFrame);

			if(field.title && (!fieldFrame.empty() || !field.optional)) {
				node[*field.title] = fieldFrame.value;
			}
		}

		if(nodeRule.normalize > 0 && node.size() <= 1) {
			if(node.empty()) {
				return nullopt;
			}
			if(nodeRule.normalize == 2) {
				nodeFrame.value = node.begin()->second;

				return nodeFrame;
			}
		}

		int start = nodeFrame.start(),
			end = start+max(usize(), nodeFrame.size() ? nodeFrame.size()-1 : 0);

		node["range"] = {
			{"start", start},
			{"end", end}
		};

		if(nodeRule.post) {
			nodeRule.post(node);
		}

		return nodeFrame;
	}

	optional<Frame> parseToken(Frame& tokenFrame) {
		TokenRule& tokenRule = get<2>(tokenFrame.key.rule);
		static Token endOfFileToken = { .type = "endOfFile" };
		usize position = tokenFrame.start();
		Token& token = position < tokens.size() ? tokens[position] : endOfFileToken;
		string operands[2] = { token.type, token.value };
		bool clean = true;

		#ifndef NDEBUG
			println("Token at position ", position, ": ", token.type, ", value: ", token.value);
		#endif

		for(int i = 0; i < 2; i++) {
			if(tokenRule.patterns[i] && !regex_match(operands[i], tokenRule.regexes[i])) {
				if(!tokenFrame.permitsDirt) {
					return nullopt;
				}

				#ifndef NDEBUG
					println("[Parser] Token operand[", i, "] (", operands[i], ") is not matching \"", *tokenRule.patterns[i], "\"");
				#endif

				clean = false;

				break;
			}
		}

		tokenFrame.value = token.value;

		if(clean) {
			tokenFrame.cleanTokens++;
		} else {
			tokenFrame.dirtyTokens++;
		}

		return tokenFrame;
	}

	optional<Frame> parseLogic(Frame& logicFrame) {
		LogicRule& logicRule = get<3>(logicFrame.key.rule);
		Frame& ruleFrame = parse({logicRule.rule, logicFrame.end()});

		if(ruleFrame.empty()) {
			return nullopt;  // False
		} else {
			logicFrame.value = true;  // True
			logicFrame.addSize(ruleFrame);

			if(logicRule.optionalizer) {
				Frame& optionalizerFrame = parse({*logicRule.optionalizer, logicFrame.end()});

				if(!optionalizerFrame.empty()) {
					logicFrame.value = false;  // Optional
					logicFrame.addSize(optionalizerFrame);
				}
			}
		}

		return logicFrame;
	}

	optional<Frame> parseVariant(Frame& variantFrame) {
		VariantRule& variantRule = get<4>(variantFrame.key.rule);
		usize position = variantFrame.start();
		optional<Frame> greatestFrame;

		for(Rule& rule : variantRule) {
			Frame& ruleFrame = parse({rule, position});

			if(!greatestFrame || ruleFrame.greater(*greatestFrame)) {
				greatestFrame = ruleFrame;
			}
		}

		return greatestFrame;
	}

	bool parseDelimitSubsequence(Frame& sequenceFrame, vector<Frame>& frames, usize range[2]) {
		SequenceRule& sequenceRule = get<5>(sequenceFrame.key.rule);

		if(!sequenceRule.delimiter) {
			return true;
		}

		Rule rule = *sequenceRule.delimiter;
		usize count = 0,
			  minCount = range[0],
			  maxCount = range[1];

		while(sequenceFrame.end() <= tokens.size() && count < maxCount) {
			Frame& frame = parse({rule, sequenceFrame.end()});

			if(frame.empty()) {
				break;
			}

			sequenceFrame.addSize(frame);
			count++;

			if(sequenceRule.delimited) {
				frames.push_back(frame);
			}
		}

		return count >= minCount;
	}

	bool parseSubsequence(Frame& sequenceFrame, vector<Frame>& frames, usize range[2]) {
		SequenceRule& sequenceRule = get<5>(sequenceFrame.key.rule);

		if(!parseDelimitSubsequence(sequenceFrame, frames, sequenceRule.outerDelimitRange)) {
			return false;
		}

		Rule rule = sequenceRule.rule;
		usize count = 0,
			  minCount = range[0],
			  maxCount = range[1],
			  rollbackIndex = frames.size(),
			  scopeLevel = 1;
		bool atDelimit = false;

		while(sequenceFrame.end() <= tokens.size() && count < maxCount) {
			// Parse delimited rule frame
			if(!atDelimit) {
				Frame& frame = parse({rule, sequenceFrame.end()});

				if(!frame.empty()) {
					sequenceFrame.addSize(frame);
					count++;
					frames.push_back(frame);
					rollbackIndex = frames.size();
					atDelimit = true;
					continue;
				}
			} else
			if(parseDelimitSubsequence(sequenceFrame, frames, sequenceRule.innerDelimitRange)) {
				atDelimit = false;
				continue;
			}

			// Unexpected region reached

			// Rollback rightmost inner delimiter frame(s)
			for(usize i = frames.size(); i > rollbackIndex; i--) {
				Frame& frame = frames.back();

				sequenceFrame.removeSize(frame);
				frames.pop_back();
			}

			// Parse dirty frame
			if(!sequenceFrame.permitsDirt) {
				break;
			}

			static Frame dummy;
			Frame& dirtyFrame = (dummy = Frame());

			if(sequenceRule.ascender) {
				dirtyFrame = parse({*sequenceRule.ascender, sequenceFrame.end()});

				if(!dirtyFrame.empty()) {
					scopeLevel++;
				}
			}
			if(dirtyFrame.empty() && sequenceRule.descender) {
				dirtyFrame = parse({*sequenceRule.descender, sequenceFrame.end()});

				if(!dirtyFrame.empty()) {
					scopeLevel--;
					if(scopeLevel == 0) { break; }
				}
			}
			if(dirtyFrame.empty()) {
				dirtyFrame = parse({TokenRule(), sequenceFrame.end(), true});  // Always expecting exactly one token (including EOF) here
			}

			// Append dirty frame
			int position = dirtyFrame.start();

			if(
				frames.empty() ||
				frames.back().value.type() != 5 ||
				frames.back().value.get<NodeSP>()->get("type", "") != "dirt" ||
				frames.back().value.get<NodeSP>()->get<Node&>("range").get<int>("end")+1 < position  // Clean frames split unexpected regions even if they are implicit
			) {
				frames.emplace_back().value = Node {
					{"type", "dirt"},
					{"range", {
						{"start", position}
					}},
					{"tokens", NodeArray {}}
				};
				rollbackIndex++;
			}

			Frame& dirtFrame = frames.back();
			Node& dirtNode = dirtFrame.value;

			sequenceFrame.dirtyTokens += dirtyFrame.size();
			dirtFrame.dirtyTokens += dirtyFrame.size();  // Rollback support
			dirtNode.get<NodeArray&>("tokens").push_back(dirtyFrame.value);
			dirtNode.get<Node&>("range")["end"] = position;
		}

		return count >= minCount &&
			   parseDelimitSubsequence(sequenceFrame, frames, sequenceRule.outerDelimitRange);
	}

	optional<Frame> parseSequence(Frame& sequenceFrame) {
		SequenceRule& sequenceRule = get<5>(sequenceFrame.key.rule);
		vector<Frame> frames;

		sequenceFrame.permitsDirt = sequenceRule.descender.has_value();

		if(!parseSubsequence(sequenceFrame, frames, sequenceRule.range)) {
			return nullopt;
		}

		if(sequenceRule.normalize && frames.size() <= 1) {
			if(frames.empty()) {
				return nullopt;
			}

			sequenceFrame.value = frames.front().value;

			return sequenceFrame;
		}

		NodeArray& values = sequenceFrame.value = NodeArray();

		for(Frame& f : frames) {
			values.push_back(f.value);
		}

		return sequenceFrame;
	}

	optional<Frame> dispatch(Frame frame) {
		frame.clearResult();

		switch(frame.key.rule.index()) {
			case 0:  return parseReference(frame);
			case 1:  return parseNode(frame);
			case 2:  return parseToken(frame);
			case 3:  return parseLogic(frame);
			case 4:  return parseVariant(frame);
			case 5:  return parseSequence(frame);
			default: return nullopt;
		}
	}

	Frame& parse(const Frame& templateFrame) {
		Frame& frame = cache[templateFrame.key];
		usize position = templateFrame.start();
		string title = templateFrame.title();

		if(!frame.isInitialized) {
			frame.key = templateFrame.key;
			frame.isInitialized = true;
			frame.apply(templateFrame);
		} else
		if(frame.isCalled || frame.version == versions[position]) {
			if(frame.isCalled) {
				frame.isRecursive = true;
			}

			#ifndef NDEBUG
				println("[Parser] # ", repeat("| ", calls), title, " at ", position, ", recursion: ", frame.isCalled ? "yes" : "no", " => version: ", frame.version, ", clean: ", frame.cleanTokens, ", dirty: ", frame.dirtyTokens, ", value: ", to_string(frame.value));
			#endif

			return frame;
		}

		#ifndef NDEBUG
			println("[Parser] # ", repeat("| ", calls++), title, " at ", position, " => old version: ", frame.version, " {");
		#endif

		frame.isCalled = true;

		while(optional<Frame> newFrame = dispatch(frame)) {
			if(!newFrame->greater(frame)) {
				break;
			}

			frame.apply(*newFrame);
			versions[position]++;

			#ifndef NDEBUG
				println("[Parser] # ", repeat("| ", calls), "- version: ", versions[position], ", clean: ", frame.cleanTokens, ", dirty: ", frame.dirtyTokens, ", value: ", to_string(frame.value));

				Interface::sendToClients({
					{"type", "notification"},
					{"source", "parser"},
					{"action", "parsed"},
					{"tree", frame.value}
				});
			#endif

			if(!frame.isRecursive) {
				break;
			}
		}

		frame.version = versions[position];
		frame.isCalled = false;

		#ifndef NDEBUG
			println("[Parser] # ", repeat("| ", --calls), "} => new version: ", frame.version);
		#endif

		return frame;
	}

	NodeSP parse() {
		Interface::sendToClients({
			{"type", "notification"},
			{"source", "parser"},
			{"action", "removeAll"},
			{"moduleID", -1}
		});

		NodeSP tree = parse({"module"}).value;

		Interface::sendToClients({
			{"type", "notification"},
			{"source", "parser"},
			{"action", "parsed"},
			{"tree", tree}
		});

		return tree;
	}
};