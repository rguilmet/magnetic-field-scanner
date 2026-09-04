#!/bin/bash
git filter-branch -f --env-filter '
CORRECT_NAME="rguilmet"
CORRECT_EMAIL="rguilmet@users.noreply.github.com"

export GIT_COMMITTER_NAME="$CORRECT_NAME"
export GIT_COMMITTER_EMAIL="$CORRECT_EMAIL"
export GIT_AUTHOR_NAME="$CORRECT_NAME"
export GIT_AUTHOR_EMAIL="$CORRECT_EMAIL"
' --tag-name-filter cat -- --branches --tags
