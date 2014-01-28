#!/usr/bin/env python
"""Kegboard support library.

This package contains the Python protocol support for Kegboard.  For more
information on Kegboard, see http://kegbot.org/kegboard/
"""

DOCLINES = __doc__.split('\n')

# Change this to True to include optional dependencies
USE_OPTIONAL = False

VERSION = '1.1.1'
SHORT_DESCRIPTION = DOCLINES[0]
LONG_DESCRIPTION = '\n'.join(DOCLINES[2:])

def setup_package():
  from distribute_setup import use_setuptools
  use_setuptools()
  from setuptools import setup, find_packages

  setup(
      name = 'kegbot-kegboard',
      version = VERSION,
      description = SHORT_DESCRIPTION,
      long_description = LONG_DESCRIPTION,
      author = 'mike wakerly',
      author_email = 'opensource@hoho.com',
      url = 'http://kegbot.org/',
      packages = find_packages(exclude=['testdata']),
      namespace_packages = ['kegbot'],
      scripts = [
        'distribute_setup.py',
        'bin/kegboard-monitor.py',
        'bin/kegboard-tester.py',
        'bin/kegboard-info.py',
      ],
      install_requires = [
        'kegbot-pyutils >= 0.1.2',
        'python-gflags',
        'pyserial',
      ],
      include_package_data = True,
  )

if __name__ == '__main__':
  setup_package()
