/******************************************************************
Copyright 2000 by Object Craft P/L, Melbourne, Australia.

LICENCE - see LICENCE file distributed with this software for details.
******************************************************************/

#include <ctype.h>
#include "Python.h"
#include "structmember.h"

static PyObject *error_obj;	/* CSV exception */

/* ----------------------------------------------------- */

/* Declarations for objects of type Parser */

typedef enum {
    START_RECORD, START_FIELD, ESCAPED_CHAR, IN_FIELD, 
    IN_QUOTED_FIELD, ESCAPE_IN_QUOTED_FIELD, QUOTE_IN_QUOTED_FIELD
} ParserState;

typedef struct {
    PyObject_HEAD

    int ms_double_quote;	/* is " represented by ""? */
    char field_sep;		/* field separator */
    char quote_char;		/* quote character */
    char escape_char;		/* escape character */

    ParserState state;		/* current CSV parse state */
    PyObject *fields;		/* field list for current record */

    int auto_clear;		/* should fields be cleared on next
				   parse() after exception? */
    int strict;			/* raise exception on bad CSV */

    int had_parse_error;	/* did we have a parse error? */

    char *field;		/* build current field in here */
    int field_size;		/* size of allocated buffer */
    int field_len;		/* length of current field */

    char *rec;			/* buffer for parser.join */
    int rec_size;		/* size of allocated record */
    int rec_len;		/* length of record */
    int num_fields;		/* number of fields in record */
} ParserObj;

staticforward PyTypeObject ParserType;

static PyObject *raise_exception(char *fmt, ...)
{
    va_list ap;
    char msg[512];
    PyObject *pymsg;

    va_start(ap, fmt);
#ifdef _WIN32
    _vsnprintf(msg, sizeof(msg), fmt, ap);
#else
    vsnprintf(msg, sizeof(msg), fmt, ap);
#endif
    va_end(ap);
    pymsg = PyString_FromString(msg);
    PyErr_SetObject(error_obj, pymsg);
    Py_XDECREF(pymsg);

    return NULL;
}

static void parse_save_field(ParserObj *self)
{
    PyObject *field;

    field = PyString_FromStringAndSize(self->field, self->field_len);
    if (field != NULL) {
	PyList_Append(self->fields, field);
	Py_XDECREF(field);
    }
    self->field_len = 0;
}

static int parse_grow_buff(ParserObj *self)
{
    if (self->field_size == 0) {
	self->field_size = 4096;
	self->field = PyMem_Malloc(self->field_size);
    } else {
	self->field_size *= 2;
	self->field = PyMem_Realloc(self->field, self->field_size);
    }
    if (self->field == NULL) {
	PyErr_NoMemory();
	return 0;
    }
    return 1;
}

static void parse_add_char(ParserObj *self, char c)
{
    if (self->field_len == self->field_size && !parse_grow_buff(self))
	return;
    self->field[self->field_len++] = c;
}

static void parse_prepend_char(ParserObj *self, char c)
{
    if (self->field_len == self->field_size && !parse_grow_buff(self))
	return;
    memmove(self->field + 1, self->field, self->field_len);
    self->field[0] = c;
    self->field_len++;
}

static void parse_process_char(ParserObj *self, char c)
{
    switch (self->state) {
    case START_RECORD:
	/* start of record */
	if (c == '\0')
	    /* empty line - return [] */
	    break;
	/* normal character - handle as START_FIELD */
	self->state = START_FIELD;
        /* fallthru */
    case START_FIELD:
	/* expecting field */
	if (c == '\0') {
	    /* save empty field - return [fields] */
	    parse_save_field(self);
	    self->state = START_RECORD;
        } else if (c == self->quote_char) {
	    /* start quoted field */
	    self->state = IN_QUOTED_FIELD;
        } else if (c == self->escape_char) {
	    /* possible escaped character */
	    self->state = ESCAPED_CHAR;
	} else if (c == self->field_sep) {
	    /* save empty field */
	    parse_save_field(self);
	} else {
	    /* begin new unquoted field */
	    parse_add_char(self, c);
	    self->state = IN_FIELD;
	}
	break;

    case ESCAPED_CHAR:
        if (c != self->escape_char && c != self->field_sep &&
            c != self->quote_char)
            parse_add_char(self, self->escape_char);
        parse_add_char(self, c);
        self->state = IN_FIELD;
        break;

    case IN_FIELD:
	/* in unquoted field */
        if (c == '\0') {
	    /* end of line - return [fields] */
	    parse_save_field(self);
	    self->state = START_RECORD;
        } else if (c == self->escape_char) {
            /* possible escaped character */
            self->state = ESCAPED_CHAR;
        } else if (c == self->field_sep) {
	    /* save field - wait for new field */
	    parse_save_field(self);
	    self->state = START_FIELD;
	} else {
	    /* normal character - save in field */
	    parse_add_char(self, c);
	}
	break;

    case IN_QUOTED_FIELD:
	/* in quoted field */
	if (c == '\0') {
	    /* end of line - save '\n' in field */
	    parse_add_char(self, '\n');
        } else if (c == self->escape_char) {
            /* Possible escape character */
            self->state = ESCAPE_IN_QUOTED_FIELD;
        } else if (c == self->quote_char) {
	    if (self->ms_double_quote) {
		/* microsoft style double quotes; " represented by "" */
		self->state = QUOTE_IN_QUOTED_FIELD;
	    } else {
		/* end of quote part of field */
		self->state = IN_FIELD;
	    }
	} else {
	    /* normal character - save in field */
	    parse_add_char(self, c);
	}
	break;

    case ESCAPE_IN_QUOTED_FIELD:
        if (c != self->escape_char && c != self->field_sep &&
            c != self->quote_char)
            parse_add_char(self, self->escape_char);
        parse_add_char(self, c);
        self->state = IN_QUOTED_FIELD;
        break;

    case QUOTE_IN_QUOTED_FIELD:
	/* microsoft double quotes - seen a quote in an quoted field */
	if (self->quote_char && c == self->quote_char) {
	    /* save "" as " */
	    parse_add_char(self, c);
	    self->state = IN_QUOTED_FIELD;
	} else if (c == self->field_sep) {
	    /* save field - wait for new field */
	    parse_save_field(self);
	    self->state = START_FIELD;
	} else if (c == '\0') {
	    /* end of line - return [fields] */
	    parse_save_field(self);
	    self->state = START_RECORD;
	} else if (!self->strict) {
	    parse_add_char(self, c);
	    self->state = IN_FIELD;
	} else {
	    /* illegal */
	    self->had_parse_error = 1;
	    raise_exception("%c expected after %c", 
			    self->field_sep, self->quote_char);
	}
	break;

    }
}

static void clear_fields_and_status(ParserObj *self)
{
    if (self->fields) {
	Py_XDECREF(self->fields);
    }
    self->fields = PyList_New(0);
    self->field_len = 0;
    self->state = START_RECORD;

    self->had_parse_error = 0;
}

/* ---------------------------------------------------------------- */

static char Parser_parse__doc__[] = 
"parse(s) -> list of strings\n"
"\n"
"CSV parse the single line in the string s and return a\n"
"list of string fields.  If the CSV record contains multi-line\n"
"fields, the function will return None until all lines of the\n"
"record have been parsed.";

static PyObject *Parser_parse(ParserObj *self, PyObject *args)
{
    char *line;

    if (!PyArg_ParseTuple(args, "s", &line))
	return NULL;

    if (self->auto_clear && self->had_parse_error)
	clear_fields_and_status(self);

    /* Process line of text - send '\0' to processing code to
       represent end of line.  End of line which is not at end of
       string is an error. */
    while (*line) {
	char c;

	c = *line++;
	if (c == '\r') {
	    c = *line++;
	    if (c == '\0')
		/* macintosh end of line */
		break;
	    if (c == '\n') {
		c = *line++;
		if (c == '\0')
		    /* DOS end of line */
		    break;
	    }
	    self->had_parse_error = 1;
	    return raise_exception("Newline inside string");
	}
	if (c == '\n') {
	    c = *line++;
	    if (c == '\0')
		/* unix end of line */
		break;
	    self->had_parse_error = 1;
	    return raise_exception("Newline inside string");
	}
	parse_process_char(self, c);
	if (PyErr_Occurred())
	    return NULL;
    }
    parse_process_char(self, '\0');

    if (self->state == START_RECORD) {
	PyObject *fields = self->fields;
	self->fields = PyList_New(0);
	return fields;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

/* ---------------------------------------------------------------- */

static char Parser_clear__doc__[] = 
"clear() -> None\n"
"\n"
"Discard partially parsed record.  This must be called to reset\n"
"parser state after an exception.";

static PyObject *Parser_clear(ParserObj *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
	return NULL;

    clear_fields_and_status(self);

    Py_INCREF(Py_None);
    return Py_None;
}

/* ---------------------------------------------------------------- */
static void join_reset(ParserObj *self)
{
    self->rec_len = 0;
    self->num_fields = 0;
}

#define MEM_INCR 32768

/* Calculate new record length or append field to record.  Return new
 * record length.
 */
static int join_append_data(ParserObj *self, char *field, int quote_empty,
			    int *quoted, int copy_phase)
{
    int i, rec_len;

    rec_len = self->rec_len;

    /* If this is not the first field we need a field separator.
     */
    if (self->num_fields > 0) {
	if (copy_phase)
	    self->rec[rec_len] = self->field_sep;
	rec_len++;
    }
    /* We only know about quoted in the copy phase.
     */
    if (copy_phase && *quoted) {
	self->rec[rec_len] = self->quote_char;
	rec_len++;
    }
    for (i = 0;; i++) {
	char c = field[i];

	if (c == '\0')
	    break;
	/* If in MS double quote mode we escape quote chars with a
	 * quote.
	 */
	if (c == self->quote_char && self->ms_double_quote) {
	    if (copy_phase)
		self->rec[rec_len] = self->quote_char;
	    *quoted = 1;
	    rec_len++;
	}
	/* Some special characters need to be escaped.  If we have a
	 * quote character switch to quoted field instead of escaping
	 * individual characters.
	 */
        if (!*quoted
	    && (c == self->field_sep || c == self->escape_char
		|| c == '\n' || c == '\r')) {
	    if (self->quote_char)
		*quoted = 1;
	    else if (self->escape_char) {
		if (copy_phase)
		    self->rec[rec_len] = self->escape_char;
		rec_len++;
	    }
	}
	/* Copy field character into record buffer.
	 */
	if (copy_phase)
	    self->rec[rec_len] = c;
	rec_len++;
    }

    /* If field is empty check if it needs to be quoted.
     */
    if (i == 0 && quote_empty && self->quote_char)
	*quoted = 1;

    /* Handle final quote character on field.
     */
    if (*quoted) {
	if (copy_phase)
	    self->rec[rec_len] = self->quote_char;
	else
	    /* Didn't know about leading quote until we found it
	     * necessary in field data - compensate for it now.
	     */
	    rec_len++;
	rec_len++;
    }

    return rec_len;
}

static int join_append(ParserObj *self, char *field, int quote_empty)
{
    int rec_len, quoted;

    quoted = 0;
    rec_len = join_append_data(self, field, quote_empty, &quoted, 0);

    /* grow record buffer if necessary */
    if (rec_len > self->rec_size) {
	if (self->rec_size == 0) {
	    self->rec_size = (rec_len / MEM_INCR + 1) * MEM_INCR;
	    self->rec = PyMem_Malloc(self->rec_size);
	} else {
	    char *old_rec = self->rec;

	    self->rec_size = (rec_len / MEM_INCR + 1) * MEM_INCR;
	    self->rec = PyMem_Realloc(self->rec, self->rec_size);
	    if (self->rec == NULL)
		free(old_rec);
	}
	if (self->rec == NULL) {
	    PyErr_NoMemory();
	    return 0;
	}
    }

    self->rec_len = join_append_data(self, field, quote_empty, &quoted, 1);
    self->num_fields++;

    return 1;
}

static PyObject *join_string(ParserObj *self)
{
    return PyString_FromStringAndSize(self->rec, self->rec_len);
}

static char Parser_join__doc__[] = 
"join(sequence) -> string\n"
"\n"
"Construct a CSV record from a sequence of fields.  Non-string\n"
"elements will be converted to string.";

static PyObject *Parser_join(ParserObj *self, PyObject *args)
{
    PyObject *seq;
    int len, i;

    if (!PyArg_ParseTuple(args, "O", &seq))
	return NULL;
    if (!PySequence_Check(seq))
	return raise_exception("sequence expected");

    len = PySequence_Length(seq);
    if (len < 0)
	return NULL;

    join_reset(self);
    for (i = 0; i < len; i++) {
	PyObject *field;
	int append_ok;

	field = PySequence_GetItem(seq, i);
	if (field == NULL)
	    return NULL;

	if (PyString_Check(field)) {
	    append_ok = join_append(self, PyString_AsString(field), len == 1);
	    Py_DECREF(field);
	} else if (field == Py_None) {
	    append_ok = join_append(self, "", len == 1);
	    Py_DECREF(field);
	} else {
	    PyObject *str;

	    str = PyObject_Str(field);
	    Py_DECREF(field);
	    if (str == NULL)
		return NULL;

	    append_ok = join_append(self, PyString_AsString(str), len == 1);
	    Py_DECREF(str);
	}
	if (!append_ok)
	    return NULL;
    }

    return join_string(self);
}

static struct PyMethodDef Parser_methods[] = {
    { "parse", (PyCFunction)Parser_parse, METH_VARARGS, Parser_parse__doc__ },
    { "clear", (PyCFunction)Parser_clear, METH_VARARGS, Parser_clear__doc__ },
    { "join", (PyCFunction)Parser_join, METH_VARARGS, Parser_join__doc__ },
    
    { NULL, NULL }		/* sentinel */
};

static void Parser_dealloc(ParserObj *self)
{
    if (self->field)
	free(self->field);
    if (self->fields) {
	Py_XDECREF(self->fields);
    }
    if (self->rec)
	free(self->rec);

    PyMem_DEL(self);
}

#define OFF(x) offsetof(ParserObj, x)

static struct memberlist Parser_memberlist[] = {
    { "ms_double_quote", T_INT,    OFF(ms_double_quote) },
    { "fields",          T_OBJECT, OFF(fields) },
    { "field_sep",       T_CHAR,   OFF(field_sep) },
    { "quote_char",      T_CHAR,   OFF(quote_char) },
    { "escape_char",     T_CHAR,   OFF(escape_char) },
    { "auto_clear",      T_INT,    OFF(auto_clear) },
    { "strict",          T_INT,    OFF(strict) },
    { "had_parse_error", T_INT,    OFF(had_parse_error), RO },

    { NULL }  /* Sentinel */
};

static PyObject *Parser_getattr(ParserObj *self, char *name)
{
    PyObject *rv;

    rv = PyMember_Get((char *)self, Parser_memberlist, name);
    if (rv)
	return rv;
    PyErr_Clear();
    return Py_FindMethod(Parser_methods, (PyObject *)self, name);
}

static int _set_char_attr(char *attr, PyObject *v)
{
    /* Special case for constructor - NULL == use default.
     */
    if (v == NULL)
	return 0;

    if (v == Py_None) {
        *attr = 0;
        return 0;
    } else if (PyInt_Check(v) && PyInt_AsLong(v) == 0) {
        *attr = 0;
        return 0;
    } else if (PyString_Check(v) && PyString_Size(v) == 1) {
        *attr = PyString_AsString(v)[0];
        return 0;
    } else {
        PyErr_BadArgument();
        return -1;
    }
}

static int Parser_setattr(ParserObj *self, char *name, PyObject *v)
{
    if (v == NULL) {
	PyErr_SetString(PyExc_AttributeError, "Cannot delete attribute");
	return -1;
    }
    /* Old Python's had a bug with T_CHAR (missing break at the end of their
     * case statement in structmember.c) - this has been fixed, but Python
     * 1.5.2 still has the bug, so we keep this code around. It's also a
     * convenient place to check for None being passed in. */
    if (strcmp(name, "field_sep") == 0)
        return _set_char_attr(&self->field_sep, v);
    else if (strcmp(name, "quote_char") == 0)
        return _set_char_attr(&self->quote_char, v);
    else if (strcmp(name, "escape_char") == 0)
        return _set_char_attr(&self->escape_char, v);
    else
        return PyMember_Set((char *)self, Parser_memberlist, name, v);
}

static PyObject *csv_parser(PyObject *module,
			    PyObject *args, PyObject *keyword_args);

static char ParserType__doc__[] = 
"CSV parser";

static PyTypeObject ParserType = {
    PyObject_HEAD_INIT(0)
    0,				/*ob_size*/
    "Parser",			/*tp_name*/
    sizeof(ParserObj),		/*tp_basicsize*/
    0,				/*tp_itemsize*/
    /* methods */
    (destructor)Parser_dealloc,	/*tp_dealloc*/
    (printfunc)0,		/*tp_print*/
    (getattrfunc)Parser_getattr, /*tp_getattr*/
    (setattrfunc)Parser_setattr, /*tp_setattr*/
    (cmpfunc)0,			/*tp_compare*/
    (reprfunc)0,		/*tp_repr*/
    0,				/*tp_as_number*/
    0,				/*tp_as_sequence*/
    0,				/*tp_as_mapping*/
    (hashfunc)0,		/*tp_hash*/
    (ternaryfunc)0,	/*tp_call*/
    (reprfunc)0,		/*tp_str*/

    /* Space for future expansion */
    0L, 0L, 0L, 0L,
    ParserType__doc__		/* Documentation string */
};

/* End of code for Parser objects */
/* -------------------------------------------------------- */

static char csv_parser__doc__[] =
"parser(ms_double_quote = 1, field_sep = ',',\n"
"       auto_clear = 1, strict = 0,\n"
"       quote_char = '\"', escape_char = None) -> Parser\n"
"\n"
"Constructs a CSV parser object.  \n"
"\n"
"    ms_double_quote\n"
"        When True, quotes in a fields must be doubled up.\n"
"\n"
"    field_sep\n"
"        Defines the character that will be used to separate\n"
"        fields in the CSV record.\n"
"\n"
"    auto_clear\n"
"        When True, calling parse() will automatically call\n"
"        the clear() method if the previous call to parse() raised an\n"
"        exception during parsing.\n"
"\n"
"    strict\n"
"        When True, the parser will raise an exception on\n"
"        malformed fields rather than attempting to guess the right\n"
"        behavior.\n"
"\n"
"    quote_char\n"
"        Defines the character used to quote fields that\n"
"        contain the field separator or newlines.  If set to None\n"
"        special characters will be escaped using the escape_char.\n"
"\n"
"    escape_char\n"
"        Defines the character used to escape special\n"
"        characters.  Only used if quote_char is None.\n";

static PyObject *csv_parser(PyObject *module,
			    PyObject *args, PyObject *keyword_args)
{
    static char *keywords[] = {
	"ms_double_quote", "field_sep", "auto_clear", "strict", 
	"quote_char", "escape_char", NULL
    };
    PyObject *quote_char, *escape_char;
    ParserObj *self = PyObject_NEW(ParserObj, &ParserType);

    if (self == NULL)
	return NULL;

    self->state = START_RECORD;
    self->fields = PyList_New(0);
    self->ms_double_quote = 1;
    self->auto_clear = 1;
    self->strict = 0;
    self->field_sep = ',';
    quote_char = escape_char = NULL;
    self->quote_char = '"';
    self->escape_char = '\0';

    self->had_parse_error = 0;

    self->field = NULL;
    self->field_size = 0;
    self->field_len = 0;

    self->rec = NULL;
    self->rec_size = 0;
    self->rec_len = 0;
    self->num_fields = 0;

    if (PyArg_ParseTupleAndKeywords(args, keyword_args, "|iciiOO", keywords,
				    &self->ms_double_quote, &self->field_sep,
				    &self->auto_clear, &self->strict,
				    &quote_char, &escape_char)
        && !_set_char_attr(&self->quote_char, quote_char)
	&& !_set_char_attr(&self->escape_char, escape_char))
	return (PyObject*)self;

    Py_DECREF(self);
    return NULL;
}

static struct PyMethodDef csv_methods[] = {
    { "parser", (PyCFunction)csv_parser, METH_VARARGS | METH_KEYWORDS, csv_parser__doc__ },

    { NULL, (PyCFunction)NULL, 0, NULL } /* sentinel */
};

static char csv_module__doc__[] = 
"This module provides class for performing CSV parsing and writing.\n"
"\n"
"The CSV parser object (returned by the parser() function) supports the\n"
"following methods:\n"
"    clear()\n"
"        Discards all fields parsed so far.  If auto_clear is set to\n"
"        zero. You should call this after a parser exception.\n"
"\n"
"    parse(string) -> list of strings\n"
"        Extracts fields from the (partial) CSV record in string.\n"
"        Trailing end of line characters are ignored, so you do not\n"
"        need to strip the string before passing it to the parser. If\n"
"        you pass more than a single line of text, a csv.Error\n"
"        exception will be raised.\n"
"\n"
"    join(sequence) -> string\n"
"        Construct a CSV record from a sequence of fields. Non-string\n"
"        elements will be converted to string.\n"
"\n"
"Typical usage:\n"
"\n"
"    import csv\n"
"    p = csv.parser()\n"
"    file = open('afile.csv')\n"
"    while 1:\n"
"        line = file.readline()\n"
"        if not line:\n"
"            break\n"
"        fields = p.parse(line)\n"
"        if not fields:\n"
"            # multi-line record\n"
"            continue\n"
"        # process the fields\n";

void initcsv(void)
{
    PyObject *module, *dict, *rev = NULL;

    ParserType.ob_type = &PyType_Type;
    /* Create the module and add the functions */
    module = Py_InitModule4("csv", csv_methods,
			    csv_module__doc__,
			    (PyObject*)NULL,PYTHON_API_VERSION);
    if (module == NULL)
	goto error;

    /* Add some symbolic constants to the module. */
    if ((dict = PyModule_GetDict(module)) == NULL)
	goto error;
    if ((rev = PyString_FromString("1.0")) == NULL)
	goto error;
    if (PyDict_SetItemString(dict, "__version__", rev) < 0)
	goto error;

    /* Add the CSV exception object to the module. */
    if ((error_obj = PyErr_NewException("csv.Error", NULL, NULL)) == NULL)
	goto error;

    PyDict_SetItemString(dict, "Error", error_obj);

error:
    Py_XDECREF(rev);
    Py_XDECREF(error_obj);
    /* Check for errors */
    if (PyErr_Occurred())
	Py_FatalError("can't initialize module csv");
}
