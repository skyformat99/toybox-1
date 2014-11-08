/* sed.c - stream editor. Thing that does s/// and other stuff.
 *
 * Copyright 2014 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/sed.html
 *
 * What happens when first address matched, then EOF? How about ",42" or "1,"
 * Does $ match last line of file or last line of input
 * If file doesn't end with newline
 * command preceded by whitespace. whitespace before rw or s///w file
 * space before address
 * numerical addresses that cross, select one line
 * test backslash escapes in regex; share code with printf?
 * address counts lines cumulatively across files
 * Why can't I start an address with \\ (posix says no, but _why_?)
 * Fun with \nblah\nn vs \tblah\tt
 *
 * echo -e "one\ntwo\nthree" | sed -n '$,$p'

USE_SED(NEWTOY(sed, "(version)e*f*inr", TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_LOCALE))

config SED
  bool "sed"
  default n
  help
    usage: sed [-inr] [-e SCRIPT]...|SCRIPT [-f SCRIPT_FILE]... [FILE...]

    Stream editor. Apply one or more editing SCRIPTs to each line of input
    (from FILE or stdin) producing output (by default to stdout).

    -e	add SCRIPT to list
    -f	add contents of SCRIPT_FILE to list
    -i	Edit each file in place.
    -n	No default output. (Use the p command to output matched lines.)
    -r	Use extended regular expression syntax.
    -s	Treat input files separately (implied by -i)

    A SCRIPT is a series of one or more COMMANDs separated by newlines or
    semicolons. All -e SCRIPTs are concatenated together as if separated
    by newlines, followed by all lines from -f SCRIPT_FILEs, in order.
    If no -e or -f SCRIPTs are specified, the first argument is the SCRIPT.

    Each COMMAND may be preceded by an address which limits the command to
    apply only to the specified line(s). Commands without an address apply to
    every line. Addresses are of the form:

      [ADDRESS[,ADDRESS]]COMMAND

    The ADDRESS may be a decimal line number (starting at 1), a /regular
    expression/ within a pair of forward slashes, or the character "$" which
    matches the last line of input. (In -s or -i mode this matches the last
    line of each file, otherwise just the last line of the last file.) A single
    address matches one line, a pair of comma separated addresses match
    everything from the first address to the second address (inclusive). If
    both addresses are regular expressions, more than one range of lines in
    each file can match.

    REGULAR EXPRESSIONS in sed are started and ended by the same character
    (traditionally / but anything except a backslash or a newline works).
    Backslashes may be used to escape the delimiter if it occurs in the
    regex, and for the usual printf escapes (\abcefnrtv and octal, hex,
    and unicode). An empty regex repeats the previous one. ADDRESS regexes
    (above) require the first delimeter to be escaped with a backslash when
    it isn't a forward slash (to distinguish it from the COMMANDs below).

    Sed mostly operates on individual lines one at a time. It reads each line,
    processes it, and either writes it to the output or discards it before
    reading the next line. Sed can remember one additional line in a separate
    buffer (using the h, H, g, G, and x commands), and can read the next line
    of input early (using the n and N command), but other than that command
    scripts operate on individual lines of text.

    Each COMMAND starts with a single character. The following commands take
    no arguments:

      {  Start a new command block, continuing until a corresponding "}".
         Command blocks may nest. If the block has an address, commands within
         the block are only run for lines within the block's address range.

      }  End command block (this command cannot have an address)

      d  Delete this line and move on to the next one
         (ignores remaining COMMANDs)

      D  Delete one line of input and restart command SCRIPT (same as "d"
         unless you've glued lines together with "N" or similar)

      g  Get remembered line (overwriting current line)

      G  Get remembered line (appending to current line)

      h  Remember this line (overwriting remembered line)

      H  Remember this line (appending to remembered line, if any)

      l  Print this line, escaping \abfrtv (but leaving \n as a newline),
         using octal escapes for other nonprintable characters, and
         wrapping lines to terminal width with a backslash and newline

      n  Print default output and read next line, replacing current line
         (If no next line available, quit processing script)

      N  Append next line of input to this line, separated by a newline
         (This advances the line counter for address matching and "=", if no
         next line available quit processing script without default output)

      p  Print this line

      P  Print this line up to first newline (from "N")

      q  Quit (print default output, no more commands processed or lines read)

      x  Exchange this line with remembered line (overwrite in both directions)

      =  Print the current line number (followed by a newline)

    The following commands (may) take an argument. ("b", "s", "t", "T", "y"
    and ":" may be ended with semicolons, the rest eat at least one line.)

      a [text]   Append text to output before attempting to read next line,
                 if text ends with unescaped "\" append next line of script

      b [label]  Branch, jumps to :label (or with no label, to end of SCRIPT)

      c [text]   Delete current address range and print text instead,
                 if text ends with unescaped "\" append next line of script

      i [text]   Print text, if text ends with unescaped "\" append next
                 line of script

      r [file]   Append contents of file to output before attempting to read
                 next line.

      s/S/R/F    Search for regex S, replace matched text with R using flags F.
                 The first character after the "s" (anything but newline or
                 backslash) is the delimiter, escape with \ to use normally.

                 The replacement text may contain "&" to substitute the matched
                 text (escape it with backslash for a literal &), or \1 through
                 \9 to substitute a parenthetical subexpression in the regex.
                 You can also use the normal backslash escapes such as \n and
                 a backslash at the end of the line appends the next line.

                 The flags are:

                 [0-9]    A number, substitute only that occurrence of pattern
                 g        Global, substitute all occurrences of pattern
                 i        Ignore case when matching
                 p        Print the line if match was found and replaced
                 w [file] Write (append) line to file if match replaced

      t [label]  Test, jump to :label only if an "s" command found a match in
                 this line since last test (replacing with same text counts)

      T [label]  Test false, jump only if "s" hasn't found a match.

      w [file]   Write (append) line to file

      y/old/new/ Change each character in 'old' to corresponding character
                 in 'new' (with standard backslash escapes, delimiter can be
                 any repeated character except \ or \n)

      : [label]  Labeled target for jump commands

      #  Comment, ignore rest of this line of SCRIPT

    Deviations from posix: we allow extended regular expressions with -r,
    editing in place with -i, separate with -s, printf escapes in text, line
    continuations, semicolons after all commands, 2-address anywhere an
    address is allowed, "T" command.
*/

#define FOR_sed
#include "toys.h"

GLOBALS(
  struct arg_list *f;
  struct arg_list *e;

  // processed pattern list
  struct double_list *pattern;

  char *nextline, *remember;
  void *restart;
  long nextlen, rememberlen, count;
  int fdout, noeol;
)

struct step {
  struct step *next, *prev;

  // Begin and end of each match
  long lmatch[2];
  int rmatch[2]; // offset to regex_t, because realloc() would confuse pointer
  unsigned not, hit, sflags;

  int arg1, arg2, arg3;  // offset start of to argument (string or regex_t)
  char c; // action
};

// Write out line with potential embedded NUL, handling eol/noeol
static int emit(char *line, long len, int eol)
{
  if (TT.noeol && !writeall(TT.fdout, "\n", 1)) return 1;
  if (eol) line[len++] = '\n';
  TT.noeol = !eol;
  if (len != writeall(TT.fdout, line, len)) {
    perror_msg("short write");

    return 1;
  }

  return 0;
}

// Do regex matching handling embedded NUL bytes in string. Note that
// neither the pattern nor the match can currently include NUL bytes
// (even with wildcards) and string must be nul terminated.
static int ghostwheel(regex_t *preg, char *string, long len, int nmatch,
  regmatch_t pmatch[], int eflags)
{
/*
  // todo: this
  long start = 0, rc = 0, matches = 0;

  for (;;) {
    long new = strlen(string+start);

    // eflags nobegin noend
    rc |= regexec(preg, string+start, nmatch-matches, pmatch+matches, eflags);
    if ((start += end + 1) >= len) break;
  }

  return rc;
*/
  return regexec(preg, string, nmatch, pmatch, eflags);

}

// Extend allocation to include new string.

static char *extend_string(char **old, char *new, int oldlen, int newlen)
{
  int newline = newlen < 0;
  char *s;

  if (newline) newlen = -newlen;
  s = *old = xrealloc(*old, oldlen+newlen+newline+1);
  if (newline) s[oldlen++] = '\n';
  memcpy(s+oldlen, new, newlen);
  s[oldlen+newlen] = 0;

  return s+oldlen+newlen;
}

// Apply pattern to line from input file
static void walk_pattern(char **pline, long plen)
{
  char *line = TT.nextline, *append = 0;
  long len = TT.nextlen;
  struct step *logrus;
  int eol = 0, tea = 0;

  // Grab next line for deferred processing (EOF detection: we get a NULL
  // pline at EOF to flush last line). Note that only end of _last_ input
  // file matches $ (unless we're doing -i).
  if (pline) {
    TT.nextline = *pline;
    TT.nextlen = plen;
    *pline = 0;
  }

  if (!line || !len) return;
  if (line[len-1] == '\n') line[--len] = eol++;
  TT.count++;

  logrus = TT.restart ? TT.restart : (void *)TT.pattern;
  TT.restart = 0;
  while (logrus) {
    char *str, c = logrus->c;

    // Have we got a matching range for this rule?
    if (*logrus->lmatch || *logrus->rmatch) {
      int miss = 0;
      long lm;

      // In a match that might end?
      if (logrus->hit) {
        if (!(lm = logrus->lmatch[1])) {
          if (!logrus->rmatch[1]) logrus->hit = 0;
          else {
            void *rm = logrus->rmatch[1] + (char *)logrus;

            // regex match end includes matching line, so defer deactivation
            if (!ghostwheel(rm, line, len, 0, 0, 0)) miss = 1;
          }
        } else if (lm > 0 && lm < TT.count) logrus->hit = 0;

      // Start a new match?
      } else {
        if (!(lm = *logrus->lmatch)) {
          void *rm = *logrus->rmatch + (char *)logrus;

          if (!ghostwheel(rm, line, len, 0, 0, 0)) logrus->hit++;
        } else if (lm == TT.count || (lm == -1 && !pline)) logrus->hit++;
      } 

      // Didn't match?
      if (!(logrus->hit ^ logrus->not)) {

        // Handle skipping curly bracket command group
        if (c == '{') {
          int curly = 1;

          while (curly) {
            logrus = logrus->next;
            if (logrus->c == '{') curly++;
            if (logrus->c == '}') curly--;
          }
        }
        continue;
      }
      // Deferred disable from regex end match
      if (miss) logrus->hit = 0;
    }

    // Process command

    if (c == 'a') {
      long alen = append ? strlen(append) : 0;

      str = logrus->arg1+(char *)logrus;
      extend_string(&append, str, alen, -strlen(str));
    } else if (c == 'b') {
      str = logrus->arg1+(char *)logrus;

      if (!*str) break;
      for (logrus = (void *)TT.pattern; logrus; logrus = logrus->next)
        if (logrus->c == ':' && !strcmp(logrus->arg1+(char *)logrus, str))
          break;
      if (!logrus) error_exit("no :%s", str);
    } else if (c == 'd') goto done;
    else if (c == 'D') {
      // Delete up to \n or end of buffer
      for (str = line; !*str || *str == '\n'; str++);
      len -= str - line;
      memmove(line, str, len);
      line[len] = 0;

      // restart script
      logrus = (void *)TT.pattern;
      continue;
    } else if (c == 'g') {
      free(line);
      line = xstrdup(TT.remember);
      len = TT.rememberlen;
    } else if (c == 'G') {
      line = xrealloc(line, len+TT.rememberlen+2);
      line[len++] = '\n';
      memcpy(line+len, TT.remember, TT.rememberlen);
      line[len += TT.rememberlen] = 0;
    } else if (c == 'h') {
      free(TT.remember);
      TT.remember = xstrdup(line);
      TT.rememberlen = len;
    } else if (c == 'H') {
      TT.remember = xrealloc(TT.remember, TT.rememberlen+len+2);
      TT.remember[TT.rememberlen++] = '\n';
      memcpy(TT.remember+TT.rememberlen, line, len);
      TT.remember[TT.rememberlen += len] = 0;
    } else if (c == 'l') {
      error_exit("todo: l");
    } else if (c == 'n') {
      TT.restart = logrus->next;

      break;
    } else if (c == 'N') {
      if (pline) {
        TT.restart = logrus->next;
        extend_string(&line, TT.nextline, plen, -TT.nextlen);
        free(TT.nextline);
        TT.nextline = line;
      }

      goto append; 
    } else if (c == 'p') {
      if (emit(line, len, eol)) break;
    } else if (c == 'q') break;
    else if (c == 'x') {
      long swap = TT.rememberlen;

      str = TT.remember;
      TT.remember = line;
      line = str;
      TT.rememberlen = len;
      len = swap;
    } else if (c == '=') xprintf("%ld\n", TT.count);
    // labcirstTwy
    else if (c != ':') error_exit("todo: %c", c);

    logrus = logrus->next;
  }

  if (!(toys.optflags & FLAG_n)) emit(line, len, eol);

done:
  free(line);

append:
  if (append) {
    emit(append, strlen(append), 1);
    free(append);
  }
}

// Genericish function, can probably get moved to lib.c

// Iterate over lines in file, calling function. Function can write NULL to
// the line pointer if they want to keep it, otherwise line is freed.
// Passed file descriptor is closed at the end of processing.
static void do_lines(int fd, char *name, void (*call)(char **pline, long len))
{
  FILE *fp = fd ? xfdopen(fd, "r") : stdin;

  for (;;) {
    char *line = 0;
    ssize_t len;

    len = getline(&line, (void *)&len, fp);
    if (len > 0) {
      call(&line, len);
      free(line);
    } else break;
  }

  if (fd) fclose(fp);
}

// Iterate over newline delimited data blob (potentially with embedded NUL),
// call function on each line.
static void chop_lines(char *data, long len, void (*call)(char **p, long l))
{
  long ll;

  for (ll = 0; ll < len; ll++) {
    if (data[ll] == '\n') {
      char *c = data;

      data[ll] = 0;
      call(&c, len);
      data[ll++] = '\n';
      data += ll;
      len -= ll;
      ll = -1;
    }
  }
  if (len) call(&data, len);
}

static void do_sed(int fd, char *name)
{
  int i = toys.optflags & FLAG_i;

  if (i) {
    // todo: rename dance
  }
  do_lines(fd, name, walk_pattern);
  if (i) {
    walk_pattern(0, 0);

    // todo: rename dance
  }
}

// Note: removing backslash escapes edits the source string, which could
// be from the environment space via -e, which could screw up what
// "ps" sees, and I'm ok with that.

// extract regex up to delimeter, converting \escapes
// You can't use \ as delimiter because how would you escape anything?
static int parse_regex(regex_t *reg, char **pstr, char delim)
{
  char *to, *from = *pstr;
  int rc;

  if (delim == '\\') rc = 0;
  else {
    for (to = from; *from != delim; *(to++) = *(from++)) {
      if (!*from) break;
      if (*from == '\\') {
        if (!from[1]) break;

        // Check escaped end delimiter before printf style escapes.
        if (from[1] == delim) from++;
        else {
          char c = unescape(from[1]);

          if (c) {
            *to = c;
            from++;
          }
        }
      }
    }
    rc = (*from == delim);
  }

  if (rc) {
    delim = *to;
    *to = 0;
    xregcomp(reg, *pstr, ((toys.optflags & FLAG_r)*REG_EXTENDED)|REG_NOSUB);
    *to = delim;
  }
  *pstr = from + rc;
  
  return rc;
}

// Translate primal pattern into walkable form.
static void jewel_of_judgement(char **pline, long len)
{
  struct step *corwin = (void *)TT.pattern;
  char *line = *pline, *reg, c;
  int i;

  // Append additional line to pattern argument string?
  if (corwin && corwin->prev->hit) {
    // Remove half-finished entry from list so remalloc() doesn't confuse it
    TT.pattern = TT.pattern->prev;
    corwin = dlist_pop(&TT.pattern);
    corwin->hit = 0;
    c = corwin->c;
    reg = (char *)corwin;
    reg += corwin->arg1 + strlen(reg + corwin->arg1);

    // Resume parsing
    goto append;
  }

  // Loop through commands in line

  corwin = 0;
  for (;;) {
    if (corwin) dlist_add_nomalloc(&TT.pattern, (void *)corwin);

    while (isspace(*line) || *line == ';') line++;
    if (!*line || *line == '#') return;

    memset(toybuf, 0, sizeof(struct step));
    corwin = (void *)toybuf;
    reg = toybuf + sizeof(struct step);

    // Parse address range (if any)
    for (i = 0; i < 2; i++) {
      if (*line == ',') line++;
      else if (i) break;

      if (isdigit(*line)) corwin->lmatch[i] = strtol(line, &line, 0);
      else if (*line == '$') {
        corwin->lmatch[i] = -1;
        line++;
      } else if (*line == '/' || *line == '\\') {
        char delim = *(line++);

        if (delim == '\\') {
          if (!*line) goto brand;
          delim = *(line++);
        }

        if (!parse_regex((void *)reg, &line, delim)) goto brand;
        corwin->rmatch[i] = reg-toybuf;
        reg += sizeof(regex_t);
      } else break;
    }

    while (isspace(*line)) line++;
    if (!*line) break;

    while (*line == '!') corwin->not = 1;
    while (isspace(*line)) line++;

    c = corwin->c = *(line++);
    if (strchr("}:", c) && i) break;
    if (strchr("aiqr=", c) && i>1) break;

    // Add step to pattern
    corwin = xmalloc(reg-toybuf);
    memcpy(corwin, toybuf, reg-toybuf);
    reg = (reg-toybuf) + (char *)corwin;

    // Parse arguments by command type
    if (c == '{') TT.nextlen++;
    else if (c == '}') {
      if (!TT.nextlen--) break;
    } else if (c == 's') {
      char delim = *line;
      int end;

      // s/pattern/replacement/flags

      if (delim) line++;
      else break;

      // get pattern
      end = reg - (char *)corwin;
      corwin = xrealloc(corwin, end+sizeof(regex_t));
      reg = end + (char *)corwin;
      if (!parse_regex((void *)reg, &line, delim)) break;
      corwin->arg1 = reg-toybuf;
      reg += sizeof(regex_t);

      // get replacement

      for (end = 0; line[end] != delim; end++) {
        if (line[end] == '\\') end++;
        if (!line[end]) goto brand;
      }

      corwin->arg2 = reg - (char*)corwin;
      reg = extend_string((void *)&corwin, line, corwin->arg2, end);
      line += end+1;

      // flags:
      //           [0-9]    A number, substitute only that occurrence of pattern
      //           g        Global, substitute all occurrences of pattern
      //           p        Print the line if match was found and replaced
      //           w [file] Write (append) line to file if match replaced
      //           i        case insensitive match

      for (;;) {
        long l;

        line++;
        if (!*line || *line == ';') break;
        if (isspace(*line)) continue;

        if (0 <= (l = stridx("gpi", *line))) corwin->sflags |= 1<<l;
        else if (!corwin->sflags >> 4 && 0<(l = strtol(line+end, &line, 10))) {
          corwin->sflags |= l << 4;
          line--;
        } else if (*line == 'w') {
          while (isspace(*++line));
          if (!*line) goto brand;
          corwin->arg3 = reg - (char *)corwin;
          reg = extend_string((void *)&corwin, line, corwin->arg3,
                              strlen(line));
        } else goto brand;
      }
    } else if (c == 'y') {
      // y/old/new/
    } else if (strchr("abcirtTw:", c)) {
      int end, class;

      // Trim whitespace from "b ;" and ": blah " but only first space in "w x "

      while (isspace(*line)) line++;
append:
      class = !strchr("btT:", c);
      end = strcspn(line, class ? "" : "; \t\r\n\v\f");

      if (!end) {
        if (!strchr("btT", c)) break;
        continue;
      }

      // Extend allocation to include new string. We use offsets instead of
      // pointers so realloc() moving stuff doesn't break things. Do it
      // here instead of toybuf so there's no maximum size.

      if (!corwin->arg1) corwin->arg1 = reg - (char*)corwin;
      reg = extend_string((void *)&corwin, line, reg - (char *)corwin, end); 

      // Line continuation?
      if (class && reg[-1] == '\\') {
        reg[-1] = 0;
        corwin->hit++;
      }

    // Commands that take no arguments
    } else if (!strchr("{dDgGhHlnNpPqx=", *line)) break;
  }

brand:
  // Reminisce about chestnut trees.
  error_exit("bad pattern '%s'@%ld (%c)", *pline, line-*pline+1, *line);
}

void sed_main(void)
{
  struct arg_list *dworkin;
  char **args = toys.optargs;

  // Lie to autoconf when it asks stupid questions, so configure regexes
  // that look for "GNU sed version %f" greater than some old buggy number
  // don't fail us for not matching their narrow expectations.
  if (toys.optflags & FLAG_version) {
    xprintf("This is not GNU sed version 9.0\n");
    return;
  }

  // Need a pattern. If no unicorns about, fight serpent and take its eye.
  if (!TT.e && !TT.f) {
    if (!*toys.optargs) error_exit("no pattern");
    (TT.e = xzalloc(sizeof(struct arg_list)))->arg = *(args++);
  }

  // Option parsing infrastructure can't interlace "-e blah -f blah -e blah"
  // so handle all -e, then all -f. (At least the behavior's consistent.)

  for (dworkin = TT.e; dworkin; dworkin = dworkin->next)
    chop_lines(dworkin->arg, strlen(dworkin->arg), jewel_of_judgement);
  for (dworkin = TT.f; dworkin; dworkin = dworkin->next)
    do_lines(xopen(dworkin->arg, O_RDONLY), dworkin->arg, jewel_of_judgement);
  dlist_terminate(TT.pattern);
  if (TT.nextlen) error_exit("no }");  

  TT.fdout = 1;
  TT.remember = xstrdup("");

  // Inflict pattern upon input files
  loopfiles_rw(args, O_RDONLY, 0, 0, do_sed);

  if (!(toys.optflags & FLAG_i)) walk_pattern(0, 0);
}
