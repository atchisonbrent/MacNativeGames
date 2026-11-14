#!/usr/bin/env python3
"""Temporarily revert fixes/introduce controls, build, test, restore exact bytes.
Run only in an otherwise idle worktree; this deliberately edits ../nvn_mtl.m.
All build outputs, diffs, and logs stay in the Makefile's scratch directory.
"""
import difflib
import json
import os
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parent / 'nvn_mtl.m'
OUT = Path(os.environ.get('OUT') or HERE / '.build') / 'present-handoff'
BIN = OUT / 't_present_handoff'


def replace_once(text, old, new):
    assert text.count(old) == 1, (old, text.count(old))
    return text.replace(old, new, 1)


def swap_rescue_wish(text):
    start = text.index('    if (g_rescue_mode && now_ns() >= g_rescue_back_at)')
    wish = text.index('    if (!g_rescue_mode) {', start)
    end = text.index('    int want = g_dm_present_link;', wish)
    return text[:start] + text[wish:end] + text[start:wish] + text[end:]


def old_save(text):
    text = replace_once(text, 'static int host_on, host_saved;', 'static int host_on, host_saved = 1;')
    return replace_once(text, 'if (!host_on) host_saved = g_dm_present_link;',
                        'if (!host_on && g_dm_present_link) host_saved = g_dm_present_link;')


def mutation(old, new):
    return lambda text: replace_once(text, old, new)


MUTATIONS = [
    ('1-stale', mutation('want == applied && !(!want && g_link_started)', 'want == applied'), [('stale', 1)]),
    ('2-delegate', mutation('if (g_pq_read == g_pq_in) { pthread_mutex_unlock(&g_pq_mu); return; }',
                           'if (!g_dm_present_link || g_pq_read == g_pq_in) { pthread_mutex_unlock(&g_pq_mu); return; }'), [('delegate', 1)]),
    ('3-order', swap_rescue_wish, [('rescue', 1)]),
    ('3-guard', mutation('if (!g_rescue_mode) {   /* the frontend\'s wish',
                        '{   /* the frontend\'s wish'), [('rescue', 1)]),
    ('4-holds', mutation('if (host && (!host_on || g_dm_present_link))', 'if (host && !host_on)'), [('holds', 1)]),
    ('5-saved-zero', old_save, [('configured', 0), ('configured', 1), ('configured', 2)]),
    # Modes 1/2 were already restored by f7b2107c; this additional control removes
    # restoration altogether. It is not claimed as a historical revert.
    ('5-no-restore-control', mutation('g_dm_present_link = host_saved;', 'g_dm_present_link = 0;'), [('configured', 0), ('configured', 1), ('configured', 2)]),
    ('6-power', mutation('atomic_load(&g_present_link_host) == 0 && mtl_fps_target() != 30',
                        'atomic_load(&g_present_link_host) == 0'), [('power', 1)]),
    ('7-clock', mutation('        pthread_mutex_lock(&g_pq_mu); g_stall_t0 = 0; g_link_tick_ns = now_ns(); pthread_mutex_unlock(&g_pq_mu);\n        g_link_started = 1;',
                        '        g_link_started = 1;'), [('clock', 1)]),
    # Targeted negative controls for winding-down invariants, not historical fixes.
    ('8-second-thread-control', mutation('if (!g_link_started) {', 'if (!g_link) {'), [('winding', 1)]),
    ('8-ownership-control', mutation('g_link_owns_layer = 0; pthread_cond_broadcast(&g_pq_cv);',
                                   'pthread_cond_broadcast(&g_pq_cv);'), [('winding', 1)]),
]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    original = SOURCE.read_bytes()
    text = original.decode()
    results = []
    try:
        for name, change, cases in MUTATIONS:
            changed = change(text)
            SOURCE.write_text(changed)
            (OUT / (name + '.diff')).write_text(''.join(difflib.unified_diff(
                text.splitlines(True), changed.splitlines(True), fromfile='nvn_mtl.m', tofile=name)))
            build = subprocess.run(['make', 'all'], cwd=HERE, capture_output=True, text=True, timeout=120)
            (OUT / (name + '-build.log')).write_text(build.stdout + build.stderr)
            if build.returncode:
                raise RuntimeError(name + ': build failed (not a valid mutation test)')
            caught = False
            for case, mode in cases:
                run = subprocess.run([str(BIN), case], env={**os.environ, 'DM_PRESENT_LINK': str(mode)},
                                     capture_output=True, text=True, timeout=7)
                log = f'{name}-{case}-{mode}.log'
                (OUT / log).write_text(run.stdout + run.stderr)
                # A crash/alarm is not an assertion failure and cannot count as caught.
                assert run.returncode in (0, 1), (name, run.returncode)
                caught |= run.returncode == 1 and 'FAIL:' in run.stdout
                results.append(dict(mutation=name, case=case, mode=mode, exit=run.returncode,
                                    stdout=run.stdout, log=log))
                print(f'{name} / {case} mode {mode}: exit {run.returncode}', flush=True)
            assert caught, name + ': mutation survived'
            SOURCE.write_bytes(original)
    finally:
        SOURCE.write_bytes(original)
        (OUT / 'mutations.json').write_text(json.dumps(results, indent=2) + '\n')
    baseline = subprocess.run(['make', 'run'], cwd=HERE, capture_output=True, text=True, timeout=120)
    (OUT / 'restored-pass.log').write_text(baseline.stdout + baseline.stderr)
    assert SOURCE.read_bytes() == original
    assert baseline.returncode == 0, 'restored baseline failed'
    print('All mutations detected; production bytes restored; baseline passes.', flush=True)


if __name__ == '__main__':
    main()
