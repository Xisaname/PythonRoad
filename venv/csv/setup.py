#!/usr/bin/env python
# To use:
#       python setup.py install
#

import distutils, os, sys, re
from distutils.core import setup, Extension
from distutils.command.sdist import sdist

class PreReleaseCheck:
    def __init__(self, distribution):
        self.distribution = distribution
        self.check_rev('csv.c', r'rev = PyString_FromString("\(.*\)")')

    def _extract_rev(self, filename, pattern):
        regexp = re.compile(pattern)
        match = None
        revs = []
        line_num = 0
        f = open(filename)
        try:
            for line in f.readlines():
                line_num += 1
                match = regexp.search(line)
                if match:
                    revs.append((line_num, match.group(1)))
        finally:
            f.close()
        return revs

    def check_rev(self, filename, pattern):
        file_revs = self._extract_rev(filename, pattern)
        if not file_revs:
            sys.exit("Could not locate version in %s" % filename)
        line_num, file_rev = file_revs[0]
        for num, rev in file_revs[1:]:
            if rev != file_rev:
                sys.exit("%s:%d inconsistent version on line %d" % \
                         (filename, line_num, num))
        setup_rev = self.distribution.get_version()
        if file_rev != setup_rev:
            sys.exit("%s:%d version %s does not match setup.py version %s" % \
                     (filename, line_num, file_rev, setup_rev))


class my_sdist(sdist):
    def run(self):
        PreReleaseCheck(self.distribution)
        self.announce("Pre-release checks pass!")
        sdist.run(self)

setup(name = "csv",
      version = "1.0",
      maintainer = "Dave Cole",
      maintainer_email = " csv@object-craft.com.au",
      description = "Fast CSV Parser for Python",
      url = "http://www.object-craft.com.au/projects/csv/",
      ext_modules = [
           Extension('csv',
                     ['csv.c'],
                     )
           ],
      license = 'BSD - see file LICENCE',
      )

