/* Copyright 2010 Michael Poole.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <ctype.h>     /* isspace() */
#include <errno.h>     /* errno, EINPROGRESS */
#include <inttypes.h>  /* sized integer types *and formatting* */
#include <stdio.h>     /* fprintf(), stdout */
#include <stdlib.h>    /* EXIT_SUCCESS, EXIT_FAILURE */
#include <string.h>    /* strerror() */
#include <unistd.h>    /* getopt(), etc. */

/* Lookup tables. */
struct indexed_item {
        const char *name;
        unsigned int value;
};

static const char *main_input_names[] = {
        "Constant",
        "Variable",
        "Relative",
        "Wrap",
        "Non-Linear",
        "No Preferred",
        "Null state",
        "Reserved",
        "Buffered Bytes",
        NULL
};

static const char *main_output_names[] = {
        "Constant",
        "Variable",
        "Relative",
        "Wrap",
        "Non-Linear",
        "No Preferred",
        "Null state",
        "Volatile",
        "Buffered Bytes",
        NULL
};

static const char *main_feature_names[] = {
        "Constant",
        "Variable",
        "Relative",
        "Wrap",
        "Non-Linear",
        "No Preferred",
        "Null state",
        "Volatile",
        "Buffered Bytes",
        NULL
};

static const struct indexed_item main_collection_names[] = {
        { "Physical", 0 },
        { "Application", 1 },
        { "Logical", 2 },
        { "Report", 3 },
        { "Named Array", 4 },
        { "Usage Switch", 5 },
        { "Usage Modifier", 6 },
        { NULL, 0 }
};

static const struct indexed_item tag_formats[] = {
        { "Usage Page (%#x)",      0x04 },
        { "Usage (%#x)",           0x08 },
        { "Logical Minimum (%d)",  0x14 },
        { "Usage Minimum (%d)",    0x18 },
        { "Logical Maximum (%d)",  0x24 },
        { "Usage Maximum (%d)",    0x28 },
        { "Physical Minimum (%d)", 0x34 },
        { "Designator Index (%d)", 0x38 },
        { "Physical Maximum (%d)", 0x44 },
        { "Designator Minimum (%d)", 0x48 },
        { "Unit Exponent (%d)",    0x54 },
        { "Designator Maximum (%d)", 0x58 },
        { "Unit (%#x)",            0x64 },
        { "Report Size (%d)",      0x74 },
        { "String Index (%d)",     0x78 },
        { "Report ID (%#x)",       0x84 },
        { "String Minimum (%d)",   0x88 },
        { "Report Count (%d)",     0x94 },
        { "String Maximum (%d)",   0x98 },
        { "Push (%d)",             0xa4 },
        { "Delimiter (%d)",        0xa8 },
        { "Pop (%d)",              0xb4 },
        { "End Collection",        0xc0 },
        { NULL, 0 }
};

/* Formatting and parsing functions. */

unsigned char hextab[256];
unsigned char data[65536];
int length;

void init_hex(void)
{
        const char hexdigits[] = "0123456789abcdef";
        int ii;

        for (ii = 0; hexdigits[ii] != '\0'; ii++) {
                hextab[tolower(hexdigits[ii])] = ii;
                hextab[toupper(hexdigits[ii])] = ii;
        }
}

unsigned char fromhex(char ch)
{
        return hextab[(unsigned char)ch];
}

void parse_line(const char line[])
{
        int jj;

        for (jj = 0; line[jj] != '\0'; jj++) {
                while (isspace(line[jj])) {
                        jj++;
                }

                if (isxdigit(line[jj+0]) && isxdigit(line[jj+1])) {
                        data[length++] = 0
                                | (hextab[(unsigned char)line[jj+0]] << 4)
                                | (hextab[(unsigned char)line[jj+1]] << 0)
                                ;
                }
        }
}

void print_bitfield(const char *class, const char *names[], unsigned int data)
{
        int ii;
        int hits;

        fputs(class, stdout);
        fputs(" (", stdout);
        for (ii = hits = 0; (data >> ii) != 0 && names[ii] != NULL; ii++) {
                if ((data >> ii) & 1) {
                        if (hits > 0) {
                                fputs(", ", stdout);
                        }
                        fputs(names[ii], stdout);
                        hits++;
                }
        }
        if (data >> ii) {
                fprintf(stdout, "%sReserved (%#x)",
                        hits ? ", " : "",
                        data >> ii << ii);
        }
        fputs(")", stdout);
}

const char *find_indexed(unsigned int data, const struct indexed_item items[])
{
        int ii;

        for (ii = 0; items[ii].name != NULL; ii++) {
                if (items[ii].value == data) {
                        break;
                }
        }

        return items[ii].name;
}

void print_indexed(const char *class, const struct indexed_item items[], unsigned int data)
{
        const char *name;

        fputs(class, stdout);
        fputs(" (", stdout);
        name = find_indexed(data, items);
        if (name != NULL) {
                fputs(name, stdout);
        } else {
                fprintf(stdout, "Reserved (%d)", data);
        }
        fputs(")", stdout);
}

void print_descriptor(void)
{
        const char *fmt;
        int indent;
        int ii;
        int len;

        for (ii = indent = 0; ii < length; ii += len) {
                unsigned char tag = data[ii];
                unsigned int param = 0;

                /* Apple Magic Mouse descriptor ends with null byte.
                 * Otherwise, put a comma and newline between items
                 */
                if (tag == 0 && ii == length - 1) {
                        break;
                } else if (ii > 0) {
                        fprintf(stdout, ",\n");
                }

                /* Indent appropriately. */
                if (tag == 0xc0) { /* End Collection */
                        indent -= 2;
                }
                if (indent > 0) {
                        fprintf(stdout, "%*s", indent, " ");
                }

                /* Bail if we see a long item. */
                if (tag == 0xfe) {
                        fprintf(stderr, "Long items (pos %d) not supported!\n", ii);
                        len = 2 + data[ii + 1];
                        continue;
                }

                /* Get argument data. */
                len = 1;
                switch (tag & 3) {
                case 3:
                        param |= data[ii+4] << 24;
                        param |= data[ii+3] << 16;
                        len += 2;
                case 2:
                        param |= data[ii+2] << 8;
                        len++;
                case 1:
                        param |= data[ii+1] << 0;
                        len++;
                }

                /* What are we looking at here? */
                switch (tag & 0xfc) {
                case 0x80:
                        print_bitfield("Input", main_input_names, param);
                        break;
                case 0x90:
                        print_bitfield("Output", main_output_names, param);
                        break;
                case 0xa0:
                        print_indexed("Collection", main_collection_names, param);
                        indent += 2;
                        break;
                case 0xb0:
                        print_bitfield("Feature", main_feature_names, param);
                        break;
                default:
                        fmt = find_indexed(tag & 0xfc, tag_formats);
                        if (fmt != NULL) {
                                fprintf(stdout, fmt, param);
                        } else {
                                fprintf(stdout, "Reserved tag (%#x, data=%#x)", tag, param);
                        }
                }
        }
        fprintf(stdout, "\n");
}

int main(int argc, char *argv[])
{
        char line[4096];
        int ii;

        init_hex();

        for (ii = optind; ii < argc; ++ii) {
                const char *fname;
                FILE *str;

                fname = argv[ii];
                if (!strcmp(fname, "-")) {
                        str = stdin;
                } else {
                        str = fopen(fname, "r");
                }

                length = 0;
                while (fgets(line, sizeof(line), str) != NULL) {
                        parse_line(line);
                }

                if (str != stdin) {
                        fclose(str);
                }

                print_descriptor();
        }

        return EXIT_SUCCESS;
}
