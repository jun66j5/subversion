/* skel.c --- parsing and unparsing skeletons
 *
 * ================================================================
 * Copyright (c) 2000 Collab.Net.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by Collab.Net (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of Collab.Net.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of Collab.Net.
 */

#include "string.h"
#include "svn_string.h"
#include "fs.h"
#include "convert-size.h"
#include "skel.h"


/* Parsing skeletons.  */

enum char_type {
  type_nothing = 0,
  type_space = 1,
  type_digit = 2,
  type_paren = 3,
  type_name = 4,
};


/* We can't use the <ctype.h> macros here, because they are locale-
   dependent.  The syntax of a skel is specified directly in terms of
   byte values, and is independent of locale.  */

static const enum char_type skel_char_type[256] = {
  0, 0, 0, 0, 0, 0, 0, 0,   0, 1, 1, 0, 1, 1, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  1, 0, 0, 0, 0, 0, 0, 0,   3, 3, 0, 0, 0, 0, 0, 0,
  2, 2, 2, 2, 2, 2, 2, 2,   2, 2, 0, 0, 0, 0, 0, 0,

  /* 64 */
  0, 4, 4, 4, 4, 4, 4, 4,   4, 4, 4, 4, 4, 4, 4, 4,
  4, 4, 4, 4, 4, 4, 4, 4,   4, 4, 4, 3, 0, 3, 0, 0,
  0, 4, 4, 4, 4, 4, 4, 4,   4, 4, 4, 4, 4, 4, 4, 4,
  4, 4, 4, 4, 4, 4, 4, 4,   4, 4, 4, 0, 0, 0, 0, 0,

  /* 128 */
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,

  /* 192 */
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
};


static skel_t *parse (char *data, apr_size_t len,
		      apr_pool_t *pool);
static skel_t *list (char *data, apr_size_t len,
		     apr_pool_t *pool);
static skel_t *implicit_atom (char *data, apr_size_t len,
			      apr_pool_t *pool);
static skel_t *explicit_atom (char *data, apr_size_t len,
			      apr_pool_t *pool);


skel_t *
svn_fs__parse_skel (char *data,
		    apr_size_t len,
		    apr_pool_t *pool)
{
  return parse (data, len, pool);
}


/* Parse any kind of skel object --- atom, or list.  */
static skel_t *
parse (char *data,
       apr_size_t len,
       apr_pool_t *pool)
{
  char c;

  /* The empty string isn't a valid skel.  */
  if (len <= 0)
    return 0;

  c = *data;

  /* Is it a list, or an atom?  */
  if (c == '(')
    return list (data, len, pool);

  /* Is it a string with an implicit length?  */
  if (skel_char_type[(unsigned char) c] == type_name)
    return implicit_atom (data, len, pool);

  /* Otherwise, we assume it's a string with an explicit length;
     svn_fs__getsize will catch the error.  */
  else
    return explicit_atom (data, len, pool);
}


static skel_t *
list (char *data,
      apr_size_t len,
      apr_pool_t *pool)
{
  char *end = data + len;
  char *list_start;

  /* Verify that the list starts with an opening paren.  At the
     moment, all callers have checked this already, but it's more
     robust this way.  */
  if (data >= end || *data != '(')
    return 0;

  /* Mark where the list starts.  */
  list_start = data;

  /* Skip the opening paren.  */
  data++;

  /* Parse the children.  */
  {
    skel_t *children = 0;
    skel_t **tail = &children;

    for (;;)
      {
	skel_t *element;

	/* Skip any whitespace.  */
	while (data < end
	       && skel_char_type[(unsigned char) *data] == type_space)
	  data++;

	/* End of data, but no closing paren?  */
	if (data >= end)
	  return 0;

	/* End of list?  */
	if (*data == ')')
	  {
	    data++;
	    break;
	  }

	/* Parse the next element in the list.  */
	element = parse (data, end - data, pool);
	if (! element)
	  return 0;

	/* Link that element into our list.  */
	element->next = 0;
	*tail = element;
	tail = &element->next;

	/* Advance past that element.  */
	data = element->data + element->len;
      }

    /* Construct the return value.  */
    {
      skel_t *s = NEW (pool, skel_t);

      s->is_atom = 0;
      s->data = list_start;
      s->len = data - list_start;
      s->children = children;
      s->next = 0;

      return s;
    }
  }
}


/* Parse an atom with implicit length --- one that starts with a name
   character, terminated by whitespace or end-of-data.  */
static skel_t *
implicit_atom (char *data,
	       apr_size_t len,
	       apr_pool_t *pool)
{
  char *start = data;
  char *end = data + len;
  skel_t *s;

  /* Verify that the atom starts with a name character.  At the
     moment, all callers have checked this already, but it's more
     robust this way.  */
  if (data >= end || skel_char_type[(unsigned char) *data] != type_name)
    return 0;

  /* Find the end of the string.  */
  while (++data < end && skel_char_type[(unsigned char) *data] != type_space)
    ;

  /* Verify that the required terminating whitespace character is
     present.  */
  if (data >= end || skel_char_type[(unsigned char) *data] != type_space)
    return 0;

  /* Allocate the skel representing this string.  */
  s = NEW (pool, skel_t);
  s->is_atom = 1;
  s->data = start;
  s->len = data - start;

  return s;
}


/* Parse an atom with explicit length --- one that starts with a byte
   length, as a decimal ASCII number.  */
static skel_t *
explicit_atom (char *data,
	       apr_size_t len,
	       apr_pool_t *pool)
{
  char *end = data + len;
  char *next;
  apr_size_t size;
  skel_t *s;

  /* Parse the length.  */
  size = svn_fs__getsize (data, end - data, &next, end - data);
  data = next;

  /* Exit if we overflowed, or there wasn't a valid number there.  */
  if (! data)
    return 0;

  /* Skip the whitespace character after the length.  */
  if (data >= end || skel_char_type[(unsigned char) *data] != type_space)
    return 0;
  data++;

  /* Check the length.  */
  if (data + size > end)
    return 0;

  /* Allocate the skel representing this string.  */
  s = NEW (pool, skel_t);
  s->is_atom = 1;
  s->data = data;
  s->len = size;

  return s;
}



/* Unparsing skeletons.  */

static apr_size_t estimate_unparsed_size (skel_t *, int);
static svn_string_t *unparse (skel_t *, svn_string_t *, int,
			      apr_pool_t *);


svn_string_t *
svn_fs__unparse_skel (skel_t *skel, apr_pool_t *pool)
{
  svn_string_t *str;
  
  /* Allocate a string to hold the data.  */
  str = NEW (pool, svn_string_t);
  str->blocksize = estimate_unparsed_size (skel, 0) + 200;
  str->data = NEWARRAY (pool, char, str->blocksize);
  str->len = 0;

  return unparse (skel, str, 0, pool);
}


/* Return an estimate of the number of bytes that the external
   representation of SKEL will occupy.  DEPTH is the number of lists
   we're inside at the moment, to account for space used by
   indentation.  */
static apr_size_t
estimate_unparsed_size (skel_t *skel, int depth)
{
  if (skel->is_atom)
    {
      if (skel->len < 100)
	/* If we have to use the explicit-length form, that'll be
	   two bytes for the length, one byte for the space, and 
	   the contents.  */
	return skel->len + 3;
      else
	return skel->len + 30;
    }
  else
    {
      int total_len;
      skel_t *child;

      /* Allow space for an indented opening and closing paren, with
         a newline after the opening paren.  */
      total_len = depth * 2 + 2 + depth * 2 + 1;

      depth++;

      /* For each element, allow for some indentation, and a following
         newline.  */
      for (child = skel->children; child; child = child->next)
	total_len += estimate_unparsed_size (child, depth) + (depth * 2) + 1;

      return total_len;
    }
}


/* Return non-zero iff we should use the implicit-length form for SKEL.  
   Assume that SKEL is an atom.  */
static int
use_implicit (skel_t *skel)
{
  /* If it's null, or long, we should use explicit-length form.  */
  if (skel->len == 0
      || skel->len >= 100)
    return 0;

  /* If it doesn't start with a name character, we must use
     explicit-length form.  */
  if (skel_char_type[(unsigned char) skel->data[0]] != type_name)
    return 0;

  /* If it contains any whitespace, then we must use explicit-length
     form.  */
  {
    int i;

    for (i = 1; i < skel->len; i++)
      if (skel_char_type[(unsigned char) skel->data[i]] == type_space)
	return 0;
  }

  /* If we can't reject it for any of the above reasons, then we can
     use implicit-length form.  */
  return 1;
}


/* Append the concrete representation of SKEL to the string STR.
   DEPTH indicates how many lists we're inside; we use it for
   indentation.  Grow S with new space from POOL as necessary.  */
static svn_string_t *
unparse (skel_t *skel, svn_string_t *str, int depth, apr_pool_t *pool)
{
  if (skel->is_atom)
    {
      /* Append an atom to STR.  */
      if (use_implicit (skel))
	{
	  svn_string_appendbytes (str, skel->data, skel->len);
	  svn_string_appendbytes (str, " ", 1);
	}
      else
	{
	  /* Append the length to STR.  */
	  char buf[200];
	  int length_len;

	  length_len = svn_fs__putsize (buf, sizeof (buf), skel->len);
	  if (! length_len)
	    abort ();

	  /* Make sure we have room for the length, the space, and the
             atom's contents.  */
	  svn_string_ensure (str,
			     str->len + length_len + 1 + skel->len);
	  svn_string_appendbytes (str, buf, length_len);
	  str->data[str->len++] = '\n';
	  svn_string_appendbytes (str, skel->data, skel->len);
	}
    }
  else
    {
      /* Append a list to STR.  */
      skel_t *child;
      int i;

      /* The opening paren has been indented by the parent, if necessary.  */
      svn_string_ensure (str, str->len + 1);
      str->data[str->len++] = '(';
      
      depth++;

      /* Append each element.  */
      for (child = skel->children; child; child = child->next)
	{
	  /* Add a newline, and indentation.  */
	  svn_string_ensure (str, str->len + 1 + depth * 2);
	  str->data[str->len++] = '\n';
	  for (i = 0; i < depth * 2; i++)
	    str->data[str->len++] = ' ';
	  unparse (child, str, depth, pool);
	}

      depth--;
      
      /* Add a newline, indentation, and a closing paren.

	 There should be no newline after a closing paren; a skel must
	 entirely fill its string.  If we're part of a parent list,
	 the parent will take care of adding that.  */
      svn_string_ensure (str, str->len + 1 + depth * 2 + 1);
      str->data[str->len++] = '\n';
      for (i = 0; i < depth * 2; i++)
	str->data[str->len++] = ' ';
      str->data[str->len++] = ')';
    }

  return str;
}



/* Examining skels.  */


int
svn_fs__is_atom (skel_t *skel, const char *str)
{
  if (skel
      && skel->is_atom)
    {
      int len = strlen (str);

      return (skel->len == len
	      && ! memcmp (skel->data, str, len));
    }
  else
    return 0;
}


int
svn_fs__list_length (skel_t *skel)
{
  if (! skel
      || skel->is_atom)
    return -1;

  {
    int len = 0;
    skel_t *child;

    for (child = skel->children; child; child = child->next)
      len++;

    return len;
  }
}
