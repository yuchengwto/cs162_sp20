#!/usr/bin/env bash
set -euo pipefail
if git diff --cached --name-only --diff-filter=CMRA | xargs grep -q 'clang-format off'
then
	echo 'Error: You are not permitted to disable clang-format' 1>&2
	exit 1
fi
( cd "$(git rev-parse --show-toplevel)/pintos/src" && make format )
git diff --cached --name-only --diff-filter=CMRA | xargs git add
