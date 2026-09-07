"""Check that x86 decorated stub aliases keep distinct public export names."""
import pathlib
import subprocess
import sys
import tempfile

spec = pathlib.Path(__file__).with_name('decorated-stubs.spec')
with tempfile.TemporaryDirectory() as directory:
    definition = pathlib.Path(directory) / 'aliases.def'
    subprocess.run([sys.argv[1], '-a=i386', '-n=aliases.dll',
                    '-d=' + str(definition), str(spec)], check=True)
    lines = {line.strip() for line in definition.read_text().splitlines()}
    for name, size, ordinal in [('OpenTnefStream', 28, 149),
                                ('OpenTnefStreamEx', 32, 151),
                                ('GetTnefStreamCodepage', 12, 153),
                                ('RTFSync', 12, 183)]:
        decorated = '{}@{}'.format(name, size)
        assert '{}={} @{}'.format(decorated, decorated, ordinal) in lines
        assert '{}={} @{}'.format(name, decorated, ordinal + 1) in lines
print('Decorated stub export regression passed')
