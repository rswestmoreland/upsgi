#!/usr/bin/env python3
import http.client
import os
import pathlib
import socket
import subprocess
import sys
import time

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
REPO_DIR = SCRIPT_DIR.parent.parent.parent
UPSGI_BIN = pathlib.Path(os.environ.get('UPSGI_BIN', str(REPO_DIR / 'upsgi')))
ARTIFACT_DIR = pathlib.Path(os.environ.get('UPSGI_BENCH_ARTIFACT_DIR', str(REPO_DIR / 'tests/fork/artifacts/bench_y1_baseline')))
STATIC_ROOT = REPO_DIR / 'tests/fork/fixtures/static'
APP_SIMPLE = REPO_DIR / 'tests/fork/fixtures/apps/app_simple.psgi'
APP_UPLOAD = REPO_DIR / 'tests/fork/fixtures/apps/app_upload.psgi'
REQUESTS_GET = int(os.environ.get('UPSGI_BENCH_REQUESTS_GET', '200'))
WARMUP_GET = int(os.environ.get('UPSGI_BENCH_WARMUP_GET', '20'))
REQUESTS_UPLOAD_SMALL = int(os.environ.get('UPSGI_BENCH_REQUESTS_UPLOAD_SMALL', '80'))
WARMUP_UPLOAD_SMALL = int(os.environ.get('UPSGI_BENCH_WARMUP_UPLOAD_SMALL', '10'))
REQUESTS_UPLOAD_LARGE = int(os.environ.get('UPSGI_BENCH_REQUESTS_UPLOAD_LARGE', '20'))
WARMUP_UPLOAD_LARGE = int(os.environ.get('UPSGI_BENCH_WARMUP_UPLOAD_LARGE', '5'))

CASES = [
    {
        'name': 'dynamic_get_logged',
        'app': APP_SIMPLE,
        'method': 'GET',
        'path': '/bench/dynamic',
        'requests': REQUESTS_GET,
        'warmup': WARMUP_GET,
        'body_bytes': 0,
        'expect_contains': 'path=/bench/dynamic',
        'expect_body_bytes': None,
        'disable_logging': False,
    },
    {
        'name': 'static_get_logged',
        'app': APP_SIMPLE,
        'method': 'GET',
        'path': '/assets/hello.txt',
        'requests': REQUESTS_GET,
        'warmup': WARMUP_GET,
        'body_bytes': 0,
        'expect_contains': 'hello from upsgi static map',
        'expect_body_bytes': None,
        'disable_logging': False,
    },
    {
        'name': 'static_head_logged',
        'app': APP_SIMPLE,
        'method': 'HEAD',
        'path': '/assets/hello.txt',
        'requests': REQUESTS_GET,
        'warmup': WARMUP_GET,
        'body_bytes': 0,
        'expect_contains': None,
        'expect_body_bytes': 0,
        'disable_logging': False,
    },
    {
        'name': 'upload_4k_logged',
        'app': APP_UPLOAD,
        'method': 'POST',
        'path': '/upload',
        'requests': REQUESTS_UPLOAD_SMALL,
        'warmup': WARMUP_UPLOAD_SMALL,
        'body_bytes': 4096,
        'expect_contains': None,
        'expect_body_bytes': 4096,
        'disable_logging': False,
    },
    {
        'name': 'upload_256k_logged',
        'app': APP_UPLOAD,
        'method': 'POST',
        'path': '/upload',
        'requests': REQUESTS_UPLOAD_LARGE,
        'warmup': WARMUP_UPLOAD_LARGE,
        'body_bytes': 262144,
        'expect_contains': None,
        'expect_body_bytes': 262144,
        'disable_logging': False,
    },
    {
        'name': 'dynamic_get_no_logging',
        'app': APP_SIMPLE,
        'method': 'GET',
        'path': '/bench/nolog',
        'requests': REQUESTS_GET,
        'warmup': WARMUP_GET,
        'body_bytes': 0,
        'expect_contains': 'path=/bench/nolog',
        'expect_body_bytes': None,
        'disable_logging': True,
    },
]


def choose_port():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(('127.0.0.1', 0))
    port = sock.getsockname()[1]
    sock.close()
    return port



def wait_http(port, path='/', attempts=120, sleep_seconds=0.10):
    for _ in range(attempts):
        try:
            conn = http.client.HTTPConnection('127.0.0.1', port, timeout=1.0)
            conn.request('GET', path)
            resp = conn.getresponse()
            resp.read()
            conn.close()
            return True
        except Exception:
            time.sleep(sleep_seconds)
    return False



def write_environment():
    ARTIFACT_DIR.mkdir(parents=True, exist_ok=True)
    lines = [
        f"date_utc={time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}",
        f"python3={sys.version.split()[0]}",
        f"upsgi_version={subprocess.check_output([str(UPSGI_BIN), '--version'], text=True).strip()}",
        f"requests_get={REQUESTS_GET}",
        f"warmup_get={WARMUP_GET}",
        f"requests_upload_small={REQUESTS_UPLOAD_SMALL}",
        f"warmup_upload_small={WARMUP_UPLOAD_SMALL}",
        f"requests_upload_large={REQUESTS_UPLOAD_LARGE}",
        f"warmup_upload_large={WARMUP_UPLOAD_LARGE}",
    ]
    (ARTIFACT_DIR / 'environment.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')



def stop_process(proc):
    if proc.poll() is not None:
        return
    proc.terminate()
    deadline = time.time() + 5.0
    while time.time() < deadline:
        if proc.poll() is not None:
            return
        time.sleep(0.10)
    proc.kill()
    proc.wait(timeout=5.0)



def start_case(case, port):
    case_dir = ARTIFACT_DIR / case['name']
    case_dir.mkdir(parents=True, exist_ok=True)
    pid_file = case_dir / 'server.pid'
    log_file = case_dir / 'server.log'
    stdout_file = case_dir / 'stdout.log'
    cmd = [
        str(UPSGI_BIN),
        '--master',
        '--workers', '1',
        '--need-app',
        '--strict',
        '--vacuum',
        '--die-on-term',
        '--http-socket', f':{port}',
        '--psgi', str(case['app']),
        '--static-map', f'/assets={STATIC_ROOT}',
        '--logto', str(log_file),
        '--pidfile', str(pid_file),
    ]
    if case['disable_logging']:
        cmd.append('--disable-logging')
    (case_dir / 'launch.cmd').write_text(' '.join(cmd) + '\n', encoding='utf-8')
    out = open(stdout_file, 'w', encoding='utf-8')
    proc = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, cwd=str(REPO_DIR))
    for _ in range(100):
        if pid_file.exists() and pid_file.stat().st_size > 0:
            break
        if proc.poll() is not None:
            break
        time.sleep(0.10)
    if not pid_file.exists() or pid_file.stat().st_size == 0:
        out.close()
        raise RuntimeError(f'pid file not created for {case["name"]}')
    if not wait_http(port):
        out.close()
        raise RuntimeError(f'HTTP socket did not become ready for {case["name"]}')
    return proc, out, case_dir



def run_case(case):
    port = choose_port()
    proc, out_handle, case_dir = start_case(case, port)
    try:
        cmd = [
            sys.executable,
            str(SCRIPT_DIR / 'http_bench.py'),
            '--label', case['name'],
            '--host', '127.0.0.1',
            '--port', str(port),
            '--method', case['method'],
            '--path', case['path'],
            '--requests', str(case['requests']),
            '--warmup', str(case['warmup']),
            '--timeout', '5.0',
            '--body-bytes', str(case['body_bytes']),
            '--expect-status', '200',
            '--output-json', str(case_dir / 'result.json'),
        ]
        if case['expect_contains']:
            cmd.extend(['--expect-contains', case['expect_contains']])
        if case['expect_body_bytes'] is not None:
            cmd.extend(['--expect-body-bytes', str(case['expect_body_bytes'])])
        subprocess.run(cmd, check=True, cwd=str(REPO_DIR))
    finally:
        stop_process(proc)
        out_handle.close()



def main():
    if not UPSGI_BIN.exists():
        raise SystemExit(f'missing executable upsgi binary: {UPSGI_BIN}')
    write_environment()
    for case in CASES:
        run_case(case)
    subprocess.run(
        [sys.executable, str(SCRIPT_DIR / 'render_summary.py'), str(ARTIFACT_DIR), str(ARTIFACT_DIR / 'summary.md')],
        check=True,
        cwd=str(REPO_DIR),
    )
    print(f'captured Y1 baseline in {ARTIFACT_DIR}')


if __name__ == '__main__':
    main()
