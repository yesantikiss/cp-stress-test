#!/usr/bin/env bash
# gen.hpp / adaptive.hpp の単体テストと、わざとバグらせた解でのドライバの動作確認。
# 何を確かめているかは docs/testing.md。
set -uo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${TMPDIR:-/tmp}/cp-stress-test-tests"
mkdir -p "$build_dir"
fail=0
check() {  # check <name> <expected exit> <command...>
    local name="$1" want="$2"; shift 2
    "$@" >"$build_dir/$name.log" 2>&1
    local got=$?
    if [ "$got" -eq "$want" ]; then echo "ok   $name"; else echo "FAIL $name (exit $got, want $want)"; tail -20 "$build_dir/$name.log"; fail=1; fi
}
kind_is() {  # kind_is <check の名前> <判定>: その check が保存した失敗（ログの saved: の行）の meta.json の kind
    local dir got=""
    dir="$(sed -n 's/^saved: //p' "$build_dir/$1.log" | tail -1)"
    [ -n "$dir" ] && got="$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]+"/meta.json"))["kind"])' "$dir" 2>/dev/null)"
    if [ "$got" = "$2" ]; then echo "ok   $1_kind"; else echo "FAIL $1_kind (kind '$got', want '$2')"; fail=1; fi
}
log_has() {  # log_has <名前> <check の名前> <grep -E のパターン>
    if grep -qE "$3" "$build_dir/$2.log"; then echo "ok   $1"; else echo "FAIL $1 (no /$3/ in $2.log)"; tail -20 "$build_dir/$2.log"; fail=1; fi
}

# C++ の単体テスト。テストごとの ok / FAIL をそのまま出す（行頭に "gen_test: " を付ける）
for t in gen_test adaptive_test; do
    if g++ -std=gnu++20 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$root" \
        "$root/tests/$t.cpp" -o "$build_dir/$t" 2>"$build_dir/$t.build.log"; then
        "$build_dir/$t" | sed "s/^/$t: /"
        [ "${PIPESTATUS[0]}" -eq 0 ] || fail=1
    else
        echo "FAIL $t (build)"; tail -20 "$build_dir/$t.build.log"; fail=1
    fi
done

work="$build_dir/sample"
rm -rf "$work" && cp -r "$root/tests/sample" "$work" && cd "$work"
st() { python3 "$root/stress.py" "$@"; }
check ok          0 st --main main_ok.cpp -n 300 --seed 1
check wa          1 st --main main_wa.cpp --seed 1
kind_is wa WA
check re          1 st --main main_re.cpp --seed 2
kind_is re RE
check tle         1 st --main main_tle.cpp --seed 3 --tl 1
kind_is tle TLE
check checker_ok  0 st -c checker.cpp --main main_ok.cpp -n 300 --seed 5
check checker_wa  1 st -c checker.cpp --main main_wa.cpp --seed 5
kind_is checker_wa WA
check checker_ref 0 st -c checker.cpp --ref --main main_ok.cpp -n 100 --seed 5
check checker_default_ok 0 st -c --main main_ok.cpp -n 100 --seed 5
check checker_default_wa 1 st --main main_wa.cpp --seed 5 -c
check checker_default_ref 0 st -c --ref --main main_ok.cpp -n 100 --seed 5

# 愚直解の失敗、道具の側の失敗（終了コード 2 で、保存しない）
check ref_re      1 st --main main_ok.cpp --guchoku guchoku_re.cpp --seed 1
kind_is ref_re REF_RE
check ref_tle     1 st --main main_ok.cpp --guchoku guchoku_tle.cpp --ref-tl 0.5 --seed 1
kind_is ref_tle REF_TLE
check gen_fail    2 st --main main_ok.cpp --gen-file gen_bad.cpp --seed 1
log_has gen_fail_msg gen_fail '^GEN_FAIL .*distinct'
# checker が例外で落ちたら main の WA ではなく CHECKER_FAIL
check checker_crash 2 st -c checker_crash.cpp --main main_ok.cpp --seed 1
log_has checker_crash_msg checker_crash '^CHECKER_FAIL .*signal SIGABRT'

# 比較方法。main_ok は行末に空白を出すので exact では違い、main_float は 1e-7 ずれた小数を出す
check cmp_exact   1 st --main main_ok.cpp --cmp exact --seed 1
kind_is cmp_exact WA
check cmp_float_ok 0 st --main main_float.cpp --cmp float:1e-6 -n 200 --seed 1
check cmp_float_wa 1 st --main main_float.cpp --cmp float:1e-12 --seed 1
kind_is cmp_float_wa WA
check cmp_token_float 1 st --main main_float.cpp --seed 1

# 表示された st repro をそのまま打てば再現する（--main / --cmp / --tl も付く）
st --main main_ok.cpp --cmp exact --tl 1.5 --seed 1 >/dev/null 2>&1
repro_cmd="$(python3 -c 'import json;print(json.load(open(".stress/last/meta.json"))["repro"])')"
case "$repro_cmd" in
    *"--main main_ok.cpp"*"--cmp exact"*"--tl 1.5"*) echo "ok   repro_cmd_flags" ;;
    *) echo "FAIL repro_cmd_flags ($repro_cmd)"; fail=1 ;;
esac
check repro_cmd_reproduces 1 st ${repro_cmd#st }

# repro: 直前の失敗の seed:cap で同じ入力が出る
st --main main_wa.cpp --seed 7 >/dev/null 2>&1
case_id="$(python3 -c 'import json;m=json.load(open(".stress/last/meta.json"));print(str(m["seed"])+":"+str(m["cap"]))')"
cp .stress/last/in.txt "$build_dir/expected_in.txt"
check repro 1 st repro "$case_id" --main main_wa.cpp
# -c の直後にケース ID を書いても、ケース ID として扱う
check repro_c_swap 1 st repro --main main_wa.cpp -c "$case_id"
if cmp -s .stress/last/in.txt "$build_dir/expected_in.txt"; then echo "ok   repro_same_input"; else echo "FAIL repro_same_input"; fail=1; fi
check last_to_in 0 st last --to-in
if cmp -s in.txt "$build_dir/expected_in.txt"; then echo "ok   to_in_copied"; else echo "FAIL to_in_copied"; fail=1; fi

# 失敗すると in.txt に自動で書き出す。手書きの in.txt だけ退避し、失敗が続いても残る
printf 'hand written\n' > in.txt
st --main main_wa.cpp --seed 11 >/dev/null 2>&1
cmp -s in.txt .stress/last/in.txt && echo "ok   auto_to_in" || { echo "FAIL auto_to_in"; fail=1; }
st --main main_wa.cpp --seed 12 >/dev/null 2>&1
cmp -s in.txt .stress/last/in.txt && [ "$(cat .stress/in.txt.bak)" = "hand written" ] \
    && echo "ok   auto_to_in_keeps_backup" || { echo "FAIL auto_to_in_keeps_backup"; fail=1; }
cp .stress/last/in.txt "$build_dir/before_no_to_in.txt"
st --main main_wa.cpp --seed 13 --no-to-in >/dev/null 2>&1
cmp -s in.txt "$build_dir/before_no_to_in.txt" && echo "ok   no_to_in" || { echo "FAIL no_to_in"; fail=1; }
check restore 0 st restore
[ "$(cat in.txt)" = "hand written" ] && echo "ok   restore_hand_written" || { echo "FAIL restore_hand_written"; fail=1; }
printf 'edited again\n' > in.txt
check restore_refuse 2 st restore
check restore_force  0 st restore --force

# 縮小中に gen error が出ても、そのケースは飛ばして本物の WA を残し、警告を出す
work_s="$build_dir/shrink"
rm -rf "$work_s" && cp -r "$root/tests/shrink" "$work_s" && cd "$work_s"
check shrink_skips_gen_fail 1 st --seed 1
kind_is shrink_skips_gen_fail WA
log_has shrink_gen_fail_warning shrink_skips_gen_fail '^warning: .*GEN_FAIL|^  GEN_FAIL'
[ "$(head -1 in.txt)" -ge 6 ] 2>/dev/null && echo "ok   shrink_in_txt_is_real_failure" || { echo "FAIL shrink_in_txt_is_real_failure"; fail=1; }

# 縮小中の Ctrl+C: 縮小をやめて、見つけた失敗を保存する
check shrink_interrupt 1 python3 "$root/tests/limits/interrupt.py" "shrinking" "$root/stress.py" --seed 1
kind_is shrink_interrupt WA
log_has shrink_interrupt_msg shrink_interrupt '^shrink interrupted'
[ "$(head -1 in.txt)" -ge 6 ] 2>/dev/null && echo "ok   shrink_interrupt_in_txt" || { echo "FAIL shrink_interrupt_in_txt"; fail=1; }

# 子プロセスの上限と後始末
work_l="$build_dir/limits"
rm -rf "$work_l" && cp -r "$root/tests/limits" "$work_l" && cd "$work_l"
# スタックは 8 MB ではなく 1024 MB まで使える（深さ 100 万の再帰が RE にならない）
check limits_deep_stack 0 st --main main_deep.cpp -n 2 --seed 1
check limits_deep_bench 0 st bench deep --main main_deep.cpp -n 1 --seed 1
# 出力が止まらない main は --output-limit で止まって OLE、保存される出力も上限まで
check limits_output 1 st --main main_spam.cpp -n 1 --seed 1 --output-limit 1 --no-to-in
kind_is limits_output OLE
log_has limits_output_msg limits_output '^OLE  main output exceeded 1 MB'
[ "$(stat -c %s .stress/last/out_main.txt)" -le 1048576 ] && echo "ok   limits_output_size" || { echo "FAIL limits_output_size"; fail=1; }
# 愚直解が出し続けたら REF_OLE、bench でも OLE と出る
check limits_ref_output 1 st --main main_deep.cpp --guchoku main_spam.cpp -n 1 --seed 1 --output-limit 1 --no-to-in
kind_is limits_ref_output REF_OLE
check limits_bench_output 1 st bench deep --main main_spam.cpp -n 1 --seed 1 --output-limit 1
log_has limits_bench_output_msg limits_bench_output 'OLE \(> 1 MB\)'
# Ctrl+C で止めたら、走っていた main も止まる
check limits_interrupt 130 python3 "$root/tests/limits/interrupt.py" "mode=" "$root/stress.py" --main main_loop.cpp --tl 60 -n 1 --seed 1
sleep 0.5
if pgrep -f "$work_l/.stress/bin/main-" >/dev/null; then echo "FAIL limits_interrupt_no_leftover"; pkill -9 -f "$work_l/.stress/bin/main-"; fail=1; else echo "ok   limits_interrupt_no_leftover"; fi

# 名前付き生成器、make、bench
work2="$build_dir/multi"
rm -rf "$work2" && cp -r "$root/tests/multi" "$work2" && cd "$work2"
check multi_random_only 0 st --main main_same.cpp -g random -n 300 --seed 1
check multi_all_same    1 st --main main_same.cpp --seed 1
if grep -q '"gen": "all_same"' .stress/last/meta.json; then echo "ok   multi_gen_name_saved"; else echo "FAIL multi_gen_name_saved"; fail=1; fi
grep -q '"same"' .stress/last/meta.json && grep -q '^tags: same$' "$build_dir/multi_all_same.log" \
    && echo "ok   multi_tags" || { echo "FAIL multi_tags"; fail=1; }
check multi_repro       1 st repro "$(python3 -c 'import json;print(json.load(open(".stress/last/meta.json"))["case"])')" --main main_same.cpp
check multi_big         0 st --main main_ok.cpp -g max --seed 1
grep -q 'OK  20 cases' "$build_dir/multi_big.log" && echo "ok   multi_big_default_20" || { echo "FAIL multi_big_default_20"; fail=1; }
check multi_big_mixed   0 st --main main_ok.cpp -g random,max --seed 1 --big-iters 3
grep -q 'OK  1000 cases' "$build_dir/multi_big_mixed.log" && echo "ok   multi_big_mixed_1000" || { echo "FAIL multi_big_mixed_1000"; fail=1; }
check multi_big_n       0 st --main main_ok.cpp -g max --seed 1 -n 25
grep -q 'OK  25 cases' "$build_dir/multi_big_n.log" && echo "ok   multi_big_explicit_n" || { echo "FAIL multi_big_explicit_n"; fail=1; }
check make              0 st make max all_same -n 2 --seed 3 -o cases
if [ "$(ls cases | wc -l)" -eq 4 ]; then echo "ok   make_files"; else echo "FAIL make_files"; fail=1; fi
check bench_ok          0 st bench --main main_ok.cpp -n 1 --seed 3
check bench_tle         1 st bench --main main_slow.cpp -n 1 --seed 3 --tl 0.5

# インタラクティブ（数当て、25 回まで）
work3="$build_dir/interactive"
rm -rf "$work3" && cp -r "$root/tests/interactive" "$work3" && cd "$work3"
check inter_ok      0 st -i interactor.cpp --main main_ok.cpp -n 300 --seed 1
check inter_wa      1 st -i interactor.cpp --main main_wa.cpp --seed 1
check inter_default 0 st -i --main main_ok.cpp -n 100 --seed 1
grep -q '"kind": "WA"' .stress/last/meta.json && grep -q '^> ! ' .stress/last/transcript.txt \
    && echo "ok   inter_transcript" || { echo "FAIL inter_transcript"; fail=1; }
check inter_limit   1 st -i interactor.cpp --main main_many.cpp --seed 1 -n 1000 --no-shrink
grep -q 'query limit exceeded' .stress/last/meta.json && echo "ok   inter_limit_msg" || { echo "FAIL inter_limit_msg"; fail=1; }
check inter_noflush 1 st -i interactor.cpp --main main_noflush.cpp --seed 1 --tl 0.5 --no-shrink
grep -q '"kind": "TLE"' .stress/last/meta.json && echo "ok   inter_noflush_tle" || { echo "FAIL inter_noflush_tle"; fail=1; }
check inter_re      1 st -i interactor.cpp --main main_re.cpp --seed 1
kind_is inter_re RE
# interactor の WA の直後に main が落ちても、終わった順番によらず WA（10 回とも）
badq_ok=1
for k in $(seq 1 10); do
    st repro 5:1:gen -i interactor.cpp --main main_badquery.cpp --no-to-in >"$build_dir/inter_badquery.log" 2>&1
    grep -q '^WA  y is out of range' "$build_dir/inter_badquery.log" || badq_ok=0
done
[ "$badq_ok" -eq 1 ] && echo "ok   inter_wa_then_crash_is_wa" || { echo "FAIL inter_wa_then_crash_is_wa"; tail -20 "$build_dir/inter_badquery.log"; fail=1; }
check inter_extra   1 st -i interactor.cpp --main main_extra.cpp --seed 1
check inter_repro   1 st repro "$(python3 -c 'import json;print(json.load(open(".stress/last/meta.json"))["case"])')" -i interactor.cpp --main main_extra.cpp
check inter_with_c  2 st -i interactor.cpp -c checker.cpp --main main_ok.cpp
check inter_init    0 st init -i --force
check inter_adaptive_ok    0 st -i interactor_adaptive.cpp --main main_ok.cpp -n 300 --seed 1
check inter_adaptive_guess 1 st -i interactor_adaptive.cpp --main main_guess.cpp -n 300 --seed 1

# examples/adaptive の例がそのまま通る
for ex in guess sort tree arc184_a; do
    work4="$build_dir/example_$ex"
    rm -rf "$work4" && cp -r "$root/examples/adaptive/$ex" "$work4" && cd "$work4"
    n=100; [ "$ex" = arc184_a ] && n=10
    check "example_$ex" 0 st -i interactor.cpp -n "$n" --seed 1
done

# examples/checker の例がそのまま通る
for ex in lis subset_sum sqrt; do
    work5="$build_dir/example_checker_$ex"
    rm -rf "$work5" && cp -r "$root/examples/checker/$ex" "$work5" && cd "$work5"
    ref=(); [ "$ex" != sqrt ] && ref=(--ref)
    check "example_checker_$ex" 0 st -c checker.cpp "${ref[@]}" -n 100 --seed 1
done

[ "$fail" -eq 0 ] && echo "All tests passed" || { echo "Some tests failed"; exit 1; }
