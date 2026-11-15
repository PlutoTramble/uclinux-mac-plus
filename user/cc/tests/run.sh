#!/bin/sh
# On-Mac test of the C toolchain: compiles each test with cc, runs it and
# compares the output with the .expected file.
#   sh /usr/lib/gcc/tests/run.sh
# Prints "ok name" or "FAIL name (stage, reason)" per test. Plain sh only,
# it has to work with msh.

D=/usr/lib/gcc/tests
T=/var/tmp
failed=0

for n in hello float int64 libc multi
do
    case $n in
    libc)  srcs="$D/libc.c"; opts="-lm" ;;
    multi) srcs="$D/multi_a.c $D/multi_b.c"; opts="" ;;
    *)     srcs="$D/$n.c"; opts="" ;;
    esac
    rm -f $T/t_$n $T/t_$n.out
    cc $srcs -o $T/t_$n $opts
    rc=$?
    if [ $rc -ne 0 ]
    then
        echo "FAIL $n (cc, exit $rc)"
        failed=1
    else
        $T/t_$n > $T/t_$n.out
        rc=$?
        if [ $rc -ne 0 ]
        then
            echo "FAIL $n (run, exit $rc)"
            failed=1
        elif cmp -s $T/t_$n.out $D/$n.expected
        then
            echo "ok $n"
        else
            echo "FAIL $n (output differs, see $T/t_$n.out)"
            failed=1
        fi
    fi
done

if [ $failed -eq 0 ]
then
    echo "ALL OK"
else
    echo "SOME TESTS FAILED"
fi
exit $failed
