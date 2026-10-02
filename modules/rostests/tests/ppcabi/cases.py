"""The ordered cases shared by the probe, boot service, batch and report."""
from pathlib import Path
import re


def load_cases():
    cases = []
    for line in Path(__file__).with_name('cases.def').read_text().splitlines():
        if not line.strip():
            continue
        match = re.fullmatch(r'PPC_ABI_CASE\(([a-z][a-z0-9]*)\)', line)
        if not match:
            raise ValueError(f'invalid PPC ABI case: {line}')
        cases.append(match[1])
    if not cases or len(set(cases)) != len(cases):
        raise ValueError('PPC ABI cases must be nonempty and unique')
    return tuple(cases)


CASES = load_cases()
