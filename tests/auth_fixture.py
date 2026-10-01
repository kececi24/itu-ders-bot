#!/usr/bin/env python3
"""Offline authentication sequence, form and failure fixtures (no OBS traffic)."""
import http.server
import subprocess
import sys
import threading
import urllib.parse

TOKEN = 'eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxIn0.signature'

class Fixture(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, *args):
        pass
    def reply(self, status=200, body='', headers=()):
        data = body.encode()
        self.send_response(status)
        for name, value in headers:
            self.send_header(name, value)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)
    def do_GET(self):
        self.server.paths.append(('GET', self.path))
        scenario = self.server.scenario
        if self.path == '/':
            if scenario == 'root_http': return self.reply(503, 'private response')
            if scenario == 'root_disconnect':
                self.close_connection = True
                return
            return self.reply(302, headers=[('Location', '/auth/Login.aspx?subSessionId=fixture-session'), ('Set-Cookie', 'handshake=ok; Path=/')])
        if self.path.startswith('/auth/Login.aspx?subSessionId='):
            action = {'query_action': '?subSessionId=fixture-session&step=login', 'absolute_action': '/auth/Login.aspx?subSessionId=fixture-session&step=login'}.get(scenario, './Login.aspx?subSessionId=fixture-session&step=login')
            if scenario == 'absolute_action': action = f'http://127.0.0.1:{self.server.server_port}' + action
            fields = "<input value='a&amp;b+/&#X2D;' name='__VIEWSTATE'><input name='__VIEWSTATEGENERATOR' value='vsg'><input value='ev' id='__EVENTVALIDATION'>"
            if scenario == 'missing_field': fields = fields.replace("id='__EVENTVALIDATION'", "id='other'")
            return self.reply(body=f"<form method='post' action='{action.replace('&', '&amp;')}'>{fields}</form>")
        if self.path == '/Login.aspx?identityGuid=first&state=fixture-session':
            self.server.check(self.headers.get('Cookie', '').find('loggedin=yes') >= 0, 'identity cookie missing')
            return self.reply(body='selected')
        if self.path == '/ogrenci/':
            self.server.check('loggedin=yes' in self.headers.get('Cookie', ''), 'dashboard cookie missing')
            return self.reply(body='dashboard')
        if self.path == '/ogrenci/auth/jwt':
            self.server.check(self.headers.get('X-Requested-With') == 'XMLHttpRequest', 'JWT AJAX header')
            self.server.check(self.headers.get('Accept') == 'application/json, text/plain, */*', 'JWT Accept header')
            if scenario == 'jwt_http': return self.reply(403, 'private response')
            if scenario == 'jwt_html': return self.reply(body='<!DOCTYPE html>fixture-password')
            if scenario == 'jwt_empty': return self.reply()
            if scenario == 'jwt_invalid': return self.reply(body='not-a-jwt-at-all-private-response')
            return self.reply(body='\n' + TOKEN + '\n')
        self.server.errors.append('Unexpected GET ' + self.path)
        self.reply(404)
    def do_POST(self):
        self.server.paths.append(('POST', self.path))
        raw = self.rfile.read(int(self.headers['Content-Length'])).decode()
        self.server.check(self.path == '/auth/Login.aspx?subSessionId=fixture-session&step=login', 'action query/path lost')
        self.server.check(self.headers.get('Content-Type') == 'application/x-www-form-urlencoded', 'form Content-Type')
        self.server.check(self.headers.get('Referer', '').endswith('/auth/Login.aspx?subSessionId=fixture-session'), 'Referer')
        self.server.check('handshake=ok' in self.headers.get('Cookie', ''), 'redirect cookie missing')
        expected = [('__VIEWSTATE', 'a&b+/-'), ('__VIEWSTATEGENERATOR', 'vsg'), ('__EVENTVALIDATION', 'ev'), ('ctl00$ContentPlaceHolder1$tbUserName', 'test ü&+'), ('ctl00$ContentPlaceHolder1$tbPassword', 'fixture-password\t&+'), ('ctl00$ContentPlaceHolder1$btnLogin', 'Giriş / Login')]
        self.server.check(urllib.parse.parse_qsl(raw) == expected, 'form order/values')
        self.server.check('%09' in raw and '%C3%BC' in raw and '%2B' in raw, 'UTF8/reserved encoding')
        scenario = self.server.scenario
        if scenario == 'login_http': return self.reply(401, 'private response')
        if scenario == 'rejected_login': return self.reply(body='<input name="ctl00$ContentPlaceHolder1$tbPassword">')
        if scenario == 'missing_identity': return self.reply(body='SelectIdentity')
        body = 'welcome'
        if scenario == 'identity': body = 'SelectIdentity <a href="/Login.aspx?identityGuid=first&amp;state=fixture-session">student</a><a href="/Login.aspx?identityGuid=second">other</a>'
        self.reply(body=body, headers=[('Set-Cookie', 'loggedin=yes; Path=/')])

def main():
    scenarios = ('plain', 'identity', 'query_action', 'absolute_action', 'missing_field', 'rejected_login', 'missing_identity', 'login_http', 'jwt_http', 'jwt_html', 'jwt_empty', 'jwt_invalid', 'root_http', 'root_disconnect')
    for scenario in scenarios:
        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture)
        server.daemon_threads = True
        server.scenario, server.paths, server.errors = scenario, [], []
        server.check = lambda valid, message: None if valid else server.errors.append(message)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            success = scenario in ('plain', 'identity', 'query_action', 'absolute_action')
            run = subprocess.run([sys.argv[1], f'http://127.0.0.1:{server.server_port}/', 'success' if success else 'failure'], text=True, encoding="utf-8", capture_output=True, timeout=10)
            assert run.returncode == 0, (scenario, run.stdout, run.stderr)
            assert not server.errors, (scenario, server.errors)
            output = run.stdout + run.stderr
            for secret in ('fixture-password', 'fixture-session', TOKEN, 'private response'):
                assert secret not in output, (scenario, 'sensitive diagnostic')
            paths = [path for method, path in server.paths]
            if success:
                expected = ['/', '/auth/Login.aspx?subSessionId=fixture-session', '/auth/Login.aspx?subSessionId=fixture-session&step=login']
                if scenario == 'identity': expected += ['/Login.aspx?identityGuid=first&state=fixture-session']
                expected += ['/ogrenci/', '/ogrenci/auth/jwt']
                assert paths == expected, (scenario, paths)
            elif scenario in ('root_http', 'root_disconnect'): assert len(paths) == 1
            elif scenario == 'missing_field': assert len(paths) == 2
            elif scenario in ('rejected_login', 'missing_identity', 'login_http'): assert len(paths) == 3
        finally:
            server.shutdown()
            server.server_close()
    print(f'{len(scenarios)} offline authentication scenarios passed')

if __name__ == '__main__':
    main()
