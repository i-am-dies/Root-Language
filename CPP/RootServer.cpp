#include "Interpreter.cpp"
#include "Scheduler.cpp"

namespace RootServer {
	optional<string> code;
	optional<deque<Lexer::Token>> tokens;
	optional<NodeSP> tree;
	mutex interpreterMutex;

	struct Request {
		unordered_set<string> receiverTokens;  // Empty - inherit from sender, non-empty - specific
		unordered_set<int> receiverFDs,           // Empty - any, non-empty - specific
						   receiverProcessesIDs;
		int timeout = 0;  // 0 - Infinity, 1+ - seconds
		optional<usize> timeoutTaskID;
		bool reauthReceiver = false,
			 multipleResponses = false;
		unordered_map<int, bool> responderFDs;  // [Client FD : Responded]
	};

	struct Client {
		int FD = 0,
			processID = 0;
		enum class State {
		//	Pending,  (for spawned processes)
			Connected,
			Unresponsive
		} state;
		usize heartbeatTaskID = 0;
		unordered_set<string> tokens;
		unordered_map<string, Request> requests;

		bool match(const unordered_set<int>& FDs, const unordered_set<int>& processesIDs, const unordered_set<string>& senderTokens, bool fallback = false) {
			return (FDs.empty() ||
					FDs.contains(FD)) &&
				   (processesIDs.empty() ||
					processesIDs.contains(processID)) &&
				   (Interface::tokensMatch(tokens, senderTokens) || fallback &&
				    Interface::tokensMatch(senderTokens, tokens));
		}
	};

	unordered_map<int, Client> clients;
	mutex clientsMutex;

	// ----------------------------------------------------------------

	optional<usize> scheduleRequestTimeout(int senderFD, string requestID, int timeout) {
		if(timeout <= 0) {
			return nullopt;
		}

		return sharedScheduler.schedule([senderFD, requestID](usize timeoutTaskID) {
			lock_guard lock(clientsMutex);

			if(Client* client = find_ptr(clients, senderFD)) {
				auto it = client->requests.find(requestID);

				if(it != client->requests.end()) {
					if(!some(it->second.responderFDs, [](auto& v) { return v.second; })) {
						println(sharedServer->getLogPrefix(), "Request \"", requestID, "\" from ", senderFD, " has timed out with no response");
					}

					client->requests.erase(it);
				}
			}

			sharedScheduler.cancel(timeoutTaskID);
		}, timeout*1000);
	}

	usize scheduleServerHeartbeat(int clientFD) {
		return sharedScheduler.schedule([clientFD](int taskID) {
			unique_lock lock(clientsMutex);

			if(!sharedServer->isRunning() || !clients.contains(clientFD)) {
				return sharedScheduler.cancel(taskID);
			}

			switch(clients[clientFD].state) {
				case Client::State::Connected:
					clients[clientFD].state = Client::State::Unresponsive;
					println(sharedServer->getLogPrefix(), "Cautiously awaiting heartbeat from ", clientFD);
				break;
				case Client::State::Unresponsive:
					lock.unlock();
					sharedServer->disconnect(clientFD);
				break;
			}
		}, 15000, true);
	}

	usize scheduleClientHeartbeat() {
		return sharedScheduler.schedule([](int taskID) {
			if(!sharedClient->isRunning()) {
				return sharedScheduler.cancel(taskID);
			}

			Interface::sendToServer({
				{"type", "notification"},
				{"action", "heartbeat"}
			});
		}, 7500, true, true);
	}

	// ----------------------------------------------------------------

	void handleServerConnect(int clientFD) {
		lock_guard lock(clientsMutex);
		ucred credentials;
		socklen_t len = sizeof(ucred);

		if(getsockopt(clientFD, SOL_SOCKET, SO_PEERCRED, &credentials, &len) < 0) {
			println(sharedServer->getLogPrefix(), "Can't get credentials of ", clientFD);
		}

		clients[clientFD] = {
			.FD = clientFD,
			.processID = credentials.pid,
			.state = Client::State::Connected,
			.heartbeatTaskID = scheduleServerHeartbeat(clientFD)
		};
	}

	void handleServerDisconnect(int clientFD) {
		lock_guard lock(clientsMutex);
		Client& client = clients.at(clientFD);

		sharedScheduler.cancel(client.heartbeatTaskID);

		for(auto& [requestID, request] : client.requests) {
			if(request.timeoutTaskID) {
				sharedScheduler.cancel(*request.timeoutTaskID);
			}
		}

		clients.erase(clientFD);
	}

	void handleServerMessage(int senderFD, const string& rawMessage) {
		lock_guard lock(clientsMutex);
		Client& sender = clients.at(senderFD);

		if(NodeSP message = NodeParser(rawMessage).parse()) {
			string receiver = message->get("receiver"),
				   type = message->get("type");

			if(receiver == "server") {
				#ifndef NDEBUG
					println(sharedServer->getLogPrefix(), "Handling server-side message from ", senderFD, ": ", rawMessage);
				#endif

				string action = message->get("action");

				if(type == "notification") {
					if(action == "heartbeat") {
						sharedScheduler.reset(sender.heartbeatTaskID);

						if(sender.state == Client::State::Unresponsive) {
							sender.state = Client::State::Connected;
							println(sharedServer->getLogPrefix(), "Late heartbeat from ", senderFD);
						} else {
							#ifndef NDEBUG
								println(sharedServer->getLogPrefix(), "Heartbeat from ", senderFD);
							#endif
						}
					} else
					if(action == "setTokens") {
						if(NodeArraySP senderTokens = message->get("senderTokens")) {
							sender.tokens = {};

							for(string t : *senderTokens) {
								if(Interface::isToken(t)) {
									sender.tokens.insert(t);
								}
							}

							#ifndef NDEBUG
								println(sharedServer->getLogPrefix(), "Tokens set for ", senderFD, ": ", join(sender.tokens, ", "));
							#endif
						}
					} else
					if(action == "cancelRequest") {
						string requestID = message->get("requestID");

						sender.requests.erase(requestID);  // Unregister pending request
					} else {
						println(sharedServer->getLogPrefix(), "Unknown server-side notification action from ", senderFD, ": \"", action, "\"");
					}
				} else
				if(type == "request") {
					string requestID = message->get("requestID");

					if(action == "listClients") {
						auto clientsFDs = clients | views::keys;

						sharedServer->send(senderFD, Node {
							{"type", "response"},
							{"responseID", requestID},
							{"clientsFDs", NodeArray(clientsFDs.begin(), clientsFDs.end())},
							{"receiverFD", senderFD}
						});
					} else {
						println(sharedServer->getLogPrefix(), "Unknown server-side request action from ", senderFD, ": \"", action, "\"");
					}
				} else {
					println(sharedServer->getLogPrefix(), "Unknown server-side message type from ", senderFD, ": \"", type, "\"");
				}
			} else
			if(receiver == "client") {
				#ifndef NDEBUG
					println(sharedServer->getLogPrefix(), "Handling client-side message from ", senderFD, ": ", rawMessage);
				#endif

				unordered_set<int> receiverFDs,
								   receiverProcessesIDs;
				unordered_set<string> senderTokens;
				int sends = 0;

				message->remove("receiver");

				if(NodeArraySP RFD = message->get("receiverFDs", nullptr)) {
									 message->remove("receiverFDs");

					receiverFDs = { RFD->begin(), RFD->end() };
				}
				if(NodeArraySP RPID = message->get("receiverProcessesIDs", nullptr)) {
									  message->remove("receiverProcessesIDs");

					receiverProcessesIDs = { RPID->begin(), RPID->end() };
				}
				if(NodeArraySP RT = message->get("receiverTokens", nullptr)) {
									message->remove("receiverTokens");

					for(const string& receiverToken : *RT) {
						senderTokens.insert(">"+receiverToken);
					}
				} else {
					senderTokens = sender.tokens;
				}

				(*message)["senderFD"] = senderFD;
				(*message)["senderProcessID"] = sender.processID;

				string dryMessage = *message;

				if(type == "notification") {
					for(auto& [clientFD, client] : clients) {
						if(clientFD != senderFD && client.match(receiverFDs, receiverProcessesIDs, senderTokens, true)) {
							sharedServer->send(clientFD, dryMessage);  // Notify
							sends++;
						}
					}
				} else
				if(type == "request") {
					string requestID = message->get("requestID");

					if(sender.requests.contains(requestID)) {
						println(sharedServer->getLogPrefix(), "Request \"", requestID, "\" is already created by ", senderFD, " and will not be replaced");

						return;
					}

					int timeout = message->contains("timeout") ? message->get<int>("timeout") : 30;
					bool reauthReceiver = message->get("reauthReceiver"),
						 multipleResponses = message->get("multipleResponses");
					Request& request = (sender.requests[requestID] = {  // Register request
						senderTokens,
						receiverFDs,
						receiverProcessesIDs,
						timeout,
						scheduleRequestTimeout(senderFD, requestID, timeout),
						reauthReceiver,
						multipleResponses
					});

					for(auto& [clientFD, client] : clients) {
						if(clientFD != senderFD && client.match(receiverFDs, receiverProcessesIDs, senderTokens)) {
							request.responderFDs[clientFD] = false;  // Register responder
						}
					}
					for(auto& [responderFD, responded] : request.responderFDs) {
						sharedServer->send(responderFD, dryMessage);  // Request responce
						sends++;
					}
				} else
				if(type == "response") {
					string responseID = message->get("responseID");

					for(auto& [clientFD, client] : clients) {
						auto it = client.requests.find(responseID);

						if(it == client.requests.end()) {
							continue;
						}

						Request& request = it->second;

						if(!request.responderFDs.contains(senderFD)) {
							continue;  // Allow only previously registered responders to handle request with that ID
						}
						if(request.reauthReceiver && !sender.match(request.receiverFDs, request.receiverProcessesIDs, request.receiverTokens)) {
							println(sharedServer->getLogPrefix(), "Responder ", senderFD, " has failed to reauthentificate");

							continue;
						}

						sharedServer->send(clientFD, dryMessage);  // Respond
						sends++;
						request.responderFDs.at(senderFD) = true;

						if(!request.multipleResponses || !some(request.responderFDs, [](auto& v) { return !v.second; })) {
							client.requests.erase(it);  // Unregister fulfilled request
						}
					}
				} else {
					println(sharedServer->getLogPrefix(), "Unknown client-side message type from ", senderFD, ": \"", type, "\"");
				}

				if(sends == 0) {
					println(sharedServer->getLogPrefix(), "No message was sent to clients while handling: ", rawMessage);
				}
			} else {
				println(sharedServer->getLogPrefix(), "Unknown receiver kind from ", senderFD, ": \"", receiver, "\"");
			}
		}
	}

	void startServer() {
		sharedServer = SP<Socket>(*Interface::preferences.socketPath, Socket::Mode::Server);

		sharedServer->setConnectionHandler(&handleServerConnect, &handleServerDisconnect);
		sharedServer->setMessageHandler(&handleServerMessage);

		sharedServer->start();
	}

	// ----------------------------------------------------------------

	int clientHeartbeatTaskID = -1;

	void handleClientConnect(int) {
		clientHeartbeatTaskID = scheduleClientHeartbeat();

		if(!Interface::preferences.tokens.empty()) {
			Interface::sendToServer({
				{"type", "notification"},
				{"action", "setTokens"},
				{"senderTokens", NodeArray(Interface::preferences.tokens.begin(), Interface::preferences.tokens.end())}
			});
		}
	}

	void handleClientDisconnect(int) {
		sharedScheduler.cancel(clientHeartbeatTaskID);
	}

	void handleClientMessage(int, const string& rawMessage) {
		NodeSP message = NodeParser(rawMessage).parse();

		if(!message) {
			return;
		}

		string type = message->get("type"),
			   action = message->get("action");

		if(type == "notification") {
			if(action == "lex") {
				lock_guard lock(interpreterMutex);

				code = message->get<string>("code");
				tokens = Lexer(*code).tokenize();
			} else
			if(action == "parse") {
				lock_guard lock(interpreterMutex);

				if(tokens) {
					tree = Parser(*tokens).parse();
				}
			} else
			if(action == "interpret") {
				lock_guard lock(interpreterMutex);

				if(code && tokens && tree) {
					sharedInterpreter->clean();
					SP<Interpreter>(sharedInterpreter, Interpreter::InheritedContext(2, 2, 2, 2), *code, *tokens, *tree)->interpret();
				}
			} else {
				println(sharedClient->getLogPrefix(), "Unknown notification action: \"", action, "\"");
			}
		} else
		if(type == "request") {
			if(action == "evaluate") {
				string code = message->get("code");
				auto tokens = Lexer(code).tokenize();
				auto tree = Parser(tokens).parse();

				SP<Interpreter>(sharedInterpreter, Interpreter::InheritedContext(2), code, tokens, tree)->interpret();
			} else {
				println(sharedClient->getLogPrefix(), "Unknown request action: \"", action, "\"");
			}
		} else {
			println(sharedServer->getLogPrefix(), "Unknown message type: \"", type, "\"");
		}
	}

	void startClient() {
		sharedClient = SP<Socket>(*Interface::preferences.socketPath, Socket::Mode::Client);

		sharedClient->setConnectionHandler(&handleClientConnect, &handleClientDisconnect);
		sharedClient->setMessageHandler(&handleClientMessage);

		while(true) {
			sharedClient->start();
			this_thread::sleep_for(5s);
		}
	}
};

int main(int argc, char* argv[]) {
	if(!Interface::parseArguments(argc, argv)) {
		return 1;
	}

	Interface::printPreferences();

	using namespace RootServer;

	switch(Interface::preferences.mode) {
		case Interface::Preferences::Mode::Interpret: {
			optional<thread> clientThread;

			if(Interface::preferences.socketPath) {
				clientThread = thread(&startClient);
			}
			if(Interface::preferences.scriptPath) {
				lock_guard lock(interpreterMutex);

				if(optional<string> code = read_file(*Interface::preferences.scriptPath)) {
					tokens = Lexer(*code).tokenize();
					tree = Parser(*tokens).parse();

					sharedInterpreter->clean();
					SP<Interpreter>(sharedInterpreter, Interpreter::InheritedContext(2, 2, 2, 2), *code, *tokens, *tree)->interpret();
				}
			}
			if(clientThread) {
				clientThread->join();
			}

			break;
		}
		case Interface::Preferences::Mode::Dashboard: {
			thread serverThread(&startServer);
			this_thread::sleep_for(125ms);
			thread clientThread(&startClient);

			clientThread.join();
			serverThread.join();

			break;
		}
	}

	return 0;
}