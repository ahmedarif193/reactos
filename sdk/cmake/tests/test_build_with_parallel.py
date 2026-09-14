#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run with Python's unittest runner; integration tests require CMake and Ninja."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

HELPER = Path(__file__).resolve().parents[1] / 'build-with-parallel.py'
SPEC = importlib.util.spec_from_file_location('parallel', HELPER)
parallel = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(parallel)


class JobCountTests(unittest.TestCase):
    def test_ninja_options(self):
        cases = [(['ninja', '-j8'], 8), (['ninja', '-j', '3'], 3),
                 (['ninja', '-j0'], 0), (['ninja', '-j1', '-j7'], 7),
                 (['/tools with spaces/ninja', '-C', '/build with spaces', '-j5'], 5),
                 (['ninja', '-f', '-j2', '-j9'], 9),
                 (['ninja', '--', '-j2'], None),
                 (['ninja', '-t', 'commands', '-j2'], None),
                 (['python', 'script.py', '-j9'], None)]
        for argv, expected in cases:
            with self.subTest(argv=argv):
                self.assertEqual(parallel.job_count(argv), expected)

    def test_cmake_options(self):
        self.assertEqual(parallel.job_count(['cmake', '--build', 'dir', '--parallel', '6']), 6)
        self.assertEqual(parallel.job_count(['cmake', '--build', 'dir', '--parallel=2']), 2)
        self.assertIsNone(parallel.job_count(['cmake', '-P', 'script', '-j8']))
        self.assertIsNone(parallel.job_count(['cmake', '--build', 'dir', '--parallel']))

    def test_forwarding_and_environment(self):
        base = ['cmake', '--build', 'dir', '--', '-v']
        command, env = parallel.build_command(base, 3, {'CMAKE_BUILD_PARALLEL_LEVEL': '9'})
        self.assertEqual(command, ['cmake', '--build', 'dir', '--parallel', '3', '--', '-v'])
        self.assertEqual(env['CMAKE_BUILD_PARALLEL_LEVEL'], '3')
        command, env = parallel.build_command(base, 0, {'CMAKE_BUILD_PARALLEL_LEVEL': '9'})
        self.assertEqual(command, base + ['-j0'])
        self.assertNotIn('CMAKE_BUILD_PARALLEL_LEVEL', env)
        self.assertEqual(parallel.build_command(base, None, {'CMAKE_BUILD_PARALLEL_LEVEL': '4'}),
                         (base, {'CMAKE_BUILD_PARALLEL_LEVEL': '4'}))

    def test_explicit_child_count_and_failure_status(self):
        command = ['cmake', '--build', 'dir', '--parallel', '2']
        self.assertEqual(parallel.build_command(command, 2, {}),
                         (command, {'CMAKE_BUILD_PARALLEL_LEVEL': '2'}))
        with mock.patch.object(sys, 'argv', ['wrapper'] + command), \
             mock.patch.object(parallel.subprocess, 'call', return_value=17) as child:
            self.assertEqual(parallel.main(), 17)
            self.assertEqual(child.call_args[0][0], command)


@unittest.skipUnless(shutil.which('cmake') and shutil.which('ninja'), 'CMake and Ninja required')
class NestedBuildTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='nested build test ')
        cls.root = Path(cls.temporary.name)
        cls.result = cls.root / 'result.json'
        cls.cmake = shutil.which('cmake')
        cls.ninja = shutil.which('ninja')
        # The leaf records the actual Ninja invocation, not just forwarded argv.
        recorder = cls.root / 'record.py'
        recorder.write_text('''import importlib.util, json, os, pathlib, sys
spec = importlib.util.spec_from_file_location('parallel', sys.argv[1])
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
counts = [m.job_count(a) for a in m.ancestors()]
pathlib.Path(sys.argv[2]).write_text(json.dumps({
    'jobs': next((v for v in counts if v is not None), None),
    'environment': os.environ.get('CMAKE_BUILD_PARALLEL_LEVEL')}))
''')
        def quoted(path):
            return '"' + str(path).replace('\\', '/') + '"'
        leaf = cls.root / 'leaf'
        leaf.mkdir()
        (leaf / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.17)\nproject(Leaf NONE)\n'
            'add_custom_target(record ALL COMMAND ' + ' '.join(map(quoted,
                [sys.executable, recorder, HELPER, cls.result])) + ' VERBATIM)\n')
        cls.run_command([cls.cmake, '-S', str(leaf), '-B', str(leaf / 'build'), '-G', 'Ninja'])
        inner = leaf / 'build'
        # Two nesting levels exercise the ARM64EC/WoW64 -> Mesa case.
        for name in ['middle', 'outer']:
            source = cls.root / name
            source.mkdir()
            (source / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.17)\nproject(Nested NONE)\n'
                'add_custom_target(nested ALL COMMAND ' + ' '.join(map(quoted,
                    [sys.executable, HELPER, cls.cmake])) + ' --build ' + quoted(inner) + ' VERBATIM)\n')
            inner = source / 'build'
            cls.run_command([cls.cmake, '-S', str(source), '-B', str(inner), '-G', 'Ninja'])
        cls.build = inner

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    @staticmethod
    def run_command(command, level=None):
        env = dict(os.environ)
        env.pop('CMAKE_BUILD_PARALLEL_LEVEL', None)
        if level is not None:
            env['CMAKE_BUILD_PARALLEL_LEVEL'] = level
        result = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout)
        return result.stdout

    def check_result(self, jobs, environment):
        self.assertEqual(json.loads(self.result.read_text()),
                         {'jobs': jobs, 'environment': environment})

    def test_dynamic_ninja_jobs_without_reconfiguring(self):
        for jobs in [1, 5, 2]:
            with self.subTest(jobs=jobs):
                self.run_command([self.ninja, '-C', str(self.build), '-j' + str(jobs)])
                self.check_result(jobs, str(jobs))

    def test_explicit_ninja_jobs_override_environment(self):
        self.run_command([self.ninja, '-C', str(self.build), '-j3'], level='11')
        self.check_result(3, '3')

    def test_cmake_parallel(self):
        self.run_command([self.cmake, '--build', str(self.build), '--parallel', '4'])
        self.check_result(4, '4')

    def test_environment_parallel(self):
        self.run_command([self.cmake, '--build', str(self.build)], level='6')
        self.check_result(6, '6')

    def test_unlimited_ninja(self):
        self.run_command([self.ninja, '-C', str(self.build), '-j0'], level='9')
        self.check_result(0, None)

    def test_native_default(self):
        self.run_command([self.ninja, '-C', str(self.build)])
        self.check_result(None, None)


if __name__ == '__main__':
    unittest.main()
