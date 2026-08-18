#!/usr/bin/env bash

source $CEPH_ROOT/qa/standalone/ceph-helpers.sh

function run() {
    local dir=$1
    shift

    export CEPH_MON="127.0.0.1:7148" # git grep '\<7148\>' : there must be only one
    export CEPH_ARGS
    CEPH_ARGS+="--fsid=$(uuidgen) --auth-supported=none "
    CEPH_ARGS+="--mon-host=$CEPH_MON "
    CEPH_ARGS+="--mon-pg-warn-min-per-osd=0 "

    local funcs=${@:-$(set | sed -n -e 's/^\(TEST_[0-9a-z_]*\) .*/\1/p')}
    for func in $funcs ; do
        setup $dir || return 1
        $func $dir || return 1
        teardown $dir || return 1
    done
}

function TEST_mon_data_size_warn_adaptive() {
    local dir=$1

    run_mon $dir a || return 1
    run_mgr $dir x || return 1
    run_osd $dir 0 || return 1
    run_osd $dir 1 || return 1
    run_osd $dir 2 || return 1

    ceph osd pool create foo 8 || return 1
    ceph osd pool application enable foo rbd --yes-i-really-mean-it || return 1
    wait_for_clean || return 1

    # a threshold of one byte fires on any store, as long as the scaling terms
    # are off.  the detail line must keep the format it had before the
    # threshold became adaptive.
    ceph config set mon mon_data_size_warn_per_osd 0 || return 1
    ceph config set mon mon_data_size_warn_per_pg 0 || return 1
    ceph config set mon mon_data_size_warn 1 || return 1
    wait_for_health "MON_DISK_BIG" || return 1
    ceph health detail | grep "MON_DISK_BIG" || return 1
    ceph health detail | grep -E "mon\.a is .* >= mon_data_size_warn \([0-9]" || return 1
    ! ceph health detail | grep -q "adapted for" || return 1

    # 1 GiB per OSD over three OSDs puts the threshold out of reach
    ceph config set mon mon_data_size_warn_per_osd 1G || return 1
    wait_for_health_gone "MON_DISK_BIG" || return 1

    # ... and so does the per-PG term on its own
    ceph config set mon mon_data_size_warn_per_osd 0 || return 1
    ceph config set mon mon_data_size_warn_per_pg 1G || return 1
    wait_for_health_gone "MON_DISK_BIG" || return 1

    # with the threshold scaled up, the detail line explains why
    ceph config set mon mon_data_size_warn_per_pg 1 || return 1
    wait_for_health "MON_DISK_BIG" || return 1
    ceph health detail | grep "adapted for .* OSDs and .* PGs" || return 1

    # a large base is honoured even with the scaling terms in play
    ceph config set mon mon_data_size_warn 1T || return 1
    wait_for_health_gone "MON_DISK_BIG" || return 1

    # mon_data_size_warn_max_fs_ratio is not exercised here: whether the cap
    # binds depends on the size of the filesystem the test happens to run on.
    # unittest_mon_health covers it against fixed inputs.

    # the options are settable at runtime.  keep injectargs last, since an
    # injected value outranks anything set afterwards with 'ceph config set'.
    ceph config set mon mon_data_size_warn 1 || return 1
    ceph config set mon mon_data_size_warn_per_pg 0 || return 1
    wait_for_health "MON_DISK_BIG" || return 1
    ceph tell mon.a injectargs '--mon_data_size_warn_per_osd 1G' || return 1
    wait_for_health_gone "MON_DISK_BIG" || return 1
}

main mon-data-size-warn "$@"

# Local Variables:
# compile-command: "cd ../.. ; make -j4 && \
#   test/mon/mon-data-size-warn.sh"
# End:
