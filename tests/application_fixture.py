"""All flag combinations; registration stays inside a loopback-only test binary."""
import http.server
import itertools
import json
import os
import platform
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_): pass
    def do_HEAD(self):
        self.server.heads += 1
        self.send_response(200)
        self.send_header('Content-Length','0')
        self.end_headers()
    def do_POST(self):
        self.server.posts.append((self.path, dict(self.headers), json.loads(self.rfile.read(int(self.headers['Content-Length'])))))
        body = self.server.response
        self.send_response(self.server.status)
        if self.server.status == 307:
            self.send_header('Location', '/redirected-registration')
        self.send_header('Content-Length',str(len(body) + (100 if self.server.incomplete else 0)))
        self.end_headers()
        self.wfile.write(body)
        if self.server.incomplete:
            # Record the full POST first, then close before the advertised body
            # completes. This reproduces ambiguous submission without a 30s wait.
            self.wfile.flush()
            self.close_connection = True

binary=str(Path(sys.argv[1]).resolve())
expected_platform = {'Darwin': '"macOS"', 'Windows': '"Windows"', 'Linux': '"Linux"'}[platform.system()]
with http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler) as server, tempfile.TemporaryDirectory(prefix="itu-application-ü-") as folder:
    server.posts=[]; server.heads=0; server.status=200; server.incomplete=False
    server.response=b'{"ecrnResultList":[{"crn":"001","resultCode":"successResult"}]}'
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    root=Path(folder); (root/'data').mkdir()
    (root/'.env').write_text('ITU_USERNAME=fixture-user\nITU_PASSWORD=fixture-password\n')
    config={'time':{'year':2000,'month':1,'day':1,'hour':0,'minute':0,'second':0,'millisecond':25,'lead_millisecond':0},'courses':{'crn':['001','002'],'scrn':['003']}}
    (root/'data/config.json').write_text(json.dumps(config))
    env={k:v for k,v in os.environ.items() if k not in ('ITU_USERNAME','ITU_PASSWORD','ITU_OBS_USERNAME','ITU_OBS_PASSWORD')}
    origin=f'http://127.0.0.1:{server.server_port}'
    for choices in itertools.product((False,True),repeat=5):
        flags=[flag for enabled,flag in zip(choices,('--logs','--test','--local','--dry-run','--server-time')) if enabled]
        before_posts=len(server.posts);before_heads=server.heads
        result=subprocess.run([binary,origin,*flags],cwd=root,env=env,capture_output=True,text=True, encoding="utf-8",timeout=15)
        assert result.returncode==0,(flags,result.stdout,result.stderr)
        assert len(server.posts)-before_posts == (0 if choices[3] else 1),flags
        assert server.heads-before_heads == (7 if choices[4] else 0),flags
        for secret in ('fixture-password','fixture.header.signature','fixture-user'):
            assert secret not in result.stdout+result.stderr,(flags,secret)
        if not choices[3]:
            path,headers,payload=server.posts[-1]
            assert path=='/api/ders-kayit/v21'
            assert payload=={'ECRN':['001','002'],'SCRN':['003']}
            assert headers['Authorization']=='Bearer fixture.header.signature'
            assert headers['sec-ch-ua-platform'] == expected_platform, headers['sec-ch-ua-platform']

    # --check-clock works in an empty directory without config or credentials.
    before_posts = len(server.posts)
    before_heads = server.heads
    empty = root / 'clock-only'; empty.mkdir()
    clock_res = subprocess.run([binary, origin, '--check-clock'], cwd=empty, env=env,
                               capture_output=True, text=True, encoding="utf-8", timeout=10)
    assert clock_res.returncode == 0, (clock_res.stdout, clock_res.stderr)
    assert '[Clock Diagnostics]' in clock_res.stdout
    assert len(server.posts) == before_posts
    assert server.heads == before_heads

    # Polling mode flag restrictions.
    polling_config = dict(config, courses={'crn': ['001', '002'], 'scrn': []}, polling={
        'enabled': True,
        'start': '2030-01-01T00:00:00Z',
        'end': '2030-01-01T00:05:00Z',
        'max_interval_ms': 60000,
        'expected_interval_ms': 40000,
        'max_attempts': 5,
        'request_budget': {'count': 20, 'window_seconds': 3600}
    })
    (root/'data/config.json').write_text(json.dumps(polling_config))
    res = subprocess.run([binary, origin, '--server-time'], cwd=root, env=env,
                         capture_output=True, text=True, encoding="utf-8", timeout=10)
    assert res.returncode != 0
    assert '--server-time cannot be used with polling' in res.stderr
    res = subprocess.run([binary, origin, '--test'], cwd=root, env=env,
                         capture_output=True, text=True, encoding="utf-8", timeout=10)
    assert res.returncode != 0
    assert 'Submission-enabled --test cannot be used with polling' in res.stderr
    before_polling_posts = len(server.posts)
    res = subprocess.run([binary, origin, '--test', '--dry-run'], cwd=root, env=env,
                         capture_output=True, text=True, encoding="utf-8", timeout=10)
    assert res.returncode == 0, (res.stdout, res.stderr)
    assert 'final request preparation succeeded. No registration request was sent.' in res.stdout
    assert len(server.posts) == before_polling_posts
    # Dry-run must reach HttpSession.prepare's URL policy, not merely build JSON.
    res = subprocess.run([binary, 'ftp://127.0.0.1', '--test', '--dry-run', '--logs'], cwd=root, env=env,
                         capture_output=True, text=True, encoding="utf-8", timeout=10)
    assert res.returncode != 0, (res.stdout, res.stderr)
    assert len(server.posts) == before_polling_posts
    events = [json.loads(line) for line in res.stderr.splitlines()]
    assert events[-1]['event'] == 'stop' and events[-1]['reason'] == 'local_failure', events
    assert all(evt['dry_run'] and evt['test'] and evt['local'] for evt in events)
    # Early failures also preserve the --logs JSON Lines contract.
    for failure_flags in (['--test'], ['--server-time']):
        res = subprocess.run([binary, origin, *failure_flags, '--logs'], cwd=root, env=env,
                             capture_output=True, text=True, encoding="utf-8", timeout=10)
        assert res.returncode != 0
        events = [json.loads(line) for line in res.stderr.splitlines()]
        assert events[-1]['reason'] == 'invalid_polling_mode', events
    # Restore non-polling config for remainder of tests.
    (root/'data/config.json').write_text(json.dumps(config))

    # Future targets prove the branch behavior that past-target smoke cases
    # cannot exercise. Only wall/scheduled waits are faked; HTTP stays loopback.
    future_config = {'time': {'year': 2030, 'month': 1, 'day': 1, 'hour': 0,
                             'minute': 0, 'second': 0, 'milisecond': 37,
                             'lead_milisecond': 11}, 'courses': config['courses']}
    (root/'data/config.json').write_text(json.dumps(future_config))
    future_env = dict(env, ITU_FIXTURE_FUTURE='1')
    for test, server_time, dry_run in itertools.product((False, True), repeat=3):
        flags = [flag for enabled, flag in zip((test, server_time, dry_run),
                 ('--test', '--server-time', '--dry-run')) if enabled]
        before_posts, before_heads = len(server.posts), server.heads
        result = subprocess.run([binary, origin, *flags], cwd=root, env=future_env,
                                capture_output=True, text=True, encoding="utf-8", timeout=15)
        assert result.returncode == 0, (flags, result.stdout, result.stderr)
        trace_line = next(line for line in result.stdout.splitlines()
                          if line.startswith('FIXTURE_SCHEDULE='))
        trace = json.loads(trace_line.split('=', 1)[1])
        assert len(server.posts) - before_posts == (0 if dry_run else 1), flags
        assert server.heads - before_heads == ((7 if test else 14) if server_time else 0), flags
        assert trace['resync_seconds'] == (30 if not test and server_time else 0), trace
        assert trace['token_waits'] == (0 if test else 1), trace
        assert trace['token_time'] == (-120 if test else -60), trace
        expected_events = []
        if not test and server_time:
            expected_events.append('resync_wait')
        if not test:
            expected_events.append('token_wait')
            assert trace['token_deadline'] == -60, trace
        expected_events.append('acquire_token')
        if not test and not dry_run:
            expected_events.append('final_wait')
            assert trace['final_deadline_ms'] == 26, trace
        assert trace['events'] == expected_events, trace
        assert trace['final_waits'] == (1 if not test and not dry_run else 0), trace

    # Wall steps, cancellation, and changed synchronization state after login
    # must all prevent scheduled submission at the final dispatch boundary.
    for variable, reason in [('ITU_FIXTURE_CLOCK_STEP', 'clock_discontinuity'),
                             ('ITU_FIXTURE_CANCEL_FINAL', 'interrupted'),
                             ('ITU_FIXTURE_UNSYNCHRONIZED', 'clock_unsynchronized')]:
        before = len(server.posts)
        result = subprocess.run([binary, origin, '--logs'], cwd=root,
                                env=dict(future_env, **{variable: '1'}), capture_output=True,
                                text=True, encoding="utf-8", timeout=10)
        assert result.returncode != 0, (variable, result.stdout, result.stderr)
        assert len(server.posts) == before, variable
        events = [json.loads(line) for line in result.stderr.splitlines()]
        assert events[-1]['reason'] == reason, events

    # HTTP-Date sampling does not override a negative network-time health report.
    result = subprocess.run([binary, origin, '--server-time', '--logs'], cwd=root,
                            env=dict(future_env, ITU_FIXTURE_UNSYNCHRONIZED='1'), capture_output=True,
                            text=True, encoding='utf-8', timeout=15)
    assert result.returncode != 0
    assert len(server.posts) == before
    assert json.loads(result.stderr.splitlines()[-1])['reason'] == 'clock_unsynchronized'

    # Monotonic waits tolerate a small correction but must recheck the local
    # absolute target before sending, rather than submit 200ms too early.
    result = subprocess.run([binary, origin, '--logs'], cwd=root,
                            env=dict(future_env, ITU_FIXTURE_SMALL_CORRECTION='1'), capture_output=True,
                            text=True, encoding='utf-8', timeout=10)
    assert result.returncode == 0, (result.stdout, result.stderr)
    trace = json.loads(next(line.split('=', 1)[1] for line in result.stdout.splitlines()
                            if line.startswith('FIXTURE_SCHEDULE=')))
    assert trace['finish_wall_ms'] >= 26, trace

    # Exercise actual account fallback through run_application/acquire_token.
    config['account'] = {'username': 'fixture-user', 'password': 'fixture-password'}
    (root/'data/config.json').write_text(json.dumps(config))
    (root/'.env').unlink()
    before_posts = len(server.posts)
    result = subprocess.run([binary, origin, '--test', '--local', '--dry-run'],
                            cwd=root, env=env, capture_output=True, text=True, encoding="utf-8", timeout=10)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert len(server.posts) == before_posts
    # Restore file credentials and remove fallback before the failure cases.
    del config['account']
    (root/'data/config.json').write_text(json.dumps(config))
    (root/'.env').write_text('ITU_USERNAME=fixture-user\nITU_PASSWORD=fixture-password\n')
    for status,body in [(307,b'synthetic-private-response'),(503,b'synthetic-private-response'),(200,b'synthetic-private-response'),(200,b'{}'),(200,b'')]:
        server.status=status; server.response=body;before=len(server.posts)
        result=subprocess.run([binary,origin,'--test','--local','--logs'],cwd=root,env=env,capture_output=True,text=True, encoding="utf-8",timeout=10)
        assert result.returncode!=0
        assert len(server.posts)==before+1,'application retried submission'
        assert 'synthetic-private-response' not in result.stdout+result.stderr
        assert 'Registration outcome is unknown' not in result.stdout+result.stderr
        if status == 200:
            assert '[Result] Server Response Code: 200' in result.stdout
    # A fully received per-course rejection remains a normal reported result,
    # alongside successes from that same batch; it is not a transport failure.
    server.status = 200
    server.response = json.dumps({'ecrnResultList': [
        {'crn': '001', 'resultCode': 'successResult'},
        {'crn': '002', 'resultCode': 'VAL06'},
    ]}).encode()
    before = len(server.posts)
    result = subprocess.run([binary, origin, '--test', '--local', '--logs'],
                            cwd=root, env=env, capture_output=True, text=True, encoding="utf-8", timeout=10)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert len(server.posts) == before + 1
    assert 'CRN 001 için işlem başarıyla tamamlandı.' in result.stdout
    assert 'CRN 002 kontenjan yetersizliğinden dolayı alınamadı.' in result.stdout
    assert 'Registration outcome is unknown' not in result.stdout + result.stderr
    assert '\n>>> FIRING REGISTRATION REQUEST <<<\n' in result.stdout

    # Incomplete response after receiving the batch: do not infer that zero,
    # some, or all changes were applied, and never submit the batch again.
    server.incomplete = True
    server.response = b'synthetic-private-response fixture-password fixture.header.signature'
    before = len(server.posts)
    result = subprocess.run([binary, origin, '--test', '--local', '--logs'],
                            cwd=root, env=env, capture_output=True, text=True, encoding="utf-8", timeout=10)
    assert result.returncode != 0
    assert len(server.posts) == before + 1, 'application retried ambiguous submission'
    assert server.posts[-1][2] == {'ECRN': ['001', '002'], 'SCRN': ['003']}
    combined = result.stdout + result.stderr
    assert 'Registration outcome is unknown' in combined
    assert 'OBS may have applied some or all changes' in combined
    assert 'Check your registered courses in OBS before retrying' in combined
    assert 'No automatic retry was attempted' in combined
    assert 'curl_code=' in combined and 'http_status=200' in combined, combined
    assert json.loads(result.stderr.splitlines()[-1])['reason'] == 'transport_failure'
    assert '[Result] Server Response Code:' not in combined
    for secret in ('synthetic-private-response', 'fixture-password', 'fixture.header.signature'):
        assert secret not in combined
    server.shutdown();thread.join()
print('32 flag combinations, 8 monotonic future schedules, cancellation/clock guards, dry-run preparation, JSON diagnostics, account fallback and ambiguous submission failures passed (loopback only).')
