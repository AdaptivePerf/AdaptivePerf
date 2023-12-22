#!/bin/bash
function convert_from_ns_to_us() {
    while read -ra arr; do
        if [[ ${arr[-1]} == *\# ]]; then
            arr[-1]=${arr[-1]:0:-1}
            overall_offcpu=true
        else
            overall_offcpu=false
        fi

        new_val=$(perl <<< "print ${arr[-1]}/1000")

        if [[ $overall_offcpu == true ]]; then
            new_val+=\#
        fi

        arr[-1]=$new_val

        echo ${arr[@]}
    done
}
