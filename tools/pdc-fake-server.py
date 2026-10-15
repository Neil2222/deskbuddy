#!/usr/bin/env python3
"""A tiny stand-in for PDC Manager's "Ze Doen Maar Wat" (ZDMW), to try the companion without Teams.

Never point the companion at the real https://pdcmanager to test clicks: every click there posts a real Teams message.
This server only counts in memory and prints a line instead.

    python tools/pdc-fake-server.py                  # http://127.0.0.1:8765
    $env:DESKBUDDY_PDC_URL = 'http://127.0.0.1:8765' # PowerShell, then start the companion from that window:
    companion\\publish\\DeskBuddy.Companion.exe       # (the companion takes the override only for 127.0.0.1/localhost/::1)

Log in with any user name and the password "test" (user "nopass" acts as an account without a password yet).
The session is the cookie session=fake; it is stored per server, so it is never used for the real PDC Manager.

Simulating trouble (environment variables at start, or at run time: GET /fake/mode?click=403&access=read&slow=15):
    FAKE_PDC_CLICK   ok (default) | 401 (session expired) | 403 (no permission) | 500 | slow (answers after
                     FAKE_PDC_SLOW seconds, default 15: the companion gives up after 10 s, "timeout", yet it counted)
                     | drop (counts, then closes the connection without an answer: "unknown")
    FAKE_PDC_ACCESS  write (default) | read | none   (blueprint_access zdmw in the profile)
    FAKE_PDC_SLOW    seconds for click=slow
    FAKE_PDC_PORT    port (default 8765)
GET /fake/mode without arguments shows the current settings. Only 127.0.0.1 is listened on.
"""
import json
import os
import sys
import time
from http.cookies import SimpleCookie
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

STATE = {
    'count': 0,
    'user': None,  # who logged in last (one fake session for everyone)
    'click': os.environ.get('FAKE_PDC_CLICK', 'ok'),
    'access': os.environ.get('FAKE_PDC_ACCESS', 'write'),
    'slow': float(os.environ.get('FAKE_PDC_SLOW', '15')),
}
SESSION = 'fake'


class Handler(BaseHTTPRequestHandler):
    server_version = 'FakePdcManager/1'

    def log_message(self, fmt, *args):  # one short line per request, without headers or bodies
        sys.stderr.write('%s %s\n' % (time.strftime('%H:%M:%S'), fmt % args))

    def _send(self, status, body=b'', content_type='application/json', headers=()):
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode('utf-8')
        elif isinstance(body, str):
            body = body.encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(body)))
        for name, value in headers:
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)

    def _logged_in(self):
        jar = SimpleCookie(self.headers.get('Cookie', ''))
        return 'session' in jar and jar['session'].value == SESSION and STATE['user'] is not None

    def _body(self):
        length = int(self.headers.get('Content-Length') or 0)
        return self.rfile.read(length) if length else b''

    def _need_login(self):
        self._send(401, {'success': False, 'error': 'Niet ingelogd'})

    def _access(self):
        return {'write': 'write', 'read': 'read'}.get(STATE['access'])

    def do_GET(self):
        url = urlsplit(self.path)
        if url.path == '/health':
            return self._send(200, {'status': 'ok', 'fake': True})
        if url.path == '/fake/mode':
            for key, values in parse_qs(url.query).items():
                if key in ('click', 'access'):
                    STATE[key] = values[-1]
                elif key == 'slow':
                    STATE['slow'] = float(values[-1])
                elif key == 'count':
                    STATE['count'] = int(values[-1])
            print('fake mode: click=%(click)s access=%(access)s slow=%(slow)ss count=%(count)d' % STATE, flush=True)
            return self._send(200, {k: STATE[k] for k in ('click', 'access', 'slow', 'count', 'user')})
        if url.path == '/auth/login':
            return self._send(200, '<form method="post"><input name="username"><input name="password"></form>', 'text/html')
        if url.path == '/auth/api/me/profile':
            if not self._logged_in():
                return self._need_login()
            access = {'todo': 'write'}
            if self._access():
                access['zdmw'] = self._access()
            return self._send(200, {'success': True, 'profile': {
                'username': STATE['user'], 'display_name': STATE['user'].title() + ' (fake)', 'role': 'user',
                'blueprint_access': access}})
        if url.path == '/zdmw/api/count':
            if not self._logged_in():
                return self._need_login()
            if not self._access():
                return self._send(403, {'success': False, 'error': 'Onvoldoende rechten'})
            return self._send(200, {'success': True, 'count': STATE['count']})
        self._send(404, {'success': False, 'error': 'Not found'})

    def do_POST(self):
        url = urlsplit(self.path)
        body = self._body()
        if url.path == '/auth/login':
            form = parse_qs(body.decode('utf-8'))
            user = (form.get('username') or [''])[0].strip()
            password = (form.get('password') or [''])[0]
            if user == 'nopass':
                return self._send(200, '<form action="/auth/set_password">Kies een wachtwoord</form>', 'text/html')
            if not user or password != 'test':
                print('login refused for %r' % user, flush=True)
                return self._send(401, '<p>Ongeldige gebruikersnaam of wachtwoord</p>', 'text/html')
            STATE['user'] = user
            print('logged in: %s' % user, flush=True)
            return self._send(302, b'', 'text/html', [
                ('Location', '/'), ('Set-Cookie', 'session=%s; Path=/; HttpOnly; SameSite=Lax' % SESSION)])
        if url.path == '/zdmw/api/click':
            mode = STATE['click']
            if mode == '401' or not self._logged_in():
                return self._need_login()
            if mode == '403' or self._access() != 'write':
                return self._send(403, {'success': False, 'error': 'Onvoldoende rechten'})
            if mode == '500':
                return self._send(500, '<h1>Internal Server Error</h1>', 'text/html')
            try:
                username = json.loads(body.decode('utf-8')).get('username')
            except ValueError:
                return self._send(400, {'success': False, 'error': 'Ongeldige JSON'})
            if username != STATE['user']:
                print('WARNING: click for %r, but %r is logged in' % (username, STATE['user']), flush=True)
            STATE['count'] += 1
            print('>>> ZDMW #%d by %s (fake: no Teams message)' % (STATE['count'], username), flush=True)
            if mode == 'slow':
                time.sleep(STATE['slow'])
            elif mode == 'drop':
                self.close_connection = True
                return
            return self._send(200, {'success': True, 'count': STATE['count'], 'webhook_sent': True,
                                    'webhook_error': None, 'image_url': '/static/zdmw/fake.png'})
        self._send(404, {'success': False, 'error': 'Not found'})


def main():
    port = int(os.environ.get('FAKE_PDC_PORT', '8765'))
    server = ThreadingHTTPServer(('127.0.0.1', port), Handler)
    print('Fake PDC Manager on http://127.0.0.1:%d (click=%s, access=%s). Ctrl+C stops.'
          % (port, STATE['click'], STATE['access']), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
