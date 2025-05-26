let auth = document.getElementById('auth'),
    tabsContainer = document.getElementById('tabsContainer'),
    clientViews = document.getElementById('clientViews'),
    authForm = document.getElementById('authForm'),
    socketPath = document.getElementById('socketPath'),
    socketToken = document.getElementById('socketToken'),
    heartbeatInterval,
    processListInterval,
    client;

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

        send({ type: 'request', action: 'listProcesses' });
        send({ type: 'notification', action: 'heartbeat', senderTokens: ['='] });
    	clearInterval(heartbeatInterval);
    	heartbeatInterval = setInterval(() => send({ type: 'notification', action: 'heartbeat', senderTokens: ['='] }), 7500);

    //	clearInterval(processListInterval);
    //	processListInterval = setInterval(() => send({ type: 'request', action: 'listProcesses', token: token }), 10000);
    });

    client.on('data', (data) => {
        console.log('chunk size', data.length);
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
    });

    client.on('end', () => {
        console.log('🔴 Disconnected from server');

        clearInterval(heartbeatInterval);
        clearInterval(processListInterval);

        client = undefined;
        auth.style.display = '';
    });
}

function handleMessage(message) {
    let report = JSON.parse(message);

    console.log('Received: ', report);

    if(report.action === 'processList') {
        updateProcessTabs(report.processes);
    }

    if(report.action === 'tokenized') {
        let view = document.getElementById('client-0');
    //	let view = document.getElementById('client-'+report.clientId);
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
        tokensOutput.innerText = JSON.stringify(tokens.filter(v => !v.trivia), null, 4);
    } else
    if(report.action === 'parsed') {
        let view = document.getElementById('client-0');
    //	let view = document.getElementById('client-'+report.clientId);
        if (!view) return;

        let ASTOutput = view.querySelector('.ASTOutput');

        ASTOutput.innerText = JSON.stringify(report.tree, null, 4);
    } else
    if(report.action === 'interpreted') {}

    if(report.action === 'add') {
        let view = document.getElementById('client-0');
    //	let view = document.getElementById('client-'+report.clientId);
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
            sources = ['lexer', 'parser', 'interpreter']

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
        let view = document.getElementById('client-0');
    //	let view = document.getElementById('client-'+report.clientId);
        if (!view) return;

        let consoleOutput = view.querySelector('.consoleOutput');

        let nodes = [...consoleOutput.children]

        for(let node of nodes) {
            if(node.dataset.source === report.source && node.dataset.position*1 > report.position)  {
                node.remove();
            }
        }
    } else
    if(report.action === 'removeAll') {
        let view = document.getElementById('client-0');
    //	let view = document.getElementById('client-'+report.clientId);
        if (!view) return;

        let consoleOutput = view.querySelector('.consoleOutput');

        let sources = ['lexer', 'parser', 'interpreter'],
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

authForm.onsubmit = function(e) {
    e.preventDefault();
    openSocket(socketPath.value.trim(), socketToken.value.trim());
};

function updateProcessTabs(processes) {
    let newIds = new Set(processes.map(p => p.ID));
    let existingTabs = new Map();
    let existingViews = new Map();

    // Сохраняем текущие вкладки и представления
    for (let tab of tabsContainer.children) {
        let id = tab.dataset.id;
        if (id) existingTabs.set(id, tab);
    }
    for (let view of clientViews.children) {
        let id = view.dataset.id;
        if (id) existingViews.set(id, view);
    }

    // Удаляем устаревшие
    for (let [id, tab] of existingTabs) {
        if (!newIds.has(+id)) tab.remove();
    }
    for (let [id, view] of existingViews) {
        if (!newIds.has(+id)) view.remove();
    }

    // Обновляем или создаём вкладки и представления
    processes.forEach(proc => {
        let id = String(proc.ID);

        if (!existingTabs.has(id)) {
            let tab = document.createElement('button');
            tab.innerText = '#'+id;
            tab.dataset.id = id;
            tab.onclick = () => showClient(id);
            tabsContainer.appendChild(tab);
        }

        if (!existingViews.has(id)) {
            let template = document.getElementById('clientTemplate');
            let view = document.createElement('div');
            view.id = 'client-'+id;
            view.dataset.id = id;
            view.style.display = 'none';

            let clone = template.content.cloneNode(true);
            view.appendChild(clone);
            clientViews.appendChild(view);

            initializeClientInterface(view, id);
        }
    });
}

function initializeClientInterface(container, clientId) {
    const inputText = container.querySelector('.inputText');
    const inputLint = container.querySelector('.inputLint');
    const tokensOutput = container.querySelector('.tokensOutput');
    const ASTOutput = container.querySelector('.ASTOutput');
    const compositesOutput = container.querySelector('.compositesOutput');
    const changeTree = container.querySelector('.changeTree');
    const interpret = container.querySelector('.interpret');
    const consoleInput = container.querySelector('.consoleInput');

    inputText.oninput = () => {
        inputLint.innerHTML = '';
        send({ receiverToken: socketToken.value, type: 'notification', action: 'lex', code: inputText.value, clientId });
        send({ receiverToken: socketToken.value, type: 'notification', action: 'parse', clientId });
    };

    inputText.onscroll = () => {
        inputLint.scrollTop = inputText.scrollTop;
        inputLint.scrollLeft = inputText.scrollLeft;
    };

    changeTree.onclick = () => {
        if (tokensOutput.style.display !== 'none') {
            ASTOutput.style.display = '';
            tokensOutput.style.display = compositesOutput.style.display = 'none';
        } else if (ASTOutput.style.display !== 'none') {
            compositesOutput.style.display = '';
            tokensOutput.style.display = ASTOutput.style.display = 'none';
        } else {
            tokensOutput.style.display = '';
            ASTOutput.style.display = compositesOutput.style.display = 'none';
        }
    };

    interpret.onclick = () => {
        send({ receiverToken: socketToken.value, type: 'notification', action: 'interpret', clientId });
    };

    consoleInput.onkeypress = (e) => {
        if (e.key === 'Enter') {
            send({ receiverToken: socketToken.value, type: 'notification', action: 'evaluate', code: consoleInput.value, clientId });
        }
    };
}

function showClient(id) {
    [...clientViews.children].forEach(c => c.style.display = 'none');
    let view = document.getElementById('client-'+id);
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

updateProcessTabs([ { ID: 0 }, { ID: 1 } ]);