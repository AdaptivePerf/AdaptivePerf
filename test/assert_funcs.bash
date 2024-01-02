# AdaptivePerf: comprehensive profiling tool based on Linux perf
# Copyright (C) 2023 CERN.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; only version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

function assert_dir_exists() {
    assert [ -d $1 ]
}

function assert_dir_count() {
    assert [ $(find $1 -mindepth 1 -maxdepth 1 -name "*" -type d | wc -l) -eq $2 ]
}

function assert_file_exists() {
    assert [ -f $1 ]
}

function assert_file_count() {
    assert [ $(find $1 -mindepth 1 -maxdepth 1 -name "*$2" -type f | wc -l) -eq $3 ]
}

function assert_file_count_prefix() {
    assert [ $(find $1 -mindepth 1 -maxdepth 1 -name "$2*" -type f | wc -l) -eq $3 ]
}

function assert_all_files_non_empty {
    assert_file_count $1 $2 $(find $1 -mindepth 1 -maxdepth 1 -name "*$2" -type f ! -size 0 | wc -l)
}

function assert_file_non_empty {
    assert [ -s $1 ]
}

function assert_char_count() {
    assert [ $(cat $1 | wc -m) -eq $2 ]
}

function assert_line_count() {
    assert [ $(cat $1 | wc -l) -eq $2 ]
}
