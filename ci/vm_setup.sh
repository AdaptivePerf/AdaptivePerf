#!/bin/bash
last_update=$(date --date="$(cat $(portageq get_repo_path / gentoo)/metadata/timestamp.chk)" +%s)
current_time=$(date +%s)

if [[ $((current_time-last_update)) -ge 604800 ]]; then
    >&2 echo "Gentoo is too old, please update it first."
    exit 1
fi
