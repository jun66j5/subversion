/*
 * ls.c:  list local and remote directory entries.
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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

/* ==================================================================== */



#include "client.h"
#include "svn_client.h"
#include "svn_path.h"

static svn_error_t *
get_dir_contents (apr_hash_t *dirents,
                  const char *dir,
                  svn_revnum_t rev,
                  svn_ra_plugin_t *ra_lib,
                  void *session,
                  svn_boolean_t recurse,
                  apr_pool_t *pool)
{
  apr_hash_t *tmpdirents;
  svn_dirent_t *the_ent;
  apr_hash_index_t *hi;

  /* Get the directory's entries, but not its props. */
  if (ra_lib->get_dir)
    SVN_ERR (ra_lib->get_dir (session, dir, rev, &tmpdirents, NULL, NULL));
  else
    return svn_error_create (SVN_ERR_RA_NOT_IMPLEMENTED, 0, NULL, pool,
                             "No get_dir() available for url schema.");

  for (hi = apr_hash_first (pool, tmpdirents);
       hi;
       hi = apr_hash_next (hi))
    {
      const char *path;
      const void *key;
      void *val;

      apr_hash_this (hi, &key, NULL, &val);

      the_ent = val;

      path = svn_path_join (dir, key, pool);

      apr_hash_set (dirents, path, APR_HASH_KEY_STRING, val);

      if (recurse && the_ent->kind == svn_node_dir)
        SVN_ERR (get_dir_contents (dirents, path, rev, ra_lib, session,
                                   recurse, pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_ls (apr_hash_t **dirents,
               const char *url,
               svn_client_revision_t *revision,
               svn_client_auth_baton_t *auth_baton,
               svn_boolean_t recurse,               
               apr_pool_t *pool)
{
  svn_ra_plugin_t *ra_lib;  
  void *ra_baton, *session;
  svn_revnum_t rev;
  enum svn_node_kind url_kind;

  /* Get the RA library that handles URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, url, pool));

  /* Open a repository session to the URL. */
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, url, NULL, NULL,
                                        NULL, FALSE, FALSE, TRUE, 
                                        auth_baton, pool));

  /* Resolve REVISION into a real revnum. */
  SVN_ERR (svn_client__get_revision_number (&rev, ra_lib, session,
                                            revision, NULL, pool));
  if (! SVN_IS_VALID_REVNUM (rev))
    SVN_ERR (ra_lib->get_latest_revnum (session, &rev));

  /* Decide if the URL is a file or directory. */
  SVN_ERR (ra_lib->check_path (&url_kind, session, "", rev));

  if (url_kind == svn_node_dir)
    {
      *dirents = apr_hash_make (pool);

      SVN_ERR (get_dir_contents (*dirents, "", rev, ra_lib, session, recurse,
                                 pool));

      SVN_ERR (ra_lib->close (session));
    }
  else if (url_kind == svn_node_file)
    {
      apr_hash_t *parent_ents;
      const char *parent_url, *base_name;
      svn_dirent_t *the_ent;

      /* Re-open the session to the file's parent instead. */
      svn_path_split_nts (url, &parent_url, &base_name, pool);
      SVN_ERR (ra_lib->close (session));
      SVN_ERR (svn_client__open_ra_session (&session, ra_lib, parent_url, NULL,
                                            NULL, NULL, FALSE, FALSE, TRUE, 
                                            auth_baton, pool));

      /* Get all parent's entries, no props. */
      if (ra_lib->get_dir)
        SVN_ERR (ra_lib->get_dir (session, "", rev, &parent_ents, NULL, NULL));
      else
        return svn_error_create (SVN_ERR_RA_NOT_IMPLEMENTED, 0, NULL, pool,
                                 "No get_dir() available for url schema.");

      SVN_ERR (ra_lib->close (session));

      /* Copy the relevant entry into the caller's hash. */
      *dirents = apr_hash_make (pool);
      the_ent = apr_hash_get (parent_ents, base_name, APR_HASH_KEY_STRING);
      if (the_ent == NULL)
        return svn_error_create (SVN_ERR_FS_NOT_FOUND, 0, NULL, pool,
                                 "URL non-existent in that revision.");
        
      apr_hash_set (*dirents, base_name, APR_HASH_KEY_STRING, the_ent);
    }
  else
    return svn_error_create (SVN_ERR_FS_NOT_FOUND, 0, NULL, pool,
                             "URL non-existent in that revision.");

  return SVN_NO_ERROR;
}






/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
