/*
 * config_file.c :  parsing configuration files
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */



#define APR_WANT_STDIO
#include <apr_want.h>

#include <apr_lib.h>
#include <apr_md5.h>
#include "config_impl.h"
#include "svn_io.h"
#include "svn_types.h"
#include "svn_path.h"
#include "svn_auth.h"
#include "svn_md5.h"
#include "svn_hash.h"
#include "svn_private_config.h"


/* File parsing context */
typedef struct parse_context_t
{
  /* This config struct and file */
  svn_config_t *cfg;
  const char *file;

  /* The file descriptor */
  FILE *fd;

  /* The current line in the file */
  int line;

  /* Temporary strings, allocated from the temp pool */
  svn_stringbuf_t *section;
  svn_stringbuf_t *option;
  svn_stringbuf_t *value;

  /* Temporary pool parsing */
  apr_pool_t *pool;
} parse_context_t;


/* Eat chars from FD until encounter non-whitespace, newline, or EOF.
   Set *PCOUNT to the number of characters eaten, not counting the
   last one, and return the last char read (the one that caused the
   break).  */
static APR_INLINE int
skip_whitespace (FILE* fd, int *pcount)
{
  int ch = getc (fd);
  int count = 0;
  while (ch != EOF && ch != '\n' && apr_isspace (ch))
    {
      ++count;
      ch = getc (fd);
    }
  *pcount = count;
  return ch;
}


/* Skip to the end of the line (or file).  Returns the char that ended
   the line; the char is either EOF or newline. */
static APR_INLINE int
skip_to_eoln (FILE *fd)
{
  int ch = getc (fd);
  while (ch != EOF && ch != '\n')
    ch = getc (fd);
  return ch;
}


/* Parse a single option value */
static svn_error_t *
parse_value (int *pch, parse_context_t *ctx)
{
  svn_error_t *err = SVN_NO_ERROR;
  svn_boolean_t end_of_val = FALSE;
  int ch;

  /* Read the first line of the value */
  svn_stringbuf_setempty (ctx->value);
  for (ch = getc (ctx->fd); /* last ch seen was ':' or '=' in parse_option. */
       ch != EOF && ch != '\n';
       ch = getc (ctx->fd))
    {
      const char char_from_int = ch;
      svn_stringbuf_appendbytes (ctx->value, &char_from_int, 1);
    }
  /* Leading and trailing whitespace is ignored. */
  svn_stringbuf_strip_whitespace (ctx->value);

  /* Look for any continuation lines. */
  for (;;)
    {
      if (ch == EOF || end_of_val)
        {
          if (!ferror (ctx->fd))
            {
              /* At end of file. The value is complete, there can't be
                 any continuation lines. */
              svn_config_set (ctx->cfg, ctx->section->data,
                              ctx->option->data, ctx->value->data);
            }
          break;
        }
      else
        {
          int count;
          ++ctx->line;
          ch = skip_whitespace (ctx->fd, &count);

          switch (ch)
            {
            case '\n':
              /* The next line was empty. Ergo, it can't be a
                 continuation line. */
              ++ctx->line;
              end_of_val = TRUE;
              continue;

            case EOF:
              /* This is also an empty line. */
              end_of_val = TRUE;
              continue;

            default:
              if (count == 0)
                {
                  /* This line starts in the first column.  That means
                     it's either a section, option or comment.  Put
                     the char back into the stream, because it doesn't
                     belong to us. */
                  ungetc (ch, ctx->fd);
                  end_of_val = TRUE;
                }
              else
                {
                  /* This is a continuation line. Read it. */
                  svn_stringbuf_appendbytes (ctx->value, " ", 1);

                  for (;
                       ch != EOF && ch != '\n';
                       ch = getc (ctx->fd))
                    {
                      const char char_from_int = ch;
                      svn_stringbuf_appendbytes (ctx->value,
                                                 &char_from_int, 1);
                    }
                  /* Trailing whitespace is ignored. */
                  svn_stringbuf_strip_whitespace (ctx->value);
                }
            }
        }
    }

  *pch = ch;
  return err;
}


/* Parse a single option */
static svn_error_t *
parse_option (int *pch, parse_context_t *ctx)
{
  svn_error_t *err = SVN_NO_ERROR;
  int ch;

  svn_stringbuf_setempty (ctx->option);
  for (ch = *pch;               /* Yes, the first char is relevant. */
       ch != EOF && ch != ':' && ch != '=' && ch != '\n';
       ch = getc (ctx->fd))
    {
      const char char_from_int = ch;
      svn_stringbuf_appendbytes (ctx->option, &char_from_int, 1);
    }

  if (ch != ':' && ch != '=')
    {
      ch = EOF;
      err = svn_error_createf (SVN_ERR_MALFORMED_FILE, NULL,
                               "%s:%d: Option must end with ':' or '='",
                               ctx->file, ctx->line);
    }
  else
    {
      /* Whitespace around the name separator is ignored. */
      svn_stringbuf_strip_whitespace (ctx->option);
      err = parse_value (&ch, ctx);
    }

  *pch = ch;
  return err;
}


/* Read chars until enounter ']', then skip everything to the end of
 * the line.  Set *PCH to the character that ended the line (either
 * newline or EOF), and set CTX->section to the string of characters
 * seen before ']'.
 * 
 * This is meant to be called immediately after reading the '[' that
 * starts a section name.
 */
static svn_error_t *
parse_section_name (int *pch, parse_context_t *ctx)
{
  svn_error_t *err = SVN_NO_ERROR;
  int ch;

  svn_stringbuf_setempty (ctx->section);
  for (ch = getc (ctx->fd);
       ch != EOF && ch != ']' && ch != '\n';
       ch = getc (ctx->fd))
    {
      const char char_from_int = ch;
      svn_stringbuf_appendbytes (ctx->section, &char_from_int, 1);
    }

  if (ch != ']')
    {
      ch = EOF;
      err = svn_error_createf (SVN_ERR_MALFORMED_FILE, NULL,
                               "%s:%d: Section header must end with ']'",
                               ctx->file, ctx->line);
    }
  else
    {
      /* Everything from the ']' to the end of the line is ignored. */
      ch = skip_to_eoln (ctx->fd);
      if (ch != EOF)
        ++ctx->line;
    }

  *pch = ch;
  return err;
}


svn_error_t *
svn_config__sys_config_path (const char **path_p,
                             const char *fname,
                             apr_pool_t *pool)
{
  /* ### This never actually returns error in practice.  Perhaps the
     prototype should change? */

  *path_p = NULL;

  /* Note that even if fname is null, svn_path_join_many will DTRT. */

#ifdef SVN_WIN32
  {
    const char *folder;
    SVN_ERR (svn_config__win_config_path (&folder, TRUE, pool));
    *path_p = svn_path_join_many (pool, folder,
                                  SVN_CONFIG__SUBDIRECTORY, fname, NULL);
  }

#else  /* ! SVN_WIN32 */

 *path_p = svn_path_join_many (pool, SVN_CONFIG__SYS_DIRECTORY, fname, NULL);

#endif /* SVN_WIN32 */

  return SVN_NO_ERROR;
}


svn_error_t *
svn_config__user_config_path (const char **path_p,
                              const char *fname,
                              apr_pool_t *pool)
{
  /* ### This never actually returns error in practice.  Perhaps the
     prototype should change? */

  *path_p = NULL;
  
  /* Note that even if fname is null, svn_path_join_many will DTRT. */

#ifdef SVN_WIN32
  {
    const char *folder;
    SVN_ERR (svn_config__win_config_path (&folder, FALSE, pool));
    *path_p = svn_path_join_many (pool, folder,
                                  SVN_CONFIG__SUBDIRECTORY, fname, NULL);
  }

#else  /* ! SVN_WIN32 */
  {
    apr_status_t apr_err;
    apr_uid_t uid;
    apr_gid_t gid;
    char *username;
    char *homedir;

    apr_err = apr_current_userid (&uid, &gid, pool);
    if (apr_err)
      return SVN_NO_ERROR;
    
    apr_err = apr_get_username (&username, uid, pool);
    if (apr_err)
      return SVN_NO_ERROR;
    
    apr_err = apr_get_home_directory (&homedir, username, pool);
    if (apr_err)
      return SVN_NO_ERROR;
    
    *path_p = svn_path_join_many (pool,
                                  svn_path_canonicalize (homedir, pool),
                                  SVN_CONFIG__USR_DIRECTORY, fname, NULL);
    
  }
#endif /* SVN_WIN32 */

  return SVN_NO_ERROR;
}



/*** Exported interfaces. ***/

svn_error_t *
svn_config__parse_file (svn_config_t *cfg, const char *file,
                        svn_boolean_t must_exist)
{
  svn_error_t *err = SVN_NO_ERROR;
  parse_context_t ctx;
  int ch, count;
  /* "Why," you ask yourself, "is he using stdio FILE's instead of
     apr_file_t's?"  The answer is simple: newline translation.  For
     all that it has an APR_BINARY flag, APR doesn't do newline
     translation in files.  The only portable way I know to get
     translated text files is to use the standard stdio library. */

  FILE *fd = fopen (file, "rt");
  if (fd == NULL)
    {
      if (errno != ENOENT)
        return svn_error_createf (SVN_ERR_BAD_FILENAME, NULL,
                                  "Can't open config file \"%s\"", file);
      else if (must_exist && errno == ENOENT)
        return svn_error_createf (SVN_ERR_BAD_FILENAME, NULL,
                                  "Can't find config file \"%s\"", file);
      else
        return SVN_NO_ERROR;
    }

  ctx.cfg = cfg;
  ctx.file = file;
  ctx.fd = fd;
  ctx.line = 1;
  ctx.pool = svn_pool_create (cfg->pool);
  ctx.section = svn_stringbuf_create("", ctx.pool);
  ctx.option = svn_stringbuf_create("", ctx.pool);
  ctx.value = svn_stringbuf_create("", ctx.pool);

  do
    {
      ch = skip_whitespace (fd, &count);
      switch (ch)
        {
        case '[':               /* Start of section header */
          if (count == 0)
            err = parse_section_name (&ch, &ctx);
          else
            {
              ch = EOF;
              err = svn_error_createf (SVN_ERR_MALFORMED_FILE, NULL,
                                       "%s:%d: Section header"
                                       " must start in the first column",
                                       file, ctx.line);
            }
          break;

        case '#':               /* Comment */
          if (count == 0)
            {
              ch = skip_to_eoln(fd);
              ++ctx.line;
            }
          else
            {
              ch = EOF;
              err = svn_error_createf (SVN_ERR_MALFORMED_FILE, NULL,
                                       "%s:%d: Comment"
                                       " must start in the first column",
                                       file, ctx.line);
            }
          break;

        case '\n':              /* Empty line */
          ++ctx.line;
          break;

        case EOF:               /* End of file or read error */
          break;

        default:
          if (svn_stringbuf_isempty (ctx.section))
            {
              ch = EOF;
              err = svn_error_createf (SVN_ERR_MALFORMED_FILE, NULL,
                                       "%s:%d: Section header expected",
                                       file, ctx.line);
            }
          else if (count != 0)
            {
              ch = EOF;
              err = svn_error_createf (SVN_ERR_MALFORMED_FILE, NULL,
                                       "%s:%d: Option expected",
                                       file, ctx.line);
            }
          else
            err = parse_option (&ch, &ctx);
          break;
        }
    }
  while (ch != EOF);

  if (ferror (fd))
    {
      err = svn_error_createf (-1, /* FIXME: Wrong error code. */
                               NULL,
                               "%s:%d: Read error", file, ctx.line);
    }

  svn_pool_destroy (ctx.pool);
  fclose (fd);
  return err;
}


/* Helper for svn_config_ensure:  see if ~/.subversion/auth/ and its
   subdirs exist, try to create them, but don't throw errors on
   failure.  PATH is assumed to be a path to the user's private config
   directory. */
static void
ensure_auth_dirs (const char *path,
                  apr_pool_t *pool)
{
  svn_node_kind_t kind;
  apr_status_t apr_err;
  const char *auth_dir, *auth_subdir;

  /* Ensure ~/.subversion/auth/ */
  auth_dir = svn_path_join_many (pool, path, SVN_CONFIG__AUTH_SUBDIR, NULL);
  svn_io_check_path (auth_dir, &kind, pool);
  if (kind == svn_node_none)
    {
      /* 'chmod 700' permissions: */
      apr_err = apr_dir_make (auth_dir,
                              (APR_UREAD | APR_UWRITE | APR_UEXECUTE),
                              pool);
      if (apr_err)
        return;
    }

  /* If a provider exists that wants to store credentials in
     ~/.subversion, a subdirectory for the cred_kind must exist. */

  auth_subdir = svn_path_join_many (pool, auth_dir,
                                    SVN_AUTH_CRED_SIMPLE, NULL);
  svn_io_check_path (auth_subdir, &kind, pool);
  if (kind == svn_node_none)
    apr_err = apr_dir_make (auth_subdir, APR_OS_DEFAULT, pool);

  auth_subdir = svn_path_join_many (pool, auth_dir,
                                    SVN_AUTH_CRED_USERNAME, NULL);
  svn_io_check_path (auth_subdir, &kind, pool);
  if (kind == svn_node_none)
    apr_err = apr_dir_make (auth_subdir, APR_OS_DEFAULT, pool);

}


svn_error_t *
svn_config_ensure (apr_pool_t *pool)
{
  const char *path;
  svn_node_kind_t kind;
  apr_status_t apr_err;
  svn_error_t *err;

  /* Ensure that the user-specific config directory exists.  */
  SVN_ERR (svn_config__user_config_path (&path, NULL, pool));

  if (! path)
    return SVN_NO_ERROR;

  SVN_ERR (svn_io_check_path (path, &kind, pool));
  if (kind == svn_node_none)
    {
      apr_err = apr_dir_make (path, APR_OS_DEFAULT, pool);
      if (apr_err)
        return SVN_NO_ERROR;
    }
  else
    {
      /* ### config directory already exists, but for the sake of
         smooth upgrades, try to ensure that the auth/ subdirs exist
         as well.  we can remove this check someday in the future. */
      ensure_auth_dirs (path, pool);

      return SVN_NO_ERROR;
    }

  /* Else, there's a configuration directory. */

  /* If we get errors trying to do things below, just stop and return
     success.  There's no _need_ to init a config directory if
     something's preventing it. */

  /** If non-existent, try to create a number of auth/ subdirectories. */
  ensure_auth_dirs (path, pool);

  /** Ensure that the `README' file exists. **/
  SVN_ERR (svn_config__user_config_path
           (&path, SVN_CONFIG__USR_README_FILE, pool));

  if (! path)  /* highly unlikely, since a previous call succeeded */
    return SVN_NO_ERROR;

  err = svn_io_check_path (path, &kind, pool);
  if (err)
    return SVN_NO_ERROR;

  if (kind == svn_node_none)
    {
      apr_file_t *f;
      const char *contents =
   "This directory holds run-time configuration information for Subversion\n"
   "clients.  The configuration files all share the same syntax, but you\n"
   "should examine a particular file to learn what configuration\n"
   "directives are valid for that file.\n"
   "\n"
   "The syntax is standard INI format:"
   "\n"
   "\n"
   "   - Empty lines, and lines starting with '#', are ignored.\n"
   "     The first significant line in a file must be a section header.\n"
   "\n"
   "   - A section starts with a section header, which must start in\n"
   "     the first column:\n"
   "\n"
   "       [section-name]\n"
   "\n"
   "   - An option, which must always appear within a section, is a pair\n"
   "     (name, value).  There are two valid forms for defining an\n"
   "     option, both of which must start in the first column:\n"
   "\n"
   "       name: value\n"
   "       name = value\n"
   "\n"
   "     Whitespace around the separator (:, =) is optional.\n"
   "\n"
   "   - Section and option names are case-insensitive, but case is\n"
   "     preserved.\n"
   "\n"
   "   - An option's value may be broken into several lines.  The value\n"
   "     continuation lines must start with at least one whitespace.\n"
   "     Trailing whitespace in the previous line, the newline character\n"
   "     and the leading whitespace in the continuation line is compressed\n"
   "     into a single space character.\n"
   "\n"
   "   - All leading and trailing whitespace around a value is trimmed,\n"
   "     but the whitespace within a value is preserved, with the\n"
   "     exception of whitespace around line continuations, as\n"
   "     described above.\n"
   "\n"
   "   - When a value is a list, it is comma-separated.  Again, the\n"
   "     whitespace around each element of the list is trimmed.\n"
   "\n"
#if 0   /* expansion not implemented yet */
   "   - Option values may be expanded within a value by enclosing the\n"
   "     option name in parentheses, preceded by a percent sign:\n"
   "\n"
   "       %(name)\n"
   "\n"
   "     The expansion is performed recursively and on demand, during\n"
   "     svn_option_get.  The name is first searched for in the same\n"
   "     section, then in the special [DEFAULT] section. If the name\n"
   "     is not found, the whole %(name) placeholder is left\n"
   "     unchanged.\n"
   "\n"
   "     Any modifications to the configuration data invalidate all\n"
   "     previously expanded values, so that the next svn_option_get\n"
   "     will take the modifications into account.\n"
   "\n"
#endif /* 0 */
   "\n"
   "Configuration data in the Windows registry\n"
   "==========================================\n"
   "\n"
   "On Windows, configuration data may also be stored in the registry.  The\n"
   "functions svn_config_read and svn_config_merge will read from the\n"
   "registry when passed file names of the form:\n"
   "\n"
   "   REGISTRY:<hive>/path/to/config-key\n"
   "\n"
   "The REGISTRY: prefix must be in upper case. The <hive> part must be\n"
   "one of:\n"
   "\n"
   "   HKLM for HKEY_LOCAL_MACHINE\n"
   "   HKCU for HKEY_CURRENT_USER\n"
#if 0   /* expansion not implemented yet */
   "\n"
   "The values in config-key represent the options in the [DEFAULT] section."
   "\n"
   "The keys below config-key represent other sections, and their values\n"
#else
   "\n"
   "The keys below config-key represent the sections, and their values\n"
#endif /* 0 */
   "represent the options. Only values of type REG_SZ whose name doesn't\n"
   "start with a '#' will be used; other values, as well as the keys'\n"
   "default values, will be ignored.\n"
   "\n"
   "\n"
   "File locations\n"
   "==============\n"
   "\n"
   "Typically, Subversion uses two config directories, one for site-wide\n"
   "configuration,\n"
   "\n"
   "  /etc/subversion/servers\n"
   "  /etc/subversion/config\n"
   "  /etc/subversion/hairstyles\n"
   "     -- or --\n"
   "  REGISTRY:HKLM\\Software\\Tigris.org\\Subversion\\Servers\n"
   "  REGISTRY:HKLM\\Software\\Tigris.org\\Subversion\\Config\n"
   "  REGISTRY:HKLM\\Software\\Tigris.org\\Subversion\\Hairstyles\n"
   "\n"
   "and one for per-user configuration:\n"
   "\n"
   "  ~/.subversion/servers\n"
   "  ~/.subversion/config\n"
   "  ~/.subversion/hairstyles\n"
   "     -- or --\n"
   "  REGISTRY:HKCU\\Software\\Tigris.org\\Subversion\\Servers\n"
   "  REGISTRY:HKCU\\Software\\Tigris.org\\Subversion\\Config\n"
   "  REGISTRY:HKCU\\Software\\Tigris.org\\Subversion\\Hairstyles\n"
   "\n";

      apr_err = apr_file_open (&f, path,
                               (APR_WRITE | APR_CREATE | APR_EXCL),
                               APR_OS_DEFAULT,
                               pool);

      if (! apr_err)
        {
          apr_err = apr_file_write_full (f, contents, strlen (contents), NULL);
          if (apr_err)
            return svn_error_createf (apr_err, NULL, 
                                      "writing config file `%s'", path);
          
          apr_err = apr_file_close (f);
          if (apr_err)
            return svn_error_createf (apr_err, NULL, 
                                      "closing config file `%s'", path);
        }
    }

  /** Ensure that the `servers' file exists. **/
  SVN_ERR (svn_config__user_config_path
           (&path, SVN_CONFIG_CATEGORY_SERVERS, pool));

  if (! path)  /* highly unlikely, since a previous call succeeded */
    return SVN_NO_ERROR;

  err = svn_io_check_path (path, &kind, pool);
  if (err)
    return SVN_NO_ERROR;
  
  if (kind == svn_node_none)
    {
      apr_file_t *f;
      const char *contents =
        "### This file specifies server-specific protocol parameters,\n"
        "### including HTTP proxy information, HTTP timeout settings, and\n"
        "### svn protocol tunnel agents (for running svn over ssh or similar\n"
        "### tools).\n"
        "###\n"
        "### The currently defined server options are:\n"
        "###   http-proxy-host          Proxy host for HTTP connection\n"
        "###   http-proxy-port          Port number of proxy host service\n"
        "###   http-proxy-username      Username for auth to proxy service\n"
        "###   http-proxy-password      Password for auth to proxy service\n"
        "###   http-timeout             Timeout for HTTP requests in seconds\n"
        "###   http-compression         Whether to compress HTTP requests\n"
        "###   neon-debug-mask          Debug mask for Neon HTTP library\n"
        "###   svn-tunnel-agent         Program to connect to svn server\n"
        "###   ssl-ignore-unknown-ca    Allow untrusted server certificates\n"
        "###   ssl-ignore-invalid-date  Allow expired/postdated certificates\n"
        "###   ssl-ignore-host-mismatch Allow certificates for other servers\n"
        "###   ssl-client-cert-file     Client certificate file location\n"
        "###   ssl-client-key-file      Client Key location, if not in cert\n"
        "###   ssl-client-key-type      Can be either 'pem' or 'pkcs12'\n"
        "###   ssl-client-cert-password Client Key password, if needed.\n"
        "###\n"
        "### HTTP timeouts, if given, are specified in seconds.  A timeout\n"
        "### of 0, i.e. zero, causes a builtin default to be used.\n"
        "###\n"
        "### The commented-out examples below are intended only to\n"
        "### demonstrate how to use this file; any resemblance to actual\n"
        "### servers, living or dead, is entirely coincidental.\n"
        "\n"
        "### In this section, the URL of the repository you're trying to\n"
        "### access is matched against the patterns on the right.  If a\n"
        "### match is found, the server info is from the section with the\n"
        "### corresponding name.\n"
        "\n"
        "### Note that the ssl overrides significantly decrease the security\n"
        "### of the connection, and may allow a third party to intercept or\n"
        "### even modify the transmitted data\n"
        "\n"
        "# [groups]\n"
        "# group1 = *.collab.net\n"
        "# othergroup = repository.blarggitywhoomph.com\n"
        "# thirdgroup = *.example.com\n"
        "\n"
        "### Information for the first group:\n"
        "# [group1]\n"
        "# http-proxy-host = proxy1.some-domain-name.com\n"
        "# http-proxy-port = 80\n"
        "# http-proxy-username = blah\n"
        "# http-proxy-password = doubleblah\n"
        "# http-timeout = 60\n"
        "# neon-debug-mask = 130\n"
        "# ssl-ignore-unknown-ca = true\n"
        "# ssl-ignore-host-mismatch = true\n"
        "# ssl-ignore-invalid-date = true\n"
        "\n"
        "### Information for the second group:\n"
        "# [othergroup]\n"
        "# http-proxy-host = proxy2.some-domain-name.com\n"
        "# http-proxy-port = 9000\n"
        "# No username and password, so use the defaults below.\n"
        "\n"
        "### Information for the third group:\n"
        "# [thirdgroup]\n"
        "# svn-tunnel-agent = ssh\n"
        "\n"
        "### You can set default parameters in the 'global' section.\n"
        "### These parameters apply if no corresponding parameter is set in\n"
        "### a specifically matched group as shown above.  Thus, if you go\n"
        "### through the same proxy server to reach every site on the\n"
        "### Internet, you probably just want to put that server's\n"
        "### information in the 'global' section and not bother with\n"
        "### 'groups' or any other sections.\n"
        "###\n"
        "### If you go through a proxy for all but a few sites, you can\n"
        "### list those exceptions under 'http-proxy-exceptions'.  This only\n"
        "### overrides defaults, not explicitly matched server names.\n"
        "###\n"
        "### 'ssl-authorities-file' points to the location of the file\n"
        "### containing a list of known and trusted SSL Certificate \n"
        "### Authorities (CAs). See details above for overriding security\n"
        "### issues due to SSL\n"
        "# [global]\n"
        "# http-proxy-exceptions = *.exception.com, www.internal-site.org\n"
        "# http-proxy-host = defaultproxy.whatever.com\n"
        "# http-proxy-port = 7000\n"
        "# http-proxy-username = defaultusername\n"
        "# http-proxy-password = defaultpassword\n"
        "# http-compression = yes\n"
        "# No http-timeout, so just use the builtin default.\n"
        "# No neon-debug-mask, so neon debugging is disabled.\n"
        "# ssl-authorities-file = /path/to/CAcerts.pem\n";

      apr_err = apr_file_open (&f, path,
                               (APR_WRITE | APR_CREATE | APR_EXCL),
                               APR_OS_DEFAULT,
                               pool);

      if (! apr_err)
        {
          apr_err = apr_file_write_full (f, contents, strlen (contents), NULL);
          if (apr_err)
            return svn_error_createf (apr_err, NULL, 
                                      "writing config file `%s'", path);
          
          apr_err = apr_file_close (f);
          if (apr_err)
            return svn_error_createf (apr_err, NULL, 
                                      "closing config file `%s'", path);
        }
    }

  /** Ensure that the `config' file exists. **/
  SVN_ERR (svn_config__user_config_path
           (&path, SVN_CONFIG_CATEGORY_CONFIG, pool));

  if (! path)  /* highly unlikely, since a previous call succeeded */
    return SVN_NO_ERROR;

  err = svn_io_check_path (path, &kind, pool);
  if (err)
    return SVN_NO_ERROR;
  
  if (kind == svn_node_none)
    {
      apr_file_t *f;
      const char *contents =
        "### This file configures various client-side behaviors.\n"
        "###\n"
        "### The commented-out examples below are intended to demonstrate\n"
        "### how to use this file.\n"
        "\n"
        "### Section for authentication and authorization customizations.\n"
        "### Set store-password to 'no' to avoid storing your subversion\n"
        "###   password in your working copies.  It defaults to 'yes'.\n"   
        "# [auth]\n"
        "# store-password = no\n"
        "\n"
        "### Section for configuring external helper applications.\n"
        "### Set editor to the command used to invoke your text editor.\n"
        "###   This will override the environment variables that Subversion\n"
        "###   examines by default to find this information ($EDITOR, \n"
        "###   et al).\n"
        "### Set diff-cmd to the absolute path of your `diff' program.\n"
        "###   This will override the compile-time default path to `diff'\n"
        "###   that Subversion default to.\n"
        "### Set diff3-cmd to the absolute path of your `diff3' program.\n"
        "###   This will override the compile-time default path to `diff3'\n"
        "###   that Subversion default to.\n"
        "### Set diff3-has-program-arg to `true' or `yes' if your `diff3'\n"
        "###   program accepts the `--diff-program' option.\n"
        "# [helpers]\n"
        "# editor-cmd = editor (vi, emacs, notepad, etc.)\n"
        "# diff-cmd = " SVN_CLIENT_DIFF "\n"
        "# diff3-cmd = " SVN_CLIENT_DIFF3 "\n"
#ifdef SVN_DIFF3_HAS_DIFF_PROGRAM_ARG
        "# diff3-has-program-arg = true\n"
#else
        "# diff3-has-program-arg = false\n"
#endif
        "\n"
        "### Section for configuring miscelleneous Subversion options.\n"
        "# [miscellany]\n"
        "### Set global-ignores to a set of whitespace-delimited globs\n"
        "###   which Subversion will ignore in its `status' output.\n"
        "# global-ignores = *.o *.lo *.la #*# .*.rej *.rej .*~ *~ .#*\n"
        "### Set log-encoding to the default encoding for log messages\n"
        "# log-encoding = latin1\n"
        "\n"
        "### See http://subversion.tigris.org/issues/show_bug.cgi?id=668\n"
        "### for what else will soon be customized in this file.\n";
        
      apr_err = apr_file_open (&f, path,
                               (APR_WRITE | APR_CREATE | APR_EXCL),
                               APR_OS_DEFAULT,
                               pool);

      if (! apr_err)
        {
          apr_err = apr_file_write_full (f, contents, strlen (contents), NULL);
          if (apr_err)
            return svn_error_createf (apr_err, NULL, 
                                      "writing config file `%s'", path);
          
          apr_err = apr_file_close (f);
          if (apr_err)
            return svn_error_createf (apr_err, NULL, 
                                      "closing config file `%s'", path);
        }
    }

  return SVN_NO_ERROR;
}
