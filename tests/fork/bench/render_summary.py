#!/usr/bin/env python3
import json
import pathlib
import sys

CASE_ORDER = [
    'dynamic_get_logged',
    'static_get_logged',
    'static_head_logged',
    'upload_4k_logged',
    'upload_256k_logged',
    'dynamic_get_no_logging',
]

CASE_LABELS = {
    'dynamic_get_logged': 'dynamic GET logged',
    'static_get_logged': 'static-map GET logged',
    'static_head_logged': 'static-map HEAD logged',
    'upload_4k_logged': 'upload POST 4 KiB logged',
    'upload_256k_logged': 'upload POST 256 KiB logged',
    'dynamic_get_no_logging': 'dynamic GET logging disabled',
}


def fmt(value):
    return f'{value:.2f}'



def main():
    if len(sys.argv) != 3:
        raise SystemExit('usage: render_summary.py <artifact_dir> <output_md>')
    artifact_dir = pathlib.Path(sys.argv[1])
    output_md = pathlib.Path(sys.argv[2])
    rows = []
    aggregate = {}
    for case_name in CASE_ORDER:
        result_path = artifact_dir / case_name / 'result.json'
        if not result_path.exists():
            continue
        data = json.loads(result_path.read_text(encoding='utf-8'))
        aggregate[case_name] = data
        rows.append((case_name, data))

    lines = []
    lines.append('# Y1 local measurement baseline')
    lines.append('')
    lines.append('| Case | Requests | Req/s | Mean ms | P50 ms | P95 ms | Max ms | Notes |')
    lines.append('| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |')
    for case_name, data in rows:
        latency = data['latency_ms']
        notes = []
        if data['body_bytes']:
            notes.append(f"body={data['body_bytes']}B")
        if case_name == 'dynamic_get_no_logging':
            notes.append('comparison-only')
        lines.append(
            '| {label} | {requests} | {rps} | {mean} | {p50} | {p95} | {maxv} | {notes} |'.format(
                label=CASE_LABELS.get(case_name, case_name),
                requests=data['requests'],
                rps=fmt(data['requests_per_second']),
                mean=fmt(latency['mean']),
                p50=fmt(latency['p50']),
                p95=fmt(latency['p95']),
                maxv=fmt(latency['max']),
                notes=', '.join(notes) or '-',
            )
        )

    if 'dynamic_get_logged' in aggregate and 'dynamic_get_no_logging' in aggregate:
        logged = aggregate['dynamic_get_logged']['requests_per_second']
        nolog = aggregate['dynamic_get_no_logging']['requests_per_second']
        if logged > 0:
            delta = ((nolog - logged) / logged) * 100.0
            lines.append('')
            lines.append(
                'Logging comparison: dynamic GET with request logging disabled measured {delta}% req/s versus the logged baseline.'.format(
                    delta=fmt(delta)
                )
            )

    output_md.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    summary_json = artifact_dir / 'summary.json'
    summary_json.write_text(json.dumps(aggregate, indent=2, sort_keys=True) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
