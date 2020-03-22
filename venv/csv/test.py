#
#       Unit tests for CSV module
#
#       Some of the malformed input tests don't make much sense - the intention
#       is to mimic MS Excel's behavior. It's always possible that different
#       versions of Excel parse differently - Excel 97 was the basis for these
#       tests. If you suspect Excel's behaviour has changed, let us know.
#
#       These tests should pass for all Python versions from 1.5.2 up.
#       "pyunit" (unittest) for 1.5.2 can be found at:
#
#               http://pyunit.sourceforge.net/
#
#       Copyright 2002 by Object Craft P/L, Melbourne, Australia.
#
#       LICENCE - see LICENCE file distributed with this software for details.
#
#       $Id: test.py,v 1.10 2002/10/04 12:47:57 andrewm Exp $
#
import sys
import os
import distutils.util
basedir = os.path.abspath(os.path.dirname(sys.argv[0]))
plat_spec = "lib.%s-%s" % (distutils.util.get_platform(), sys.version[0:3])
sys.path.insert(0, os.path.join(basedir, 'build', plat_spec))
import unittest
import csv
import string

class TestCSVParse(unittest.TestCase):
    def parse_expect(self, input, expected_result, **kw):
        parser = csv.parser()
        for kw_arg, kw_value in kw.items():
            setattr(parser, kw_arg, kw_value)
        result = []
        for line in string.split(input, '\n'):
            fields = parser.parse(line)
            if not fields:
                continue
            result.append(fields)
        self.assertEqual(expected_result, result)

    def parse_exception(self, input, exception, **kw):
        parser = csv.parser()
        for kw_arg, kw_value in kw.items():
            setattr(parser, kw_arg, kw_value)
        self.assertRaises(exception, parser.parse, input)

    def test_single(self):
        self.parse_expect('abc', [['abc']])

    def test_simple(self):
        self.parse_expect('1,2,3,4,5', [['1','2','3','4','5']])

    def test_blankline(self):
        self.parse_expect('', [])

    def test_empty_fields(self):
        self.parse_expect(',', [['', '']])

    def test_singlequoted(self):
        self.parse_expect('""', [['']])

    def test_singlequoted_left_empty(self):
        self.parse_expect('"",', [['','']])

    def test_singlequoted_right_empty(self):
        self.parse_expect(',""', [['','']])

    def test_single_quoted_quote(self):
        self.parse_expect('""""', [['"']])

    def test_quoted_quotes(self):
        self.parse_expect('""""""', [['""']])

    def test_inline_quote(self):
        self.parse_expect('a""b', [['a""b']])

    def test_inline_quotes(self):
        self.parse_expect('a"b"c', [['a"b"c']])

    def test_quotes_and_more(self):
        self.parse_expect('"a"b', [['ab']])

    def test_lone_quote(self):
        self.parse_expect('a"b', [['a"b']])

    def test_quote_and_quote(self):
        self.parse_expect('"a" "b"', [['a "b"']])

    def test_space_and_quote(self):
        self.parse_expect(' "a"', [[' "a"']])

    def test_quoted(self):
        self.parse_expect('1,2,3,"I think, therefore I am",5,6', 
                          [['1', '2', '3', 
                            'I think, therefore I am', 
                            '5', '6']])

    def test_quoted_quote(self):
        self.parse_expect('1,2,3,"""I see,"" said the blind man","as he picked up his hammer and saw"',
                          [['1', '2', '3', 
                            '"I see," said the blind man', 
                            'as he picked up his hammer and saw']])

    def test_quoted_nl(self):
        input = '''\
1,2,3,"""I see,""
said the blind man","as he picked up his
hammer and saw"
9,8,7,6'''
        self.parse_expect(input,
                          [['1', '2', '3', 
                            '"I see,"\nsaid the blind man', 
                            'as he picked up his\nhammer and saw'],
                           ['9','8','7','6']])

    def test_strict_quote(self):
        self.parse_exception('"abc"def', csv.Error, strict = 1)

    def test_unquoted_nl(self):
        self.parse_exception('abc\ndef', csv.Error)

    def test_alt_fieldsep(self):
        self.parse_expect('1\t2\t3\t', [['1', '2', '3', '']], 
                          field_sep = '\t')

    def test_escapechar(self):
        self.parse_expect(r'abc\,def,"ghi\",jkl",mn\\o', 
                          [['abc,def', 'ghi",jkl', r'mn\o']],
                          escape_char = "\\")

    def test_alt_quotechar(self):
        self.parse_expect("'abc\ndef','I''m lost!'", 
                          [['abc\ndef', 'I\'m lost!']], 
                          quote_char = "'")

    def test_null_quotechar(self):
        self.parse_expect('''"abc",'def',ghi''',
                          [['"abc"',"'def'",'ghi']],
                          quote_char = None)

# Still trying to work out what the right answer is for this one:
#    def test_tim_quotes(self):
#        self.parse_expect('"this is not" a quoted field,"this is"', 
#                          [['"this is not" a quoted field', 'this is']])

    def test_dubious_quote(self):
        self.parse_expect('12,12,1",', [['12', '12', '1"', '']])


class TestCSVJoin(unittest.TestCase):
    def join_expect(self, input, expected_result, **kw):
        parser = csv.parser()
        for kw_arg, kw_value in kw.items():
            setattr(parser, kw_arg, kw_value)
        result = parser.join(input)
        self.assertEqual(expected_result, result)

    def test_null(self):
        self.join_expect([], '')

    def test_single(self):
        self.join_expect(['abc'], 'abc')

    def test_simple(self):
        self.join_expect([1, 2, 'abc', 3, 4], '1,2,abc,3,4')

    def test_quotes(self):
        self.join_expect([1, 2, 'a"bc"', 3, 4], '1,2,"a""bc""",3,4')

    def test_quote_fieldsep(self):
        self.join_expect(['abc,def'], '"abc,def"')

    def test_newlines(self):
        self.join_expect([1, 2, 'a\nbc', 3, 4], '1,2,"a\nbc",3,4')

    def test_field_sep(self):
        self.join_expect([1, 2, 'a"bc"', 3, 4], '1\t2\t"a""bc"""\t3\t4', 
                         field_sep = '\t')

    def test_alt_quote(self):
        self.join_expect(['abc,def'], "'abc,def'",
                         quote_char = "'")

    def test_null_quote(self):
        self.join_expect(['abc,def'], "abc,def",
                         quote_char = None)

    def test_null_quote(self):
        self.join_expect(['abc,def'], r"abc\,def",
                         quote_char = None, escape_char = '\\')

    def test_quote_and_escape(self):
        self.join_expect(['abc,def'], r"'abc,def'",
                         quote_char = "'", escape_char = '\\')

def _testclasses():
#    We'd rather just do this:
#    return [getattr(mod, name) for name in dir(mod) if name.startswith('Test')]
    mod = sys.modules[__name__]
    result = []
    for attr in dir(mod):
        if attr[:4] == 'Test':
            result.append(getattr(mod, attr))
    return result

def suite():
    suite = unittest.TestSuite()
    for testclass in _testclasses():
        suite.addTest(unittest.makeSuite(testclass))
    return suite

if __name__ == '__main__':
    unittest.main(defaultTest='suite')
