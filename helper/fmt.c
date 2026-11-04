// sprintf_ for the services build, in place of external/printf: the formats this
// firmware uses - %u %d %o %c %s %%, a 0 flag and a width (numbers), a precision
// for strings (.N, .*). (No floats, hex, long long, left-justify or + / space flags.)

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "external/printf/printf.h"

int sprintf_(char *buffer, const char *format, ...)
{
    va_list va;
    va_start(va, format);
    char *o = buffer;
    for (; *format; format++) {
        if (*format != '%') {
            *o++ = *format;
            continue;
        }
        const bool zero = *++format == '0';
        unsigned   width = 0, prec = ~0u;
        while (*format >= '0' && *format <= '9')
            width = width * 10u + (unsigned)(*format++ - '0');
        if (*format == '.') {
            if (*++format == '*') {
                prec = (unsigned)va_arg(va, int);
                format++;
            } else
                for (prec = 0; *format >= '0' && *format <= '9'; format++)
                    prec = prec * 10u + (unsigned)(*format - '0');
        }
        while (*format == 'l')
            format++;
        const char c = *format;
        if (c == 's') {
            for (const char *s = va_arg(va, const char *); *s && prec; prec--)
                *o++ = *s++;
        } else if (c == 'c') {
            *o++ = (char)va_arg(va, int);
        } else if (c == '%') {
            *o++ = '%';
        } else {   // d, u, o
            unsigned   v   = va_arg(va, unsigned);
            const bool neg = c == 'd' && (int)v < 0;
            char       t[12];
            unsigned   n = 0;
            if (neg)
                v = 0u - v;
            do {
                t[n++] = (char)('0' + v % (c == 'o' ? 8u : 10u));
                v /= c == 'o' ? 8u : 10u;
            } while (v);
            unsigned pad = width > n + neg ? width - n - neg : 0u;
            if (!zero)
                for (; pad; pad--)
                    *o++ = ' ';
            if (neg)
                *o++ = '-';
            for (; pad; pad--)
                *o++ = '0';
            while (n)
                *o++ = t[--n];
        }
    }
    *o = 0;
    va_end(va);
    return (int)(o - buffer);
}
