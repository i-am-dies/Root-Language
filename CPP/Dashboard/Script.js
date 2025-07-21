let auth = document.getElementById('auth'),
	tabsContainer = document.getElementById('tabsContainer'),
	clientViews = document.getElementById('clientViews'),
	authForm = document.getElementById('authForm'),
	socketPath = document.getElementById('socketPath'),
	socketToken = document.getElementById('socketToken'),
	heartbeatInterval,
	clientsListInterval,
	client,
	requestsHandlers = {},
	Buffer = require('buffer').Buffer;

function connectToAddress(address, onConnect) {
	const net = require('net');
	// Примитивная проверка — если строка содержит ":" и не начинается с "/", считаем TCP
	const isTcp = /^[^/]+:\d+$/.test(address);

	let socket;
	if (isTcp) {
		const [host, port] = address.split(':');
		socket = net.createConnection({ host, port: parseInt(port, 10) }, onConnect);
	} else {
		socket = net.createConnection(address, onConnect);
	}

	return socket;
}

function requestClientsListUpdate() {
	let requestID = Date.now().toString();

	requestsHandlers[requestID] = (report) => updateProcessTabs(report.clientsFDs.filter(v => v != report.receiverFD));

	send({ receiver: 'server', type: 'request', action: 'listClients', requestID });
}

function openSocket(path, token) {
	if(!path) {
		console.error('Invalid socket path');
		auth.style.display = '';
		return;
	}

	let readBuffer = Buffer.alloc(0);

	client = connectToAddress(path, () => {
		console.log('🟢 Connected to socket');
		client.setNoDelay(true);
		auth.style.display = 'none';

		send({ receiver: 'server', type: 'notification', action: 'setTokens', senderTokens: ['>'+token] });
		requestClientsListUpdate();
		send({ receiver: 'server', type: 'notification', action: 'heartbeat' });
		clearInterval(heartbeatInterval);
		heartbeatInterval = setInterval(() => send({ receiver: 'server', type: 'notification', action: 'heartbeat' }), 7500);

		clearInterval(clientsListInterval);
		clientsListInterval = setInterval(() => requestClientsListUpdate(), 5000);
	});

	client.on('data', (data) => {
		console.log('Received raw: ', data);
		readBuffer = Buffer.concat([readBuffer, data]);

		while (readBuffer.length >= 4) {
			const messageLength = readBuffer.readUInt32BE(0);

			if (readBuffer.length < 4+messageLength) {
				break; // ждём оставшуюся часть сообщения
			}

			const message = readBuffer.subarray(4, 4+messageLength).toString('utf-8');
			handleMessage(message);

			readBuffer = readBuffer.subarray(4+messageLength); // убираем обработанное сообщение
		}
	});

	client.on('error', (err) => {
		console.error('❌ Error:', err.message);

		clearInterval(heartbeatInterval);
		clearInterval(clientsListInterval);

		client = undefined;
		auth.style.display = '';
	});

	client.on('end', () => {
		console.log('🔴 Disconnected from server');

		clearInterval(heartbeatInterval);
		clearInterval(clientsListInterval);

		client = undefined;
		auth.style.display = '';
	});
}

function handleMessage(message) {
	let report = JSON.parse(message);

	console.log('Received: ', report);

	if(report.type == 'response') {
		for(let requestID in requestsHandlers) {
			if(requestID == report.responseID) {
				requestsHandlers[requestID](report);

				delete requestsHandlers[requestID]
			}
		}
	} else {
		if(report.action === 'tokenized') {
			let view = document.getElementById('client-'+report.senderFD);
			if (!view) return;

			let inputLint = view.querySelector('.inputLint');
			let inputText = view.querySelector('.inputText');
			let tokensOutput = view.querySelector('.tokensOutput');

			let tokens = report.tokens,
				lintHTML = '';

			for(let k = 0; k < tokens.length; k++) {
				let type = tokens[k].type,
					value = tokens[k].value,
					generated = tokens[k].generated,
					element;

				if(type === 'whitespace') {
					for(let i = 0; i < value.length; i++) {
						if(value[i] !== '\n') {
							element ??= document.createElement('span');
							element.innerText += value[i]

							if(value[i+1] === '\n' || i === value.length-1) {
								lintHTML += element.outerHTML;
								element = undefined;
							}
						}
						if(value[i] === '\n') {
							lintHTML += document.createElement('br').outerHTML;
						}
					}
				} else {
					element = document.createElement('span');
					element.innerText = value;

					if(type.startsWith('comment')) {
						element.style.color = 'rgb(95 179 63)';
					}
					if(type.startsWith('string') && !type.startsWith('stringExpression')) {
						element.style.color = 'rgb(191 127 95)';
					}
					if(type.startsWith('operator')) {
						element.style.color = 'rgb(255 191 0)';

						if(type.endsWith('Prefix')) {
							element.style.color = 'rgb(255 63 0)';
						}
						if(type.endsWith('Infix')) {
							element.style.color = 'rgb(255 95 0)';
						}
						if(type.endsWith('Postfix')) {
							element.style.color = 'rgb(255 127 0)';
						}
					}
					if(
						type.startsWith('parenthesis') ||
						type.startsWith('brace') ||
						type.startsWith('bracket') ||
						type === 'delimiter'
					) {
						element.style.color = 'rgb(255 255 255)';
					}
					if(type.startsWith('keyword')) {
						element.style.color = 'rgb(96 151 255)';

						if([
							'nil',
							'true',
							'false'
						].includes(value)) {
							element.style.color = 'rgb(223 127 223)';
						}
					}
					if(type === 'identifier' && value[0] === value[0].toUpperCase()) {
						element.style.color = 'rgb(223 223 223)';
					}
					if(type.startsWith('number')) {
						element.style.color = 'rgb(223 127 223)';
					}
					if(type === 'unsupported') {
						element.style.color = 'transparent';
					}
					if(generated) {
						element.style.fontStyle = 'italic';
					}

					lintHTML += element.outerHTML;
				}
			}

			inputLint.innerHTML = lintHTML;
			inputLint.scrollTop = inputText.scrollTop;
			inputLint.scrollLeft = inputText.scrollLeft;
			tokensOutput.value = JSON.stringify(tokens.filter(v => !v.trivia), null, 4);
		} else
		if(report.action === 'parsed') {
			let view = document.getElementById('client-'+report.senderFD);
			if (!view) return;

			let ASTOutput = view.querySelector('.ASTOutput');

			ASTOutput.value = JSON.stringify(report.tree, null, 4);
		} else
		if(report.action === 'interpreted') {}

		if(report.action === 'add') {
			let view = document.getElementById('client-'+report.senderFD);
			if (!view) return;

			let consoleOutput = view.querySelector('.consoleOutput');

			let message = report.string,
				color = report.level === 0 ? '0 0 0' : report.level === 1 ? '255 95 0' : '255 0 0',
				transparent = report.source !== 'interpreter',
				weight;

			if(report.location != null) {
				let line = report.location.line+1,
					column = report.location.column+1,
					level = report.level.toString()
										.replace('0', 'Info')
										.replace('1', 'Warning')
										.replace('2', 'Error');

				weight = 'regular';
				message = line+':'+column+': '+level+': '+message;
			} else {
				weight = 'bold';
			}

			let prev = document.createElement('span');

			prev.dataset.source = report.source;
			prev.dataset.position = report.position;
			prev.style.color = `rgba(${color} / ${transparent ? '0.75' : '1'})`;
			prev.style.fontWeight = weight;
			prev.innerText = message;

			let next,
				nodes = [...consoleOutput.children].reverse(),
				sources = ['lexer', 'parser', 'linter', 'interpreter']

			for(let node of nodes) {
				let nextPosition = node.dataset.position*1,
					prevPosition = report.position,
					nextSource = sources.indexOf(node.dataset.source),
					prevSource = sources.indexOf(report.source);

				if(nextSource > prevSource) {
					next = node;
				} else
				if(
					 nextSource <   prevSource ||
					(nextSource === prevSource && nextPosition <= prevPosition)
				) {
					next = node.nextSibling;

					break;
				}
			}

			consoleOutput.insertBefore(prev, next);
			consoleOutput.scrollTop = consoleOutput.scrollHeight;
		} else
		if(report.action === 'removeAfterPosition') {
			let view = document.getElementById('client-'+report.senderFD);
			if (!view) return;

			let consoleOutput = view.querySelector('.consoleOutput'),
				nodes = [...consoleOutput.children]

			for(let node of nodes) {
				if(node.dataset.source === report.source && node.dataset.position*1 > report.position)  {
					node.remove();
				}
			}
		} else
		if(report.action === 'removeAll') {
			let view = document.getElementById('client-'+report.senderFD);
			if (!view) return;

			let consoleOutput = view.querySelector('.consoleOutput');

			let sources = ['lexer', 'parser', 'linter', 'interpreter'],
				source = sources.indexOf(report.source);

			if(source === -1) {
				consoleOutput.innerHTML = '';
			} else {
				let toDelete = sources.slice(source),
					nodes = [...consoleOutput.children].filter(n => toDelete.includes(n.dataset.source));

				for(let node of nodes) {
					node.remove();
				}
			}
		}
	}
}

authForm.onsubmit = function(e) {
	e.preventDefault();
	openSocket(socketPath.value.trim(), socketToken.value.trim());
};

function updateProcessTabs(IDs) {
	IDs = IDs.map(v => v.toString());

	let newIDs = new Set(IDs);
	let existingTabs = {}
	let existingViews = {}

	// Сохраняем текущие вкладки и представления
	for (let tab of tabsContainer.children) {
		let ID = tab.dataset.id;
		if(ID) existingTabs[ID] = tab;
	}
	for (let view of clientViews.children) {
		existingViews[view.dataset.id] = view;
	}

	// Удаляем устаревшие
	for (let ID in existingTabs) {
		if (!newIDs.has(ID)) existingTabs[ID].remove();
	}
	for (let ID in existingViews) {
		if (!newIDs.has(ID)) existingViews[ID].remove();
	}

	// Обновляем или создаём вкладки и представления
	for(let ID of IDs) {
		if (existingTabs[ID] == null) {
			let tab = document.createElement('button');
			tab.innerText = '#'+ID;
			tab.dataset.id = ID;
			tab.onclick = () => showClient(ID);
			tabsContainer.appendChild(tab);
		}

		if (existingViews[ID] == null) {
			let template = document.getElementById('clientTemplate');
			let view = document.createElement('div');
			view.id = 'client-'+ID;
			view.dataset.id = ID;
			view.style.display = 'none';

			let clone = template.content.cloneNode(true);
			view.appendChild(clone);
			clientViews.appendChild(view);

			initializeClientInterface(view, ID);
		}
	}
}

function initializeClientInterface(container, receiverFD) {
	const inputText = container.querySelector('.inputText');
	const inputLint = container.querySelector('.inputLint');
	const tokensOutput = container.querySelector('.tokensOutput');
	const ASTOutput = container.querySelector('.ASTOutput');
	const compositesOutput = container.querySelector('.compositesOutput');
	const tokensTree = container.querySelector('.tokensTree');
	const ASTTree = container.querySelector('.ASTTree');
	const compositesTree = container.querySelector('.compositesTree');
	const interpret = container.querySelector('.interpret');
	const consoleInput = container.querySelector('.consoleInput');

	inputText.oninput = () => {
		inputLint.innerHTML = '';
		send({ receiver: 'client', /*receiverTokens: [socketToken.value],*/ type: 'notification', action: 'lex', code: inputText.value, receiverFDs: [receiverFD] });
		send({ receiver: 'client', /*receiverTokens: [socketToken.value],*/ type: 'notification', action: 'parse', receiverFDs: [receiverFD] });
	};

	inputText.onscroll = () => {
		inputLint.scrollTop = inputText.scrollTop;
		inputLint.scrollLeft = inputText.scrollLeft;
	};

	tokensTree.onclick = () => {
		tokensOutput.style.display = '';
		ASTOutput.style.display = 'none';
		compositesOutput.style.display = 'none';

		tokensTree.setAttribute('current_', '');
		ASTTree.removeAttribute('current_');
		compositesTree.removeAttribute('current_');
	}

	ASTTree.onclick = () => {
		tokensOutput.style.display = 'none';
		ASTOutput.style.display = '';
		compositesOutput.style.display = 'none';

		tokensTree.removeAttribute('current_');
		ASTTree.setAttribute('current_', '');
		compositesTree.removeAttribute('current_');
	}

	compositesTree.onclick = () => {
		tokensOutput.style.display = 'none';
		ASTOutput.style.display = 'none';
		compositesOutput.style.display = '';

		tokensTree.removeAttribute('current_');
		ASTTree.removeAttribute('current_');
		compositesTree.setAttribute('current_', '');
	}

	interpret.onclick = () => {
		send({ receiver: 'client', /*receiverTokens: [socketToken.value],*/ type: 'notification', action: 'interpret', receiverFDs: [receiverFD] });
	};

	consoleInput.onkeypress = (e) => {
		if (e.key === 'Enter') {
			send({ receiver: 'client', /*receiverTokens: [socketToken.value],*/ type: 'notification', action: 'evaluate', code: consoleInput.value, receiverFDs: [receiverFD] });
		}
	};
}

function showClient(ID) {
	[...tabsContainer.children].forEach(c => c.dataset.id == ID ? c.setAttribute('current_', '') : c.removeAttribute('current_'));
	[...clientViews.children].forEach(c => c.style.display = 'none');
	let view = document.getElementById('client-'+ID);
	if (view) view.style.display = '';
}

function send(action) {
	if(client?.writable) {
		let json = JSON.stringify(action);
		let lengthBuffer = Buffer.alloc(4);
		lengthBuffer.writeUInt32BE(Buffer.byteLength(json), 0);

		let packet = Buffer.concat([lengthBuffer, Buffer.from(json)]);
		client.write(packet);
		console.log('Sent: '+json);
	}
}