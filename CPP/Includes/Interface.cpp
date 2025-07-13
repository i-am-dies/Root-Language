#pragma once

#include "Node.cpp"
#include "Socket.cpp"

namespace Interface {
	struct Preferences {
		enum class Mode {
			Undefined,
			Interpret,
			Dashboard
		} mode = Mode::Undefined;

		optional<filesystem::path> scriptPath;
		vector<string> scriptArguments;

		optional<filesystem::path> socketPath;
		unordered_set<string> tokens;

		usize callStackSize = 128,
			  reportsLevel = 3,
			  metaprogrammingLevel = 3;
		bool preciseArithmetics = false;
	} preferences;

	void printUsage() {
		cout << "Usage:\n"
			 << "    RootServer (--interpret [PATH] | --dashboard)\n"
			 << "               [--socket [PATH]] [--token [TOKEN]]\n"
			 << "               [--callStackSize NUMBER] [--reportsLevel NUMBER] [--metaprogrammingLevel NUMBER] [--preciseArithmetics]\n"
			 << "               [--arguments [ARGUMENT]...]\n\n"

			 << "Modes:\n"
			 << "    (-i | --interpret) [PATH]                Interpret script on given path\n"
			 << "    (-d | --dashboard)                       Set up dashboard interface\n\n"

			 << "Debugging:\n"
			 << "    (-s | --socket) [PATH]                   Path of the socket (default - \"/tmp/RootServer.sock\" if enabled; always enabled for dashboard)\n"
			 << "    (-t | --token) (DIRECTION)[TOKEN]        Security token (at least one of \"<\" or \"=\" direction is required if socket enabled,\n"
			 << "                                             either as argument or standard input): < - input, > - output, = - universal\n\n"

			 << "Execution:\n"
			 << "    (-css | --callStackSize) NUMBER          Call stack size (default - 128)\n"
			 << "    (-rl | --reportsLevel) NUMBER            Reports level (default - 2): 0 - information, 1 - warning, 2 - error\n"
			 << "    (-ml | --metaprogrammingLevel) NUMBER    Metaprogramming level (default - 2): 0 - disabled, 1 - read, 2 - write\n"
			 << "    (-pa | --preciseArithmetics)             Precise string-based arithmetic (default - disabled)\n"
			 << "    (-a | --arguments) [ARGUMENT]...         Script arguments\n\n"

			 << "Help:\n"
			 << "    (-h | --help)                            Show this help message and exit\n";
	}

	void printPreferences() {
		cout << "                 Mode: " << (preferences.mode == Preferences::Mode::Interpret ? "Interpret" : "Dashboard") << endl;

		if(preferences.scriptPath) {
			cout << "          Script Path: " << *preferences.scriptPath << endl;
		}
		if(!preferences.scriptArguments.empty()) {
			cout << "     Script Arguments:";

			for(string& argument : preferences.scriptArguments) {
				cout << " " << argument;
			}

			cout << endl;
		}

		if(preferences.socketPath) {
			cout << "          Socket Mode: " << (preferences.mode == Preferences::Mode::Dashboard ? "Server" : "Client") << endl;
			cout << "          Socket Path: " << *preferences.socketPath << endl;
		}

		cout << "               Tokens: " << join(preferences.tokens, ", ") << endl;  // TODO: Should be hidden
		cout << "      Call Stack Size: " << preferences.callStackSize << endl;
		cout << "        Reports Level: " << preferences.reportsLevel << endl;
		cout << "Metaprogramming Level: " << preferences.metaprogrammingLevel << endl;
		cout << "  Precise Arithmetics: " << (preferences.preciseArithmetics ? "Enabled" : "Disabled") << endl;
	}

	bool isPositiveInteger(const string& s) {
		for(char c : s) {
			if(!isdigit(c)) {
				return false;
			}
		}

		return true;
	}

	bool isToken(const string& t, const string& d = "<>=") {
		for(char c : d) {
			if(t.starts_with(c)) {
				return true;
			}
		}

		return false;
	}

	bool isInputToken(const string& t) {
		return isToken(t, "<=");
	}

	bool isOutputToken(const string& t) {
		return isToken(t, ">=");
	}

	bool isUniversalToken(const string& t) {
		return isToken(t, "=");
	}

	bool tokensMatch(const unordered_set<string>& receiverTokens, const unordered_set<string>& senderTokens) {
	//	println("Matching: ", join(receiverTokens, ","), " and: ", join(senderTokens, ","));

		unordered_set<string> undirectedOutputSenderTokens;

		for(const string& senderToken : senderTokens) {
			if(isOutputToken(senderToken)) {
				undirectedOutputSenderTokens.insert(senderToken.substr(1));
			}
		}

		for(const string& receiverToken : receiverTokens) {
			if(isInputToken(receiverToken)) {
				string undirectedInputReceiverToken = receiverToken.substr(1);

				if(undirectedOutputSenderTokens.contains(undirectedInputReceiverToken)) {
					return true;
				}
			}
		}

		return false;
	}

	bool parseArguments(int argc, char* argv[]) {
		unordered_map<string, string> aliases = {
			{"-i", "--interpret"},
			{"-d", "--dashboard"},
			{"-s", "--socket"},
			{"-t", "--token"},
			{"-css", "--callStackSize"},
			{"-rl", "--reportsLevel"},
			{"-ml", "--metaprogrammingLevel"},
			{"-pa", "--preciseArithmetics"},
			{"-a", "--arguments"},
			{"-h", "--help"}
		};

		unordered_map<string, function<bool(int&)>> rules = {
			{"--interpret", [&](int& i) {
				preferences.mode = Preferences::Mode::Interpret;

				if(i+1 < argc && argv[i+1][0] != '-') {
					preferences.scriptPath = argv[++i];
				}

				return true;
			}},
			{"--dashboard", [&](int&) {
				preferences.mode = Preferences::Mode::Dashboard;
				preferences.socketPath = "/tmp/RootServer.sock";

				return true;
			}},
			{"--socket", [&](int& i) {
				if(i+1 < argc && argv[i+1][0] != '-') {
					preferences.socketPath = argv[++i];
				} else {
					preferences.socketPath = "/tmp/RootServer.sock";
				}

				return true;
			}},
			{"--token", [&](int& i) {
				char direction = '-';

				if(i+1 < argc) {
					direction = argv[i+1][0];
				}
				if(direction != '-' && direction != '<' && direction != '>' && direction != '=') {
					return false;
				}

				preferences.tokens.insert(direction == '-' ? "=" : argv[++i]);

				return true;
			}},
			{"--callStackSize", [&](int& i) {
				if(i+1 >= argc || !isPositiveInteger(argv[i+1])) {
					return false;
				}

				preferences.callStackSize = stoi(argv[++i]);

				return true;
			}},
			{"--reportsLevel", [&](int& i) {
				if(i+1 >= argc || !isPositiveInteger(argv[i+1])) {
					return false;
				}

				preferences.reportsLevel = stoi(argv[++i]);

				return true;
			}},
			{"--metaprogrammingLevel", [&](int& i) {
				if(i+1 >= argc || !isPositiveInteger(argv[i+1])) {
					return false;
				}

				preferences.metaprogrammingLevel = stoi(argv[++i]);

				return true;
			}},
			{"--preciseArithmetics", [&](int&) {
				preferences.preciseArithmetics = true;

				return true;
			}},
			{"--arguments", [&](int& i) {
				i++;

				for(; i < argc; i++) {
					preferences.scriptArguments.emplace_back(argv[i]);
				}

				return true;
			}},
			{"--help", [&](int&) {
				printUsage();
				exit(0);

				return true;
			}}
		};

		for(int i = 1; i < argc; i++) {
			string raw = argv[i],
				   arg = aliases.contains(raw) ? aliases.at(raw) : raw;

			if(rules.contains(arg)) {
				if(!rules[arg](i)) {
					cerr << "Invalid or missing argument for: " << raw << endl;

					return false;
				}
			} else {
				cerr << "Unknown argument: " << raw << endl;

				return false;
			}
		}

		if(preferences.mode == Preferences::Mode::Undefined) {
			cerr << "Error: --interpret or --dashboard must be specified\n";

			return false;
		}

		if(preferences.socketPath) {
			while(!some(preferences.tokens, &isInputToken)) {
				string token;

				cout << "Input token: ";

				getline(cin, token);

				if(cin.fail()) {
					cin.clear();
				}

				if(token.empty()) {
					preferences.tokens.insert("=");
				} else
				if(isInputToken(token)) {
					preferences.tokens.insert(token);
				} else {
					println("Wrong token format, expected input (<) or universal (=) direction specifier as a first character");
				}
			}
		}

		return true;
	}

	// ----------------------------------------------------------------

	void send(const string& input) {
		if(sharedClient) {
			sharedClient->send(input);
		}
	}

	void sendToClients(Node node) {
		node["receiver"] = "client";

		send(node);
	}

	void sendToServer(Node node) {
		node["receiver"] = "server";

		send(node);
	}
}