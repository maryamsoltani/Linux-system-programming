import socket, json

HOST, PORT = '127.0.0.1', 9000

def send_msg(s, obj):
    s.send((json.dumps(obj) + '\n').encode())

def recv_msg(s, timeout=5.0):
    s.settimeout(timeout)
    data = b''
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk: break
            data += chunk
            if b'\n' in chunk: break
    except socket.timeout:
        pass
    if not data: return None
    try:
        return json.loads(data.decode().strip())
    except Exception:
        return data.decode()

def mocked_model_decision(user_request, tools):
    lower = user_request.lower()
    if 'time' in lower:
        return {'tool': 'get_time', 'args': {}}
    if 'delet' in lower or 'old' in lower:
        return {'tool': 'delete_older_than_days', 'args': {'path': '.', 'days': 30}}
    return {'tool': 'list_files', 'args': {'path': '.'}}

def agent_flow(user_request):
    print(f'\n--- Request: "{user_request}" ---')
    s = socket.socket()
    s.connect((HOST, PORT))

    send_msg(s, {'id': 1, 'method': 'list_tools', 'params': {}})
    tools = recv_msg(s)
    print('Available tools:', [t['name'] for t in tools['result']['tools']])

    decision = mocked_model_decision(user_request, tools)
    print(f'Model chose   : {decision["tool"]}({decision["args"]})')

    send_msg(s, {'id': 10, 'method': 'call_tool',
                 'params': {'tool': decision['tool'], 'args': decision['args']}})
    result = recv_msg(s)
    print('Result        :', json.dumps(result, indent=2))
    s.close()

if __name__ == '__main__':
    # Basic client test
    s = socket.socket()
    s.connect((HOST, PORT))
    send_msg(s, {'id': 1, 'method': 'initialize', 'params': {}})
    print('initialize ->', recv_msg(s))
    send_msg(s, {'id': 2, 'method': 'list_tools', 'params': {}})
    print('list_tools ->', recv_msg(s))
    send_msg(s, {'id': 3, 'method': 'call_tool', 'params': {'tool': 'list_files', 'args': {'path': '.'}}})
    print('list_files ->', recv_msg(s))
    send_msg(s, {'id': 4, 'method': 'call_tool', 'params': {'tool': 'get_time', 'args': {}}})
    print('get_time   ->', recv_msg(s))
    s.close()

    # Agent demo
    agent_flow('List all source files in the current directory')
    agent_flow('What time is it on the server?')
    agent_flow('Delete files older than 30 days')
