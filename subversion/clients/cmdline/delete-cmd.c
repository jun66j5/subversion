/*
 * delete-cmd.c -- Delete/undelete commands
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "cl.h"



/*** Code. ***/

svn_error_t *
svn_cl__may_need_force (svn_error_t *err)
{
  if (err
      && (err->apr_err == SVN_ERR_CLIENT_UNVERSIONED ||
          err->apr_err == SVN_ERR_CLIENT_MODIFIED))
    {
      /* Should this svn_error_compose a new error number? Probably not,
         the error hasn't changed. */
      err = svn_error_quick_wrap (err,
                                  "Use --force to override this restriction" );
    }

  return err;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__delete (apr_getopt_t *os,
                void *baton,
                apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = baton;
  apr_array_header_t *targets;
  svn_client_ctx_t *ctx = svn_client_ctx_create (pool);
  int i;
  svn_client_commit_info_t *commit_info = NULL;
  apr_pool_t *subpool;
  svn_wc_notify_func_t notify_func = NULL;
  void *notify_baton = NULL;

  SVN_ERR (svn_opt_args_to_target_array (&targets, os, 
                                         opt_state->targets,
                                         &(opt_state->start_revision),
                                         &(opt_state->end_revision),
                                         FALSE, pool));

  if (! targets->nelts)
    return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, "");

  if (! opt_state->quiet)
    svn_cl__get_notifier (&notify_func, &notify_baton, FALSE, FALSE, FALSE,
			  pool);

  /* Build an authentication object to give to libsvn_client. */
  svn_client_ctx_set_auth_baton (ctx,
                                 svn_cl__make_auth_baton (opt_state, pool));

  subpool = svn_pool_create (pool);
  for (i = 0; i < targets->nelts; i++)
    {
      svn_error_t *err;
      const char *target = ((const char **) (targets->elts))[i];
      void *log_msg_baton = svn_cl__make_log_msg_baton (opt_state, NULL, 
                                                        subpool);

      commit_info = NULL;
      err = svn_client_delete
        (&commit_info, target, NULL, opt_state->force, 
         &svn_cl__get_log_message,
         log_msg_baton,
         notify_func, notify_baton, ctx, subpool);

      if (err)
        err = svn_cl__may_need_force (err);
      SVN_ERR (svn_cl__cleanup_log_msg (log_msg_baton, err));

      if (commit_info && ! opt_state->quiet)
        svn_cl__print_commit_info (commit_info);
      
      svn_pool_clear (subpool);
    }
  
  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}
