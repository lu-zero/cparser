/*
 * This file is part of cparser.
 * Copyright (C) 2012 Matthias Braun <matze@braunis.de>
 */
#include <stdarg.h>
#include <stdio.h>

#include "diagnostic.h"
#include "adt/error.h"
#include "entity_t.h"
#include "separator_t.h"
#include "symbol_t.h"
#include "ast.h"
#include "type.h"
#include "unicode.h"

/** Number of occurred errors. */
unsigned error_count             = 0;
/** Number of occurred warnings. */
unsigned warning_count           = 0;
bool     show_column             = true;
bool     diagnostics_show_option = true;

static const position_t *curr_pos = NULL;

/**
 * prints an additional source position
 */
static void print_position(FILE *out, const position_t *pos)
{
	fprintf(out, "at line %u", pos->lineno);
	if (show_column)
		fprintf(out, ":%u", (unsigned)pos->colno);
	if (curr_pos == NULL || curr_pos->input_name != pos->input_name)
		fprintf(out, " of \"%s\"", pos->input_name);
}

static void fpututf32(utf32 const c, FILE *const out)
{
	if (c < 0x80U) {
		fputc(c, out);
	} else if (c < 0x800) {
		fputc(0xC0 | (c >> 6), out);
		fputc(0x80 | (c & 0x3F), out);
	} else if (c < 0x10000) {
		fputc(0xE0 | ( c >> 12), out);
		fputc(0x80 | ((c >>  6) & 0x3F), out);
		fputc(0x80 | ( c        & 0x3F), out);
	} else {
		fputc(0xF0 | ( c >> 18), out);
		fputc(0x80 | ((c >> 12) & 0x3F), out);
		fputc(0x80 | ((c >>  6) & 0x3F), out);
		fputc(0x80 | ( c        & 0x3F), out);
	}
}

/**
 * Issue a diagnostic message.
 */
static void diagnosticvf(char const *fmt, va_list ap)
{
	for (char const *f; (f = strchr(fmt, '%')); fmt = f) {
		fwrite(fmt, sizeof(*fmt), f - fmt, stderr); // Print till '%'.
		++f; // Skip '%'.

		bool extended  = false;
		bool flag_zero = false;
		bool flag_long = false;
		for (;; ++f) {
			switch (*f) {
			case '#': extended  = true; break;
			case '0': flag_zero = true; break;
			case 'l': flag_long = true; break;
			default:  goto done_flags;
			}
		}
done_flags:;

		int field_width = 0;
		if (*f == '*') {
			++f;
			field_width = va_arg(ap, int);
		}

		switch (*f++) {
		case '%':
			fputc('%', stderr);
			break;

		case 'c': {
			if (flag_long) {
				const utf32 val = va_arg(ap, utf32);
				fpututf32(val, stderr);
			} else {
				const unsigned char val = (unsigned char) va_arg(ap, int);
				fputc(val, stderr);
			}
			break;
		}

		case 'd': {
			const int val = va_arg(ap, int);
			fprintf(stderr, "%d", val);
			break;
		}

		case 's': {
			const char* const str = va_arg(ap, const char*);
			fputs(str, stderr);
			break;
		}

		case 'S': {
			const string_t *str = va_arg(ap, const string_t*);
			for (size_t i = 0; i < str->size; ++i) {
				fputc(str->begin[i], stderr);
			}
			break;
		}

		case 'u': {
			const unsigned int val = va_arg(ap, unsigned int);
			fprintf(stderr, "%u", val);
			break;
		}

		case 'X': {
			unsigned int const val  = va_arg(ap, unsigned int);
			char  const *const xfmt = flag_zero ? "%0*X" : "%*X";
			fprintf(stderr, xfmt, field_width, val);
			break;
		}

		case 'Y': {
			const symbol_t *const symbol = va_arg(ap, const symbol_t*);
			if (symbol == NULL)
				fputs("(null)", stderr);
			else
				fputs(symbol->string, stderr);
			break;
		}

		case 'E': {
			const expression_t* const expr = va_arg(ap, const expression_t*);
			print_expression(expr);
			break;
		}

		case 'Q': {
			const unsigned qualifiers = va_arg(ap, unsigned);
			print_type_qualifiers(qualifiers, QUAL_SEP_NONE);
			break;
		}

		case 'T': {
			const type_t* const type = va_arg(ap, const type_t*);
			const symbol_t*     sym  = NULL;
			if (extended) {
				sym = va_arg(ap, const symbol_t*);
			}
			print_type_ext(type, sym, NULL);
			break;
		}

		case 'K': {
			const token_t* const token = va_arg(ap, const token_t*);
			print_token(stderr, token);
			break;
		}

		case 'k': {
			if (extended) {
				va_list* const toks = va_arg(ap, va_list*);
				separator_t    sep  = { "", va_arg(ap, const char*) };
				for (;;) {
					const token_kind_t tok = (token_kind_t)va_arg(*toks, int);
					if (tok == 0)
						break;
					fputs(sep_next(&sep), stderr);
					print_token_kind(stderr, tok);
				}
			} else {
				const token_kind_t token = (token_kind_t)va_arg(ap, int);
				print_token_kind(stderr, token);
			}
			break;
		}

		case 'N': {
			entity_t const *const ent = va_arg(ap, entity_t const*);
			if (extended && is_declaration(ent)) {
				print_type_ext(ent->declaration.type, ent->base.symbol, NULL);
			} else {
				char     const *const kind = get_entity_kind_name(ent->kind);
				symbol_t const *const sym  = ent->base.symbol;
				if (sym) {
					fprintf(stderr, "%s %s", kind, sym->string);
				} else {
					fprintf(stderr, "anonymous %s", kind);
				}
			}
			break;
		}

		case 'P': {
			const position_t *pos = va_arg(ap, const position_t *);
			print_position(stderr, pos);
			break;
		}

		default:
			panic("unknown format specifier");
		}
	}
	fputs(fmt, stderr); // Print rest.
}

void diagnosticf(const char *const fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	curr_pos = NULL;
	diagnosticvf(fmt, ap);
	va_end(ap);
}

static void diagnosticposvf(position_t const *const pos, char const *const kind, char const *const fmt, va_list ap)
{
	FILE *const out = stderr;
	if (pos) {
		fprintf(out, "%s:", pos->input_name);
		if (pos->lineno != 0) {
			fprintf(out, "%u:", pos->lineno);
			if (show_column)
				fprintf(out, "%u:", (unsigned)pos->colno);
		}
		fputc(' ', out);
	}
	fprintf(out, "%s: ", kind);
	curr_pos = pos;
	diagnosticvf(fmt, ap);
}

static void errorvf(const position_t *pos,
                    const char *const fmt, va_list ap)
{
	++error_count;
	diagnosticposvf(pos, "error", fmt, ap);
	fputc('\n', stderr);
	if (is_warn_on(WARN_FATAL_ERRORS))
		exit(EXIT_FAILURE);
}

void errorf(const position_t *pos, const char *const fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	errorvf(pos, fmt, ap);
	va_end(ap);
}

void warningf(warning_t const warn, position_t const* pos, char const *const fmt, ...)
{
	if (pos->is_system_header && !is_warn_on(WARN_SYSTEM))
		return;

	warning_switch_t const *const s = get_warn_switch(warn);
	switch ((unsigned) s->state) {
			char const* kind;
		case WARN_STATE_ON:
			if (is_warn_on(WARN_ERROR)) {
		case WARN_STATE_ON | WARN_STATE_ERROR:
				++error_count;
				kind = "error";
			} else {
		case WARN_STATE_ON | WARN_STATE_NO_ERROR:
				++warning_count;
				kind = "warning";
			}
			va_list ap;
			va_start(ap, fmt);
			diagnosticposvf(pos, kind, fmt, ap);
			va_end(ap);
			if (diagnostics_show_option)
				fprintf(stderr, " [-W%s]\n", s->name);
			else
				fputc('\n', stderr);
			break;

		default:
			break;
	}
}

static void internal_errorvf(const position_t *pos,
                    const char *const fmt, va_list ap)
{
	diagnosticposvf(pos, "internal error", fmt, ap);
	fputc('\n', stderr);
}

void internal_errorf(const position_t *pos, const char *const fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	internal_errorvf(pos, fmt, ap);
	va_end(ap);
	abort();
}
