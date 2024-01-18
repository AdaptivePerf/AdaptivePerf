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

function assert_all_files_non_empty() {
    count=$(find $1 -mindepth 1 -maxdepth 1 -name "*$2" -type f ! -size 0 | wc -l)

    assert [ $count -gt 0 ]
    assert_file_count $1 $2 $count
}

function assert_file_non_empty() {
    assert [ -s $1 ]
}

function assert_char_count() {
    assert [ $(cat $1 | wc -m) -eq $2 ]
}

function assert_line_count() {
    assert [ $(cat $1 | wc -l) -eq $2 ]
}
