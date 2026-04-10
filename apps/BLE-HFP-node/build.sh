#!/bin/zsh
set -a; source .env; set +a
idf.py "$@"
