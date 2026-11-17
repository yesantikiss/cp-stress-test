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
check cmp_float_zero_wa 1 st --main main_float.cpp --cmp float:0 --seed 1
kind_is cmp_float_zero_wa WA
check cmp_float_zero_ok 0 st --main main_ok.cpp --cmp float:0 -n 10 --seed 1
for bad_cmp in foo floating "float:" float:abc float:-1e-6 float:nan float:inf; do
    safe_tag="$(printf '%s' "$bad_cmp" | tr -c 'a-zA-Z0-9_' '_')"
    check "cmp_bad_${safe_tag}" 2 st --main main_ok.cpp --cmp "$bad_cmp"
done
check cxxflags_bad 2 st --main main_ok.cpp --cxxflags='-DFOO="abc'
log_has cxxflags_bad_msg cxxflags_bad 'invalid --cxxflags'
check cxxflags_eq_short 0 st --main main_ok.cpp -n 3 --seed 1 --cxxflags=-g3

# 表示された st repro をそのまま打てば再現する（--main / --cmp / --tl も付く）
st --main main_ok.cpp --cmp exact --tl 1.5 --seed 1 >/dev/null 2>&1
repro_cmd="$(python3 -c 'import json;print(json.load(open(".stress/last/meta.json"))["repro"])')"
case "$repro_cmd" in
    *"--main main_ok.cpp"*"--cmp exact"*"--tl 1.5"*) echo "ok   repro_cmd_flags" ;;
    *) echo "FAIL repro_cmd_flags ($repro_cmd)"; fail=1 ;;
esac
check repro_cmd_reproduces 1 st ${repro_cmd#st }
st --main main_ok.cpp --cmp exact --cxxflags=-O0 --seed 1 --no-to-in >/dev/null 2>&1
repro_cmd_cxx="$(python3 -c 'import json;print(json.load(open(".stress/last/meta.json"))["repro"])')"
case "$repro_cmd_cxx" in
    *"--cxxflags=-O0"*) echo "ok   repro_cmd_cxxflags" ;;
    *) echo "FAIL repro_cmd_cxxflags ($repro_cmd_cxx)"; fail=1 ;;
esac
check repro_cmd_cxxflags_reproduces 1 st ${repro_cmd_cxx#st }

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
check multi_gen_dedup   0 st --main main_ok.cpp -g max -g max --seed 1
grep -q 'OK  20 cases' "$build_dir/multi_gen_dedup.log" && echo "ok   multi_gen_dedup_20" || { echo "FAIL multi_gen_dedup_20"; fail=1; }
check multi_gen_empty   2 st --main main_ok.cpp -g ,
check multi_big_zero    0 st --main main_ok.cpp -g max --big-iters 0 --seed 1
grep -q 'OK  0 cases' "$build_dir/multi_big_zero.log" && echo "ok   multi_big_zero_0" || { echo "FAIL multi_big_zero_0"; fail=1; }
check multi_big_neg     2 st --main main_ok.cpp -g max --big-iters -1
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

# STRESS_BIG の寿命が尽きたときに次の生成器を飛ばさず順番に回す
work_rr="$build_dir/roundrobin"
rm -rf "$work_rr" && mkdir -p "$work_rr" && cd "$work_rr"
cat > gen.cpp << 'EOF'
#include "stress/gen.hpp"
STRESS_GEN(a) { g.out("a"); }
STRESS_BIG(b) { g.out("b"); }
STRESS_GEN(c) { g.out("c"); }
EOF
cat > guchoku.cpp << 'EOF'
#include <iostream>
int main() { std::cout << "ok\n"; }
EOF
cat > main.cpp << 'EOF'
#include <iostream>
#include <string>
int main() { std::string s; std::cin >> s; std::cout << (s == "c" ? "bad\n" : "ok\n"); }
EOF
check rr_no_skip 1 st -g a,b,c --big-iters 1 --seed 1 --no-to-in
kind_is rr_no_skip WA
log_has rr_found_after_3 rr_no_skip '^found after 3 cases'
if [ "$(python3 -c 'import json;print(json.load(open(".stress/last/meta.json"))["gen"])')" = "c" ]; then
    echo "ok   rr_gen_c"
else
    echo "FAIL rr_gen_c"; fail=1
fi

# -j: 並列に回しても、同じ seed なら直列と同じ失敗（縮小のあとも同じ）になる
cd "$work"
same_case() {  # same_case <名前> <st の引数...>: -j 1 と -j 4 で、保存されたケースと何件目で見つけたかが同じか
    local name="$1" c1 c4 f1 f4; shift
    st "$@" -j 1 --no-to-in >"$build_dir/${name}_j1.log" 2>&1
    c1="$(python3 -c 'import json;print(json.load(open(".stress/last/meta.json"))["case"])')"
    st "$@" -j 4 --no-to-in >"$build_dir/${name}_j4.log" 2>&1
    c4="$(python3 -c 'import json;print(json.load(open(".stress/last/meta.json"))["case"])')"
    f1="$(grep -o 'found after [0-9]* cases' "$build_dir/${name}_j1.log")"
    f4="$(grep -o 'found after [0-9]* cases' "$build_dir/${name}_j4.log")"
    if [ -n "$c1" ] && [ "$c1" = "$c4" ] && [ -n "$f1" ] && [ "$f1" = "$f4" ]; then echo "ok   $name"
    else echo "FAIL $name (j1: $c1 $f1, j4: $c4 $f4)"; fail=1; fi
}
for s in 1 2 3; do same_case "par_same_wa_$s" --main main_wa.cpp --seed "$s"; done
same_case par_same_re --main main_re.cpp --seed 2
same_case par_same_no_shrink --main main_wa.cpp --seed 4 --no-shrink
same_case par_same_checker -c checker.cpp --main main_wa.cpp --seed 5
check par_ok        0 st --main main_ok.cpp -n 300 --seed 1 -j 4
grep -q 'OK  300 cases' "$build_dir/par_ok.log" && echo "ok   par_ok_count" || { echo "FAIL par_ok_count"; fail=1; }
check par_auto      0 st --main main_ok.cpp -n 50 --seed 1 -j
check par_bad       2 st --main main_ok.cpp -j -1
# -j を書かなければ STRESS_JOBS が既定。-j を書けばそちらが優先。サブコマンドにも影響しない
check par_env       0 env STRESS_JOBS=3 python3 "$root/stress.py" --main main_ok.cpp -n 50 --seed 1
log_has par_env_jobs par_env 'jobs=3'
check par_env_override 0 env STRESS_JOBS=3 python3 "$root/stress.py" --main main_ok.cpp -n 50 --seed 1 -j 1
if grep -q 'jobs=' "$build_dir/par_env_override.log"; then echo "FAIL par_env_override_serial"; fail=1; else echo "ok   par_env_override_serial"; fi
check par_env_bad   2 env STRESS_JOBS=abc python3 "$root/stress.py" --main main_ok.cpp -n 5
log_has par_env_bad_msg par_env_bad 'invalid STRESS_JOBS'
check par_env_repro 0 env STRESS_JOBS=3 python3 "$root/stress.py" repro 1:1:gen --main main_ok.cpp
check par_gen_fail  2 st --main main_ok.cpp --gen-file gen_bad.cpp --seed 1 -j 4
log_has par_gen_fail_msg par_gen_fail '^GEN_FAIL '
# 本当の TLE は単独で測り直しても TLE なので、そのまま失敗
check par_tle       1 st --main main_tle.cpp --seed 3 --tl 1 -j 4 --no-to-in
kind_is par_tle TLE
log_has par_tle_found par_tle '^found after'

# 縮小も並列。gen error は飛ばし、Ctrl+C で縮小をやめて保存する
cd "$work_s"
check par_shrink_skips_gen_fail 1 st --seed 1 -j 4 --no-to-in
kind_is par_shrink_skips_gen_fail WA
log_has par_shrink_gen_fail_warning par_shrink_skips_gen_fail '^warning: .*GEN_FAIL|^  GEN_FAIL'
check par_shrink_interrupt 1 python3 "$root/tests/limits/interrupt.py" "shrinking" "$root/stress.py" --seed 1 -j 4 --no-to-in
kind_is par_shrink_interrupt WA
log_has par_shrink_interrupt_msg par_shrink_interrupt '^shrink interrupted'

# 並列でも子プロセスの上限が付く。Ctrl+C で全部止まる
cd "$work_l"
check par_limits_output 1 st --main main_spam.cpp -n 4 --seed 1 --output-limit 1 --no-to-in -j 4
kind_is par_limits_output OLE
[ "$(stat -c %s .stress/last/out_main.txt)" -le 1048576 ] && echo "ok   par_limits_output_size" || { echo "FAIL par_limits_output_size"; fail=1; }
check par_limits_deep 0 st --main main_deep.cpp -n 4 --seed 1 -j 4
check par_limits_interrupt 130 python3 "$root/tests/limits/interrupt.py" "mode=" "$root/stress.py" --main main_loop.cpp --tl 60 -n 8 --seed 1 -j 4
sleep 0.5
if pgrep -f "$work_l/.stress/bin/main-" >/dev/null; then echo "FAIL par_limits_interrupt_no_leftover"; pkill -9 -f "$work_l/.stress/bin/main-"; fail=1; else echo "ok   par_limits_interrupt_no_leftover"; fi

# インタラクティブと、STRESS_BIG の寿命つきの順番
cd "$work3"
check par_inter_ok  0 st -i interactor.cpp --main main_ok.cpp -n 300 --seed 1 -j 4
same_case par_same_inter_wa -i interactor.cpp --main main_wa.cpp --seed 1
cd "$work_rr"
check par_rr_no_skip 1 st -g a,b,c --big-iters 1 --seed 1 --no-to-in -j 3
log_has par_rr_found_after_3 par_rr_no_skip '^found after 3 cases'

# -j で TLE が出ても、単独で測り直して通るなら -j 1 に切り替えて続ける
# （この main は、ほかの main と同時に動くと 3 秒待つ）
work_b="$build_dir/busy"
rm -rf "$work_b" && mkdir -p "$work_b" && cd "$work_b"
cat > gen.cpp << 'EOF'
#include "stress/gen.hpp"
void gen(Gen& g) { g.out(1); (void)g; }
EOF
cat > guchoku.cpp << 'EOF'
#include <iostream>
int main() { std::cout << "1\n"; }
EOF
cat > main.cpp << 'EOF'
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <fcntl.h>
#include <sys/file.h>
int main() {
    int fd = open(std::getenv("BUSY_LOCK"), O_RDWR | O_CREAT, 0644);
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) std::this_thread::sleep_for(std::chrono::seconds(3));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    std::cout << "1\n";
}
EOF
check par_busy_switch 0 env BUSY_LOCK="$work_b/lock" python3 "$root/stress.py" -n 40 --seed 1 --tl 1 -j 4
log_has par_busy_warning par_busy_switch '^warning: -j 4 では TLE だったケースが単独では通った'
grep -q 'OK  40 cases' "$build_dir/par_busy_switch.log" && echo "ok   par_busy_count" || { echo "FAIL par_busy_count"; fail=1; }

# 空白や特殊文字を含むインクルード先（--cxxflags のクォート、.d 依存のキャッシュと更新検知）
work_d="$build_dir/deps_cache"
rm -rf "$work_d" && mkdir -p "$work_d/inc dir:2" && cd "$work_d"
cat > gen.cpp << 'EOF'
#include "stress/gen.hpp"
void gen(Gen& g) { g.out(1); (void)g; }
EOF
cat > guchoku.cpp << 'EOF'
#include <iostream>
int main() { std::cout << "1\n"; }
EOF
cat > main.cpp << 'EOF'
#include <iostream>
#include "val#1$x.hpp"
int main() { int x; std::cin >> x; std::cout << VAL << '\n'; }
EOF
printf 'constexpr int VAL = 1;\n' > "$work_d/inc dir:2/val#1\$x.hpp"
python3 - "$work_d" << 'PY'
import os, sys, time
root = sys.argv[1]
t = time.time() - 20
for rel in ("gen.cpp", "guchoku.cpp", "main.cpp", "inc dir:2/val#1$x.hpp"):
    os.utime(os.path.join(root, rel), (t, t))
PY
inc_flag="-I'$work_d/inc dir:2'"
check deps_cxxflags_space_ok 0 st -n 1 --seed 1 --cxxflags "$inc_flag"
mtime1="$(python3 -c 'import glob,os;p=[x for x in glob.glob(".stress/bin/main-*") if not x.endswith(".d")];print(os.stat(p[0]).st_mtime_ns)')"
check deps_cache_hit 0 st -n 1 --seed 1 --cxxflags "$inc_flag"
mtime2="$(python3 -c 'import glob,os;p=[x for x in glob.glob(".stress/bin/main-*") if not x.endswith(".d")];print(os.stat(p[0]).st_mtime_ns)')"
if [ -n "$mtime1" ] && [ "$mtime1" = "$mtime2" ]; then echo "ok   deps_cache_no_rebuild"; else echo "FAIL deps_cache_no_rebuild ($mtime1 vs $mtime2)"; fail=1; fi
printf 'constexpr int VAL = 2;\n' > "$work_d/inc dir:2/val#1\$x.hpp"
python3 - "$work_d/inc dir:2/val#1\$x.hpp" << 'PY'
import os, sys, time
t = time.time() + 10
os.utime(sys.argv[1], (t, t))
PY
check deps_stale_rebuild 1 st -n 1 --seed 1 --cxxflags "$inc_flag" --no-to-in
kind_is deps_stale_rebuild WA

# stress.py 内のヘルパ単体チェック（_parse_make_deps / Transcript.note）
check py_helpers 0 python3 -B - "$root" << 'PY'
import importlib.util, sys
from pathlib import Path

root = Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("stress_mod", root / "stress.py")
mod = importlib.util.module_from_spec(spec)
sys.modules["stress_mod"] = mod
spec.loader.exec_module(mod)

exe = Path("/tmp/sub:dir/main-12345678")
dep = "/tmp/sub:dir/main-12345678: main.cpp \\\r\n /tmp/inc\\ dir/a:b/h\\#1$$2\\\\x.hpp\n"
parsed = mod._parse_make_deps(exe, dep)
assert parsed == ["main.cpp", r"/tmp/inc dir/a:b/h#1$2\x.hpp"], parsed

tr = mod.Transcript()
for i in range(tr.HEAD + tr.TAIL + 3):
    tr.note(f"line-{i}")
assert tr.dropped == 3, tr.dropped
assert any("3 lines omitted" in ln for ln in tr.lines())

assert mod._link_error("/usr/bin/ld: cannot find -lstdc++: No such file or directory\ncollect2: error: ld returned 1 exit status")
assert mod._link_error("/usr/bin/ld: -lstdc++ が見つかりません: そのようなファイルやディレクトリはありません\ncollect2: エラー: ld はステータス 1 で終了しました")
assert not mod._link_error("main.cpp:3:5: error: 'foo' was not declared in this scope")

import tempfile
with tempfile.TemporaryDirectory() as t:
    d = Path(t) / "case"
    mod.fresh_dir(d)
    (d / "in.txt").write_text("1")
    (d / "sub").mkdir()
    (d / "sub" / "x").write_text("2")
    assert mod.fresh_dir(d) == d and d.is_dir() and not any(d.iterdir())

    # 作業場所: /dev/shm が使えればそこに置き、.stress/run をリンクにする（前の実ディレクトリは置き換える）。使えなければ .stress/run
    w = Path(t) / "prob"
    (w / ".stress" / "run").mkdir(parents=True)
    (w / ".stress" / "run" / "old.txt").write_text("old")
    if mod.SHM.is_dir():
        r = mod.work_root(w)
        assert r.parent.parent == mod.SHM and (w / ".stress" / "run").is_symlink() and (w / ".stress" / "run").resolve() == r.resolve(), r
        assert mod.work_root(w) == r  # 2 回目も同じ場所
        import shutil
        shutil.rmtree(r)
    mod.SHM = Path(t) / "no_such_shm"
    r = mod.work_root(w)
    assert r == w / ".stress" / "run" and not r.is_symlink(), r
PY

# Runner（-j）: 測り直しで止めて戻したケースを取りこぼさない。偽の evaluate で、走ったケースと見つかる失敗を確かめる
check py_runner 0 python3 -B - "$root" "$build_dir/runner" << 'PY'
import importlib.util, sys, time
from pathlib import Path

root = Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("stress_mod", root / "stress.py")
mod = importlib.util.module_from_spec(spec)
sys.modules["stress_mod"] = mod
spec.loader.exec_module(mod)
logs = []
mod.Out.log = lambda *a: logs.append(" ".join(map(str, a)))
mod.Out.status = lambda *a: None

def search(n, jobs, plan, alone=None):
    """plan: seed -> (秒, 並列のときの判定)。alone: seed -> 単独で測り直したときの判定（無ければ OK）"""
    ran = []
    def ev(case, d):
        tok = getattr(mod._tls, "token", None)
        sec, kind = plan.get(case.seed, (0.02, "OK"))
        if tok is None:  # 今のスレッドで 1 本だけ（測り直しか、-j 1 に切り替えたあと）
            ran.append(case.seed)
            return mod.Verdict((alone or {}).get(case.seed, "OK" if kind in mod.RECHECK else kind))
        t = time.time()
        while time.time() - t < sec:
            if tok.cancelled:
                raise mod.Cancelled()
            time.sleep(0.005)
        ran.append(case.seed)
        return mod.Verdict(kind)
    r = mod.Runner(Path(sys.argv[2]), jobs, ev)
    try:
        best = r.search([mod.Case(i, None, "g") for i in range(n)], lambda v: v.kind != "OK")
    finally:
        r.close()
    return (None if best is None else (best[0], best[2].kind)), sorted(set(ran))

# 末尾で偽 TLE: it が尽きたあとに止めて戻したケースも走らせる
best, ran = search(10, 4, {7: (0.3, "TLE"), 8: (1.0, "OK"), 9: (1.0, "OK")})
assert best is None and ran == list(range(10)), (best, ran)
assert any("単独では通った" in ln for ln in logs), logs

# 後ろの失敗が先に決まったあとで前のケースが偽 TLE: 止めて戻した前のケースを走らせ、番号が一番小さい失敗を返す
best, ran = search(10, 4, {0: (0.4, "TLE"), 1: (1.0, "WA"), 2: (0.05, "WA"), 3: (1.0, "OK")})
assert best == (1, "WA"), (best, ran)

# 単独で測り直したら WA: 「通った」とは書かず、WA として返す
logs.clear()
best, ran = search(4, 4, {0: (0.1, "TLE"), 1: (1.0, "OK"), 2: (1.0, "OK"), 3: (1.0, "OK")}, alone={0: "WA"})
assert best == (0, "WA"), best
assert not any("通った" in ln for ln in logs) and any("WA だった" in ln for ln in logs), logs
PY

# build: PCH なしでやり直したあとに静的リンクなしでもやり直すとき、-include も -static も付けない（g++ は偽物）
check py_build_fallback 0 python3 -B - "$root" "$build_dir/build_fallback" << 'PY'
import importlib.util, shutil, sys, types
from pathlib import Path

root = Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("stress_mod", root / "stress.py")
mod = importlib.util.module_from_spec(spec)
sys.modules["stress_mod"] = mod
spec.loader.exec_module(mod)

w = Path(sys.argv[2])
shutil.rmtree(w, ignore_errors=True)
(w / "ac-library").mkdir(parents=True)
(w / "pch_acl.hpp.gch").write_text("")  # pch_acl.hpp 本体は無い
src = w / "main.cpp"
src.write_text("#include <atcoder/dsu>\nint main() {}\n")
calls = []
def fake_run(cmd, **kw):
    calls.append(cmd)
    if "-include" in cmd:
        return types.SimpleNamespace(returncode=1, stderr="<command-line>: fatal error: pch_acl.hpp: No such file or directory\n")
    if "-static" in cmd:
        return types.SimpleNamespace(returncode=1, stderr="/usr/bin/ld: -lc が見つかりません\ncollect2: エラー: ld はステータス 1 で終了しました\n")
    return types.SimpleNamespace(returncode=0, stderr="")
mod.subprocess.run = fake_run
mod.build(mod.BuildConfig(w), src, "main")
assert len(calls) == 3 and "-include" in calls[0] and "-static" in calls[1] and "-include" not in calls[2] and "-static" not in calls[2], calls
PY

# pch_acl.hpp 本体が無くても .gch だけで動き、フラグ不一致なら PCH なしでやり直す
work_p="$build_dir/pch"
rm -rf "$work_p" && mkdir -p "$work_p/ac-library/atcoder" && cd "$work_p"
cat > ac-library/atcoder/dummy.hpp << 'EOF'
#pragma once
inline int dummy_val() { return 1; }
EOF
cat > pch_acl.hpp << 'EOF'
#include <atcoder/dummy.hpp>
EOF
cat > gen.cpp << 'EOF'
#include "stress/gen.hpp"
void gen(Gen& g) { g.out(1); (void)g; }
EOF
cat > guchoku.cpp << 'EOF'
#include <iostream>
int main() { std::cout << "1\n"; }
EOF
cat > main.cpp << 'EOF'
#include <iostream>
#include <atcoder/dummy.hpp>
int main() { int x; std::cin >> x; std::cout << dummy_val() << '\n'; }
EOF
g++ -std=gnu++20 -O2 -DLOCAL -Iac-library -x c++-header pch_acl.hpp -o pch_acl.hpp.gch
rm pch_acl.hpp
check pch_match 0 env ACL_DIR="$work_p/ac-library" CP_LIBRARY_DIR="$work_p/no_lib" python3 "$root/stress.py" -n 2 --seed 1
check pch_mismatch_fallback 0 env ACL_DIR="$work_p/ac-library" CP_LIBRARY_DIR="$work_p/no_lib" python3 "$root/stress.py" -n 2 --seed 1 --cxxflags=-O0

# 後片付け: テストで使った各ディレクトリで st clean を呼び、/dev/shm 側の作業場所も消えることを確かめる
clean_bad=""
while IFS= read -r s; do
    target=""
    [ -L "$s/run" ] && target="$(readlink "$s/run")"
    (cd "$(dirname "$s")" && python3 "$root/stress.py" clean) >/dev/null 2>&1 || clean_bad="$clean_bad $s(exit)"
    { [ -n "$target" ] && [ -e "$target" ]; } && clean_bad="$clean_bad $target"
    { [ -e "$s/run" ] || [ -L "$s/run" ]; } && clean_bad="$clean_bad $s/run"
done < <(find "$build_dir" -type d -name .stress -prune)
if [ -z "$clean_bad" ]; then echo "ok   clean_shm"; else echo "FAIL clean_shm (left:$clean_bad)"; fail=1; fi

[ "$fail" -eq 0 ] && echo "All tests passed" || { echo "Some tests failed"; exit 1; }
