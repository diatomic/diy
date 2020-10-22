import os
import re
import sys
import platform
import subprocess

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
from distutils.version import LooseVersion

# ugly hack with paths to export __version__ directly from DIY
sys.path = [os.path.dirname(os.path.abspath(sys.argv[0])) + '/bindings/python/diy'] + sys.path
from _version import __version__
sys.path = sys.path[1:]

class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=''):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)


class CMakeBuild(build_ext):
    def run(self):
        try:
            out = subprocess.check_output(['cmake', '--version'])
        except OSError:
            raise RuntimeError("CMake must be installed to build the following extensions: " +
                               ", ".join(e.name for e in self.extensions))

        if platform.system() == "Windows":
            cmake_version = LooseVersion(re.search(r'version\s*([\d.]+)', out.decode()).group(1))
            if cmake_version < '3.1.0':
                raise RuntimeError("CMake >= 3.1.0 is required on Windows")

        for ext in self.extensions:
            self.build_extension(ext)

    def build_extension(self, ext):
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))
        cmake_args = ['-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=' + extdir,
                      '-DPYTHON_EXECUTABLE=' + sys.executable,
                      '-Dpython=ON',
                      '-Dbuild_tests=OFF',
                      '-Dbuild_examples=OFF']

        cfg = 'Debug' if self.debug else 'Release'
        build_args = ['--config', cfg]

        if platform.system() == "Windows":
            cmake_args += ['-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_{}={}'.format(cfg.upper(), extdir)]
            if sys.maxsize > 2**32:
                cmake_args += ['-A', 'x64']
            build_args += ['--', '/m']
        else:
            cmake_args += ['-DCMAKE_BUILD_TYPE=' + cfg]
            build_args += ['--', '-j2']

        env = os.environ.copy()
        env['CXXFLAGS'] = '{} -DVERSION_INFO=\\"{}\\"'.format(env.get('CXXFLAGS', ''),
                                                              self.distribution.get_version())
        if not os.path.exists(self.build_temp):
            os.makedirs(self.build_temp)
        subprocess.check_call(['cmake', ext.sourcedir] + cmake_args, cwd=self.build_temp, env=env)
        subprocess.check_call(['cmake', '--build', '.'] + build_args, cwd=self.build_temp)

setup(
    name='diy',
    version=__version__,
    author='Dmitriy Morozov',
    author_email='dmitriy@mrzv.org',
    url='http://github.com/diatomic/diy',
    description='Library for computing persistent homology',
    license='BSD',
    long_description="""
DIY is a block-parallel library for implementing scalable algorithms that can execute both
in-core and out-of-core. The same program can be executed with one or more threads per MPI
process, seamlessly combining distributed-memory message passing with shared-memory thread
parallelism.  The abstraction enabling these capabilities is block parallelism; blocks
and their message queues are mapped onto processing elements (MPI processes or threads) and are
migrated between memory and storage by the DIY runtime. Complex communication patterns,
including neighbor exchange, merge reduction, swap reduction, and all-to-all exchange, are
possible in- and out-of-core in DIY.
""",
    ext_modules=[CMakeExtension('diy')],
    packages=['diy'],
    package_dir = { 'diy' : 'bindings/python/diy' },
    cmdclass=dict(build_ext=CMakeBuild),
    classifiers=[
        'Development Status :: 5 - Production/Stable',
        'Intended Audience :: Developers',
        'Intended Audience :: Science/Research',
        'Topic :: Software Development :: Libraries :: Python Modules',
        'Topic :: Utilities',
        'Programming Language :: C++',
        'Programming Language :: Python :: 2.7',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.2',
        'Programming Language :: Python :: 3.3',
        'Programming Language :: Python :: 3.4',
        'Programming Language :: Python :: 3.5',
        'Programming Language :: Python :: 3.6',
        'Programming Language :: Python :: 3.7',
        'Programming Language :: Python :: 3.8',
        'License :: OSI Approved :: BSD License'
    ],
    zip_safe=False,
)
