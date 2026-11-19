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
log_lacks() {  # log_lacks <名前> <check の名前> <grep -E のパターン>
    if grep -qE "$3" "$build_dir/$2.log"; then echo "FAIL $1 (/$3/ in $2.log)"; grep -E "$3" "$build_dir/$2.log" | head -5; fail=1; else echo "ok   $1"; fi
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

# 引数の検証（正の数・1 以上。外れたら終了コード 2 で in.txt も変えない）
printf 'keep me\n' > in.txt
check arg_tl_neg         2 st --main main_ok.cpp --tl -1
check arg_tl_zero        2 st --main main_ok.cpp --tl 0
check arg_tl_nan         2 st --main main_ok.cpp --tl nan
check arg_tl_inf         2 st --main main_ok.cpp --tl inf
check arg_ref_tl_zero    2 st --main main_ok.cpp --ref-tl 0
check arg_checker_tl_neg 2 st --main main_ok.cpp --checker-tl -0.5
check arg_gen_tl_zero    2 st --main main_ok.cpp --gen-tl 0
check arg_outlim_zero    2 st --main main_ok.cpp --output-limit 0
check arg_outlim_neg     2 st --main main_ok.cpp --output-limit -1
check arg_time_zero      2 st --main main_ok.cpp -t 0
check arg_time_neg       2 st --main main_ok.cpp -t -1
check arg_max_cap_zero   2 st --main main_ok.cpp --max-cap 0
check arg_iters_zero     2 st --main main_ok.cpp -n 0
check arg_shrink_tries_0 2 st --main main_ok.cpp --shrink-tries 0
check arg_shrink_time_0  2 st --main main_ok.cpp --shrink-time 0
check arg_make_n_zero    2 st make -n 0
check arg_make_cap_zero  2 st make --cap 0
check arg_bench_n_zero   2 st bench -n 0
check arg_bench_cap_zero 2 st bench --cap 0
check arg_bench_tl_neg   2 st bench --tl -1
check arg_repro_tl_zero  2 st repro 1:1 --tl 0
[ "$(cat in.txt)" = "keep me" ] && echo "ok   arg_invalid_keeps_in" || { echo "FAIL arg_invalid_keeps_in"; fail=1; }
rm -f in.txt

# st --help にサブコマンドの一覧が出る
check help_subcommands 0 st --help
for cmd in repro make bench last restore clean init; do
    log_has "help_has_$cmd" help_subcommands "^  $cmd "
done

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
st --main main_wa.cpp --no-leftover --seed 1 --no-to-in >/dev/null 2>&1
repro_cmd_nolo="$(python3 -c 'import json;print(json.load(open(".stress/last/meta.json"))["repro"])')"
case "$repro_cmd_nolo" in
    *"--no-leftover"*) echo "ok   repro_cmd_no_leftover" ;;
    *) echo "FAIL repro_cmd_no_leftover ($repro_cmd_nolo)"; fail=1 ;;
esac
check repro_cmd_no_leftover_reproduces 1 st ${repro_cmd_nolo#st } --no-to-in

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
# repro で道具の側の失敗（GEN_FAIL / CHECKER_FAIL）が起きたら終了コード 2、保存せず in.txt も変えない
fail_before="$(ls -1 .stress/fail | wc -l)"
cp in.txt "$build_dir/in_before_tool_fail.txt"
check repro_gen_fail 2 st repro 5:2 --main main_ok.cpp --gen-file gen_bad.cpp
log_has repro_gen_fail_msg repro_gen_fail '^GEN_FAIL '
check repro_checker_crash 2 st repro 5:2 -c checker_crash.cpp --main main_ok.cpp
log_has repro_checker_crash_msg repro_checker_crash '^CHECKER_FAIL '
fail_after="$(ls -1 .stress/fail | wc -l)"
if [ "$fail_before" -eq "$fail_after" ]; then echo "ok   repro_tool_fail_no_save"; else echo "FAIL repro_tool_fail_no_save ($fail_before -> $fail_after)"; fail=1; fi
if cmp -s in.txt "$build_dir/in_before_tool_fail.txt"; then echo "ok   repro_tool_fail_keeps_in"; else echo "FAIL repro_tool_fail_keeps_in"; fail=1; fi

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

# 縮小中の Ctrl+C: 縮小をやめて、見つけた失敗を保存する。Ctrl+C は「shrinking」の 1 秒後に送るので、
# 縮小が必ずそれより長くかかるように、1 回ごとに待つ main_slow.cpp を使う
check shrink_interrupt 1 python3 "$root/tests/limits/interrupt.py" "shrinking" "$root/stress.py" --seed 1 --main main_slow.cpp
kind_is shrink_interrupt WA
log_has shrink_interrupt_msg shrink_interrupt '^shrink interrupted'
[ "$(head -1 in.txt)" -ge 6 ] 2>/dev/null && echo "ok   shrink_interrupt_in_txt" || { echo "FAIL shrink_interrupt_in_txt"; fail=1; }

# 2 段階縮小: n >= 20 でだけ落ちる main_n20.cpp が大きい cap（61）で見つかったあと、
# 短い --shrink-time でも 1 回目の縮小で cap 20 まで縮む（1 段階縮小だと cap 1..16 の 2000 回で時間切れになる）
check shrink_n20 1 st --gen-file gen_large.cpp --main main_n20.cpp --seed 19000 --shrink-time 5 --no-to-in
kind_is shrink_n20 WA
log_has shrink_n20_shrunk shrink_n20 '^shrunk: cap [4-9][0-9] -> 20$'
shrink_n20_info="$(python3 -c '
import json, re, sys
log = open(sys.argv[1]).read()
m = json.load(open(".stress/last/meta.json"))
n = int(open(".stress/last/in.txt").readline())
mt = re.search(r"found after \d+ cases \(([0-9.]+)s\)", log)
elapsed = float(mt.group(1)) if mt else 999.0
cap = m["cap"]
print(f"elapsed={elapsed:.1f}s cap={cap} n={n}")
sys.exit(0 if elapsed < 20.0 and cap <= 25 and n >= 20 else 1)
' "$build_dir/shrink_n20.log")" && echo "ok   shrink_n20_result ($shrink_n20_info)" || { echo "FAIL shrink_n20_result ($shrink_n20_info)"; fail=1; }

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
rm -rf "$build_dir/make_abs"
check make_abs          0 st make max -n 1 --seed 3 -o "$build_dir/make_abs"
if [ -f "$build_dir/make_abs/max-01.txt" ]; then echo "ok   make_abs_file"; else echo "FAIL make_abs_file"; fail=1; fi
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
check inter_adaptive_guess 1 st -i interactor_adaptive.cpp --main main_guess.cpp --seed 1 -n 300
# it.send に std::string_view / std::string / const char* を渡しても 1 つの文字列として送られる
cat > interactor_sv.cpp << 'EOF'
#include <string>
#include <string_view>
#include "stress/interact.hpp"
void interact(Interactor& it) {
    int n, x;
    it.in >> n >> x;
    it.send(n);
    it.query_limit(25);
    while (true) {
        std::string t;
        it.out >> t;
        if (t == "?") {
            int y;
            it.out >> y;
            it.ensure(1 <= y && y <= n, "y is out of range");
            it.count_query();
            const char* raw = x <= y ? "Yes" : "No";
            if (it.queries() % 3 == 1) it.send(std::string_view(raw));
            else if (it.queries() % 3 == 2) it.send(std::string(raw));
            else it.send(raw);
        } else if (t == "!") {
            int ans;
            it.out >> ans;
            it.ensure(ans == x, "wrong answer");
            return;
        } else {
            it.wrong("unknown command: " + t);
        }
    }
}
EOF
check inter_string_view 0 st -i interactor_sv.cpp --main main_ok.cpp -n 50 --seed 1
# 改行なしで数十 MB 出力する main でも TLE で止まり、st のメモリが増え続けない
cat > main_spam_nonl.cpp << 'EOF'
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
int main() {
    int n;
    if (!(std::cin >> n)) return 0;
    std::string chunk(1 << 16, ' ');
    for (int i = 0; i < 1024; ++i) {  // 64 MB を改行なしで出す
        size_t off = 0;
        while (off < chunk.size()) {
            ssize_t w = ::write(1, chunk.data() + off, chunk.size() - off);
            if (w <= 0) return 0;
            off += static_cast<size_t>(w);
        }
    }
    std::this_thread::sleep_for(std::chrono::seconds(10));
}
EOF
# ビルドのピークメモリ（g++）が測る値に混ざらないよう、先に 1 回ビルドだけ済ませておく
st -i interactor.cpp --main main_spam_nonl.cpp -n 1 --seed 1 --tl 0.2 --no-shrink --no-to-in >/dev/null 2>&1 || true
check inter_nonl_spam 0 python3 - "$root/stress.py" << 'PY'
import json, os, re, subprocess, sys
from pathlib import Path
p = subprocess.Popen([sys.executable, sys.argv[1], "-i", "interactor.cpp",
                      "--main", "main_spam_nonl.cpp", "-n", "1", "--seed", "1",
                      "--tl", "4.0", "--no-shrink", "--no-to-in"])
_, status, ru = os.wait4(p.pid, 0)
p.returncode = os.waitstatus_to_exitcode(status)
assert p.returncode == 1, f"exit {p.returncode}"
meta = json.loads(Path(".stress/last/meta.json").read_text())
assert meta["kind"] == "TLE", meta
tr = Path(".stress/last/transcript.txt").read_text()
m = re.search(r"\.\.\. \(\+(\d+) chars\)  \(no newline\)", tr)
assert m and int(m.group(1)) >= 60_000_000, f"m={m and m.group(0)!r} len={len(tr)} tail={tr[-120:]!r}"
assert ru.ru_maxrss < 80_000, f"RSS too high: {ru.ru_maxrss} KB"
PY

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
check par_shrink_interrupt 1 python3 "$root/tests/limits/interrupt.py" "shrinking" "$root/stress.py" --seed 1 -j 4 --no-to-in --main main_slow.cpp
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

# Transcript.feed: 改行なしの大量出力で partial が WIDTH を超えない（CRLF 分割・UTF-8 分割・finish を含む）
tr2 = mod.Transcript()
chunk = b"a" * (1 << 16)
for _ in range(160):  # 10 MB 改行なし
    tr2.feed(">", chunk)
    assert len(tr2.partial[">"]) <= tr2.WIDTH
tr2.feed(">", b"\r\n")
assert tr2.lines() == [f"> {'a' * tr2.WIDTH} ... (+{160 * (1 << 16) - tr2.WIDTH} chars)"], tr2.lines()

tr3 = mod.Transcript()
tr3.feed(">", b"b" * tr3.WIDTH)
tr3.feed(">", b"\r")
tr3.feed(">", b"\n")  # 境界をまたぐ \r\n は extra に数えない
tr3.feed(">", "あ".encode()[:2])
tr3.feed(">", "あ".encode()[2:] + b"\n")  # 境界をまたぐマルチバイト UTF-8
tr3.feed("<", b"c" * (tr3.WIDTH + 50))
tr3.finish("<")
assert tr3.lines() == [
    f"> {'b' * tr3.WIDTH}",
    "> あ",
    f"< {'c' * tr3.WIDTH} ... (+50 chars)  (no newline)",
], tr3.lines()

assert mod._link_error("/usr/bin/ld: cannot find -lstdc++: No such file or directory\ncollect2: error: ld returned 1 exit status")
assert mod._link_error("/usr/bin/ld: -lstdc++ が見つかりません: そのようなファイルやディレクトリはありません\ncollect2: エラー: ld はステータス 1 で終了しました")
assert not mod._link_error("main.cpp:3:5: error: 'foo' was not declared in this scope")

import tempfile
with tempfile.TemporaryDirectory() as t:
    # read_interactor_report: 壊れた行を飛ばし、値が壊れていれば None
    rp = Path(t) / "report.txt"
    assert mod.read_interactor_report(rp) is None
    rp.write_text("queries 7\nlimit 25\n")
    assert mod.read_interactor_report(rp) == (7, 25)
    rp.write_text("queries 7\ngarbage_only_one_col\nlimit 25\n")
    assert mod.read_interactor_report(rp) == (7, 25)
    rp.write_text("only_one_col\n")
    assert mod.read_interactor_report(rp) is None
    rp.write_text("queries not_an_int\nlimit 25\n")
    assert mod.read_interactor_report(rp) is None
    rp.write_text("")
    assert mod.read_interactor_report(rp) is None

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

# 2 段階縮小の単体チェック（1 回目で仮の答えを取り、2 回目でさらに縮む / 見つからず・中断で 1 回目を残す / 1 回目不発 / -j 1 == -j 4 / status 間引き）
check py_shrink 0 python3 -B - "$root" "$build_dir/py_shrink" << 'PY'
import importlib.util, shutil, sys, time, types
from pathlib import Path

root = Path(sys.argv[1])
work = Path(sys.argv[2])
spec = importlib.util.spec_from_file_location("stress_mod", root / "stress.py")
mod = importlib.util.module_from_spec(spec)
sys.modules["stress_mod"] = mod
spec.loader.exec_module(mod)
logs = []
status_calls = 0
mod.Out.log = lambda *a: logs.append(" ".join(map(str, a)))
def count_status(*a):
    global status_calls
    status_calls += 1
mod.Out.status = count_status

orig = mod.Case(12345, 50, "g")
base = mod.splitmix64(orig.seed ^ 0x5EED)
opt = types.SimpleNamespace(shrink_tries=30)

def seed_of(c, t):
    return mod.case_seed(base, c * 1_000_003 + t)

def run_case_shrink(tag, jobs, judge_fn):
    d_root = work / f"{tag}_j{jobs}"
    shutil.rmtree(d_root, ignore_errors=True)
    d_root.mkdir(parents=True)
    def ev(case, d):
        d.mkdir(parents=True, exist_ok=True)
        (d / "in.txt").write_text(f"{case.seed}:{case.cap}")
        return judge_fn(case)
    r = mod.Runner(d_root, jobs, ev)
    try:
        shrunk, tools = mod.shrink(r, orig, opt, time.monotonic() + 30.0)
        saved_in = (shrunk[2] / "in.txt").read_text() if shrunk else None
    finally:
        r.close()
    return (None if shrunk is None else (shrunk[0], shrunk[1].kind, saved_in)), [(c.id, v.kind) for c, v in tools]

# 1. 1 回目で cap=20、2 回目で cap=12 が見つかる（-j 1 と -j 4 が一致）
target_p1 = seed_of(20, 2)
target_p2 = seed_of(12, 15)
def j_both(case):
    if case.seed == target_p1 or case.seed == target_p2:
        return mod.Verdict("WA")
    return mod.Verdict("OK")
r1, t1 = run_case_shrink("both", 1, j_both)
r4, t4 = run_case_shrink("both", 4, j_both)
assert r1 == r4 and r1[0].cap == 12 and r1[2] == f"{target_p2}:12", (r1, r4)

# 2. 1 回目で cap=20、2 回目は不発（1 回目の結果と作業ディレクトリを復元、道具の失敗も両方残る）
tool1, tool2 = seed_of(4, 1), seed_of(6, 12)
def j_p1_only(case):
    if case.seed == target_p1:
        return mod.Verdict("WA")
    if case.seed in (tool1, tool2):
        return mod.Verdict("GEN_FAIL")
    return mod.Verdict("OK")
r1, t1 = run_case_shrink("p1", 1, j_p1_only)
r4, t4 = run_case_shrink("p1", 4, j_p1_only)
assert r1 == r4 and r1[0].cap == 20 and r1[2] == f"{target_p1}:20", (r1, r4)
assert t1 == t4 == [(f"{tool1}:4:g", "GEN_FAIL"), (f"{tool2}:6:g", "GEN_FAIL")], (t1, t4)

# 3. 2 回目で Ctrl+C: 1 回目の仮の答えと作業ディレクトリを残す
logs.clear()
int_seed = seed_of(3, 14)
def j_p2_interrupt(case):
    if case.seed == target_p1:
        return mod.Verdict("WA")
    if case.seed == int_seed:
        raise KeyboardInterrupt()
    return mod.Verdict("OK")
r1, _ = run_case_shrink("p2int", 1, j_p2_interrupt)
assert r1[0].cap == 20 and r1[2] == f"{target_p1}:20", r1
assert any("shrink interrupted" in ln for ln in logs), logs

# 4. 1 回目が不発でも、2 回目で全 cap を探して cap=8 を見つける
target_late = seed_of(8, 22)
def j_p2_only(case):
    return mod.Verdict("WA") if case.seed == target_late else mod.Verdict("OK")
status_calls = 0
r1, _ = run_case_shrink("p2", 1, j_p2_only)
assert status_calls <= 10, f"Out.status was not throttled: {status_calls}"
r4, _ = run_case_shrink("p2", 4, j_p2_only)
assert r1 == r4 and r1[0].cap == 8 and r1[2] == f"{target_late}:8", (r1, r4)
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

# build: PCH なしでやり直したあとに静的リンクなしでもやり直すとき、PCH の -include も -static も付けない（g++ は偽物）。
# 読み残しを調べる leftover.hpp の -include は、やり直しても残る
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
    if "pch_acl.hpp" in cmd:
        return types.SimpleNamespace(returncode=1, stderr="<command-line>: fatal error: pch_acl.hpp: No such file or directory\n")
    if "-static" in cmd:
        return types.SimpleNamespace(returncode=1, stderr="/usr/bin/ld: -lc が見つかりません\ncollect2: エラー: ld はステータス 1 で終了しました\n")
    return types.SimpleNamespace(returncode=0, stderr="")
mod.subprocess.run = fake_run
mod.build(mod.BuildConfig(w), src, "main")
assert len(calls) == 3 and "pch_acl.hpp" in calls[0] and "-static" in calls[1], calls
assert "pch_acl.hpp" not in calls[2] and "-static" not in calls[2], calls
assert all(any(a.endswith("stress/leftover.hpp") for a in c) for c in calls), calls
# PCH の後ろに leftover.hpp を置く
assert calls[0].index("pch_acl.hpp") < calls[0].index(next(a for a in calls[0] if a.endswith("leftover.hpp"))), calls[0]
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

# 入力の読み残し（stress/leftover.hpp）。まず st を通さず -include で差し込む使い方
work_lo="$build_dir/leftover"
rm -rf "$work_lo" && cp -r "$root/tests/leftover" "$work_lo" && cd "$work_lo"
python3 -c "print(200000); print(' '.join(['7'] * 200000)); print('extra')" > in_big.txt
python3 -c "print(200000); print(' '.join(['7'] * 200000))" > in_bigok.txt
python3 -c "print(1); print(1); print('a' * 100)" > in_longtok.txt
printf '1\n1\n10 20 30 40 50\n\n' > in_five.txt
leftover_case() {  # leftover_case <名前> <プログラム> <入力> <want: yes/no> [<stderr に含まれるはずの文字列>]
    local name="$1" exe="$2" in="$3" want="$4" pat="${5:-}" err out
    err="$("./$exe" < "$in" 2>&1 >/dev/null)"
    out="$("./$exe" < "$in" 2>/dev/null)"
    if [ "$want" = yes ]; then
        if [[ "$err" == *"[leftover] "* && "$err" == *"$pat"* && -n "$out" ]]; then echo "ok   $name"; else echo "FAIL $name (stderr: $err)"; fail=1; fi
    else
        if [ -z "$err" ] && [ -n "$out" ]; then echo "ok   $name"; else echo "FAIL $name (stderr: $err)"; fail=1; fi
    fi
}
lo_built=1
for p in read_cin read_fast read_scanf read_exit; do
    g++ -std=gnu++20 -O2 -Wall -Wextra -Werror -include "$root/stress/leftover.hpp" "$p.cpp" -o "$p" 2>"$p.build.log" \
        || { echo "FAIL leftover_build_$p"; tail -20 "$p.build.log"; fail=1; lo_built=0; }
done
if [ "$lo_built" -eq 1 ]; then
    long_want="3 行目から（残り 1 行）: $(python3 -c "print('a' * 77 + '...')")"
    for p in read_cin read_fast read_scanf read_exit; do
        leftover_case "leftover_${p}_multi"   "$p" in_multi.txt   yes "3 行目から（残り 1 行）: 5 3 2"
        leftover_case "leftover_${p}_midline" "$p" in_midline.txt yes "2 行目から（残り 2 行）: 4 5 6 7 8 ..."
        leftover_case "leftover_${p}_big"     "$p" in_big.txt     yes "3 行目から（残り 1 行）: extra"
        leftover_case "leftover_${p}_longtok" "$p" in_longtok.txt yes "$long_want"
        leftover_case "leftover_${p}_five"    "$p" in_five.txt    yes "3 行目から（残り 1 行）: 10 20 30 40 50"
        # ちょうど 5 トークンのときは末尾に "..." を付けない
        err="$("./$p" < in_five.txt 2>&1 >/dev/null)"
        if [[ "$err" != *"..."* ]]; then echo "ok   leftover_${p}_five_no_dots"; else echo "FAIL leftover_${p}_five_no_dots ($err)"; fail=1; fi
        leftover_case "leftover_${p}_ok"      "$p" in_ok.txt      no
        leftover_case "leftover_${p}_bigok"   "$p" in_bigok.txt   no
        # パイプのときは調べない（st -i の対話で止まらないように）
        err="$(cat in_multi.txt | "./$p" 2>&1 >/dev/null)"
        if [ -z "$err" ]; then echo "ok   leftover_${p}_pipe"; else echo "FAIL leftover_${p}_pipe ($err)"; fail=1; fi
    done
fi
# PCH の後ろに差し込んでも PCH が使われる
mkdir -p pch && printf '#include <bits/stdc++.h>\n' > pch/pch.hpp
if g++ -std=gnu++20 -O2 -x c++-header pch/pch.hpp -o pch/pch.hpp.gch 2>/dev/null; then
    if g++ -std=gnu++20 -O2 -Winvalid-pch -H -include pch/pch.hpp -include "$root/stress/leftover.hpp" read_cin.cpp -o read_pch 2>pch.log \
        && grep -q '^! .*pch.hpp.gch' pch.log; then echo "ok   leftover_with_pch"; else echo "FAIL leftover_with_pch"; head -5 pch.log; fail=1; fi
fi
# グローバルに int stat[10]; を持つ main.cpp でも leftover.hpp と衝突せずビルドして動く
cat > main_stat.cpp << 'EOF'
#include <bits/stdc++.h>
int stat[10];
int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    stat[0] = 1;
    int t;
    std::cin >> t;
    while (t--) {
        int n;
        std::cin >> n;
        std::vector<long long> a(n);
        for (auto& x : a) std::cin >> x;
        std::cout << std::accumulate(a.begin(), a.end(), 0LL) << "\n";
    }
}
EOF
check leftover_stat_symbol 0 st --main main_stat.cpp --guchoku guchoku_ok.cpp -n 5 --seed 1
log_lacks leftover_stat_symbol_clean leftover_stat_symbol '読み残し'
# st: main も愚直解も T を n と読み違えて OK になるが、読み残しの警告が出る
check leftover_run 0 st -n 50 --seed 1
log_has leftover_run_main leftover_run 'main が 50 ケース中 50 ケースで入力を読み残しています'
log_has leftover_run_ref  leftover_run 'guchoku が 50 ケース中 50 ケースで入力を読み残しています'
log_has leftover_run_example leftover_run '例: case=[0-9]+:1:gen  3 行目から（残り 1 行）: '
check leftover_run_par 0 st -n 50 --seed 1 -j 3
log_has leftover_run_par_main leftover_run_par 'main が 50 ケース中 50 ケース'
# 例は -j でも直列と同じ（一番手前のケース）
ex1="$(grep -E '^  例: ' "$build_dir/leftover_run.log" | head -1)"
ex3="$(grep -E '^  例: ' "$build_dir/leftover_run_par.log" | head -1)"
if [ -n "$ex1" ] && [ "$ex1" = "$ex3" ]; then echo "ok   leftover_run_par_example"; else echo "FAIL leftover_run_par_example ($ex1 / $ex3)"; fail=1; fi
check leftover_clean 0 st --main main_ok.cpp --guchoku guchoku_ok.cpp -n 50 --seed 1
log_lacks leftover_clean_msg leftover_clean '読み残し'
check leftover_off 0 st -n 50 --seed 1 --no-leftover
log_lacks leftover_off_msg leftover_off '読み残し'
# checker モード（愚直解は走らない）でも main の読み残しは出る。sample の checker も T を n と読むので OK になる
check leftover_checker 0 st -c "$root/tests/sample/checker.cpp" -n 10 --seed 1
log_has leftover_checker_msg leftover_checker 'main が 10 ケース中 10 ケース'
log_lacks leftover_checker_ref leftover_checker 'guchoku が'
lo_case="$(sed -n 's/^  例: case=\([^ ]*\).*/\1/p' "$build_dir/leftover_run.log" | head -1)"
check leftover_repro 0 st repro "$lo_case"
log_has leftover_repro_msg leftover_repro 'main が入力を読み残しています: 3 行目から'
# 失敗したときも警告は出る（main だけ読み違え、愚直解は正しく読む → WA）
check leftover_wa 1 st --guchoku guchoku_ok.cpp -n 50 --seed 1 --no-to-in
log_has leftover_wa_msg leftover_wa 'main が [0-9]+ ケース中 [0-9]+ ケースで入力を読み残しています'
check leftover_bench 0 st bench --main main.cpp -n 1 --seed 1
log_has leftover_bench_msg leftover_bench 'main が入力を読み残しています'
# インタラクティブ（main の stdin はパイプ）では何も出ない
log_lacks leftover_inter inter_ok '読み残し'
log_lacks stats_inter_no_ans inter_ok '答え（先頭トークン）|^答え:'

# PR C の単体テスト: read_first_token / RunStats / compare の行番号 / around / wait_exit の RSS
check py_prc_units 0 python3 -B - "$root" "$build_dir/prc_units" << 'PY'
import importlib.util, os, shutil, sys
from pathlib import Path

root = Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("stress_mod", root / "stress.py")
mod = importlib.util.module_from_spec(spec)
sys.modules["stress_mod"] = mod
spec.loader.exec_module(mod)

w = Path(sys.argv[2])
shutil.rmtree(w, ignore_errors=True)
w.mkdir(parents=True)

# read_first_token
def rft(data: bytes):
    p = w / "tok.txt"
    p.write_bytes(data)
    return mod.read_first_token(p)

assert rft(b"Yes\n1 2 3\n") == "Yes"
assert rft(b"  \t\n  No\n") == "No"
assert rft(b"") == "(empty)"
assert rft(b"  \n\t\r\n") == "(empty)"
assert rft(b"a" * 40 + b"\n") == "a" * 40
assert rft(b"a" * 41 + b"\n") == "a" * 37 + "..."
assert rft(b"b" * 200_000) == "b" * 37 + "..."
assert rft(b" " * 4094 + b"ab" + b"cd\n") == "abcd"
assert rft(b" " * 4094 + b"ab" + b" cd\n") == "ab"
assert rft(b" " * 70_000 + b"Yes\n") is None
assert mod.read_first_token(w / "missing.txt") is None

# _fmt_pct
assert mod._fmt_pct(0, 100) == "0%"
assert mod._fmt_pct(100, 100) == "100%"
assert mod._fmt_pct(1, 1000) == "0.1%"
assert mod._fmt_pct(999, 1000) == "99.9%"
assert mod._fmt_pct(1, 10000) == "<0.1%"
assert mod._fmt_pct(9999, 10000) == ">99.9%"
assert mod._fmt_pct(50, 100) == "50%"

# RunStats (案B)
s = mod.RunStats()
s.add(mod.Verdict("OK", first_token="Yes"))
assert s.lines() == []  # 1 ケースだけでは出さない

s = mod.RunStats()
for _ in range(5):
    s.add(mod.Verdict("OK", first_token="No"))
assert s.lines() == [("dim", "答え: No 5 (100%)")]  # 10 ケース未満は警告なし

s = mod.RunStats()
for _ in range(10):
    s.add(mod.Verdict("OK", first_token="No"))
assert s.lines() == [("warn", "答え（先頭トークン）がすべて同じです: No (10/10)")]

s = mod.RunStats()
for tok in ["Yes"] * 7 + ["No"] * 3:
    s.add(mod.Verdict("OK", first_token=tok))
assert s.lines() == [("dim", "答え: Yes 7 (70%), No 3 (30%)")]

# 6 種類以上で均等 → 出さない
s = mod.RunStats()
for i in range(60):
    s.add(mod.Verdict("OK", first_token=str(i % 6)))
assert s.lines() == []

# 6 種類以上でも 80% 以上偏っていれば warning
s = mod.RunStats()
for _ in range(80):
    s.add(mod.Verdict("OK", first_token="0"))
for i in range(1, 21):
    s.add(mod.Verdict("OK", first_token=str(i)))
assert s.lines() == [("warn", "答え（先頭トークン）が偏っています: 0 80 (80%), 他 20 (20%)")]

# 79% は warning にしない
s = mod.RunStats()
for _ in range(79):
    s.add(mod.Verdict("OK", first_token="0"))
for i in range(1, 22):
    s.add(mod.Verdict("OK", first_token=str(i)))
assert s.lines() == []

# 64 種類を超える場合も追跡辞書は 64 個で頭打ち
s = mod.RunStats()
for i in range(200):
    s.add(mod.Verdict("OK", first_token=str(i)))
assert len(s.ans_counts) == mod.RunStats.ANS_CAP and s.ans_overflow == 200 - mod.RunStats.ANS_CAP
assert s.lines() == []

# 64 種類を超えても最多が 80% 以上なら warning
s = mod.RunStats()
for _ in range(800):
    s.add(mod.Verdict("OK", first_token="0"))
for i in range(1, 201):
    s.add(mod.Verdict("OK", first_token=str(i)))
assert len(s.ans_counts) == mod.RunStats.ANS_CAP and s.ans_overflow > 0
assert s.lines() == [("warn", "答え（先頭トークン）が偏っています: 0 800 (80%), 他 200 (20%)")]

# タグ: 上位 10 個 + 他 N 種類
s = mod.RunStats()
for i in range(12):
    for _ in range(12 - i):
        s.add(mod.Verdict("OK", tags=[f"t{i:02d}"]))
lines = s.lines()
assert len(lines) == 1 and lines[0][0] == "dim" and "t00 12" in lines[0][1] and "他 2 種類" in lines[0][1], lines

# compare の diff_lines と around
a = "\n".join(str(i) for i in range(1, 31)).encode() + b"\n"
b = "\n".join("999" if i == 25 else str(i) for i in range(1, 31)).encode() + b"\n"
assert mod.compare(a, a, "token") == (True, "", None)
ok, why, dl = mod.compare(a, b, "token")
assert not ok and dl == (25, 25), (ok, why, dl)
ok, why, dl = mod.compare(a, b, "exact")
assert not ok and dl == (25, 25), (ok, why, dl)
ok, why, dl = mod.compare(b" ".join(str(i).encode() for i in range(1, 31)) + b"\n", b, "token")
assert not ok and dl == (1, 25), (ok, why, dl)
short_a = "\n".join(str(i) for i in range(1, 26)).encode() + b"\n"
ok, why, dl = mod.compare(short_a, a, "token")
assert not ok and dl == (26, 26), (ok, why, dl)
af = "\n".join(f"{i}.0" for i in range(1, 31)).encode() + b"\n"
bf = "\n".join("25.05" if i == 25 else f"{i}.0" for i in range(1, 31)).encode() + b"\n"
assert mod.compare(af, af, "float:1e-3") == (True, "", None)
ok, why, dl = mod.compare(af, bf, "float:1e-3")
assert not ok and dl == (25, 25), (ok, why, dl)

pa = w / "short.txt"
pa.write_bytes(short_a)
mod.Out.color = False
snip = mod.around(pa, 26)
assert "  23: 23" in snip and "  25: 25" in snip and "> 26: (EOF)" in snip, snip

# wait_exit / run の Peak RSS（pidfd 経路と WNOHANG フォールバックの両方）
r1 = mod.run([sys.executable, "-c", "pass"], None, w / "o1", w / "e1", 5.0, w)
assert r1.status == "OK" and r1.code == 0 and r1.maxrss_kb is not None and r1.maxrss_kb > 0, r1
orig_pidfd = getattr(os, "pidfd_open", None)
try:
    def no_pidfd(pid):
        raise OSError("disabled")
    os.pidfd_open = no_pidfd
    r2 = mod.run([sys.executable, "-c", "pass"], None, w / "o2", w / "e2", 5.0, w)
    assert r2.status == "OK" and r2.code == 0 and r2.maxrss_kb is not None and r2.maxrss_kb > 0, r2
finally:
    if orig_pidfd is not None:
        os.pidfd_open = orig_pidfd
PY

# 答えとタグの分布（st run）
work_st="$build_dir/stats"
rm -rf "$work_st" && mkdir -p "$work_st" && cd "$work_st"
cat > gen.cpp << 'EOF'
#include "stress/gen.hpp"
STRESS_GEN(yn) {
    int v = g.coin() ? 1 : 0;
    g.tag(v ? "kind=yes" : "kind=no");
    g.out("yn", v);
}
STRESS_GEN(const_no) {
    g.tag("fixed");
    g.out("const", 0);
}
STRESS_GEN(many) {
    g.out("many", g.i(0, 99));
}
STRESS_GEN(biased) {
    int v = g.coin(0.9) ? 0 : g.i(1, 20);
    g.out("biased", v);
}
STRESS_GEN(yn_late) {
    int v = g.coin() ? 1 : 0;
    int sz = g.size(1, 10);
    g.tag(v ? "kind=yes" : "kind=no");
    g.out("yn_late", v, sz);
}
EOF
cat > guchoku.cpp << 'EOF'
#include <iostream>
#include <string>
int main() {
    std::string mode;
    int v;
    std::cin >> mode >> v;
    if (mode == "yn" || mode == "yn_late") {
        if (mode == "yn_late") { int sz; std::cin >> sz; }
        if (v) std::cout << "Yes\n1 2 3\n";
        else std::cout << "No\n";
    } else if (mode == "const") {
        std::cout << "No\n";
    } else {
        std::cout << v << "\n";
    }
}
EOF
cp guchoku.cpp main.cpp
cat > main_wa_late.cpp << 'EOF'
#include <iostream>
#include <string>
int main() {
    std::string mode;
    int v;
    std::cin >> mode >> v;
    if (mode == "yn_late") {
        int sz;
        std::cin >> sz;
        if (sz >= 2) { std::cout << "WRONG\n"; return 0; }
        if (v) std::cout << "Yes\n1 2 3\n";
        else std::cout << "No\n";
    } else {
        std::cout << v << "\n";
    }
}
EOF
cat > checker.cpp << 'EOF'
#include "stress/judge.hpp"
void judge(Judge& j) { (void)j; }
EOF
check stats_yn 0 st -g yn -n 40 --seed 1
log_has stats_yn_ans stats_yn '^答え: (Yes [0-9]+ \([0-9.]+%\), No [0-9]+ \([0-9.]+%\)|No [0-9]+ \([0-9.]+%\), Yes [0-9]+ \([0-9.]+%\))$'
log_has stats_yn_tag stats_yn '^タグ: .*kind='
check stats_yn_par 0 st -g yn -n 40 --seed 1 -j 4
a1="$(grep -E '^答え: ' "$build_dir/stats_yn.log")"
a4="$(grep -E '^答え: ' "$build_dir/stats_yn_par.log")"
t1="$(grep -E '^タグ: ' "$build_dir/stats_yn.log")"
t4="$(grep -E '^タグ: ' "$build_dir/stats_yn_par.log")"
if [ -n "$a1" ] && [ "$a1" = "$a4" ] && [ -n "$t1" ] && [ "$t1" = "$t4" ]; then echo "ok   stats_par_same"; else echo "FAIL stats_par_same ($a1 / $a4 | $t1 / $t4)"; fail=1; fi
check stats_all_same 0 st -g const_no -n 20 --seed 1
log_has stats_all_same_warn stats_all_same '^warning: 答え（先頭トークン）がすべて同じです: No \(20/20\)$'
log_has stats_all_same_tag  stats_all_same '^タグ: fixed 20$'
check stats_many 0 st -g many -n 40 --seed 1
log_lacks stats_many_quiet stats_many '答え（先頭トークン）|^答え:'
check stats_biased 0 st -g biased -n 100 --seed 1
log_has stats_biased_warn stats_biased '^warning: 答え（先頭トークン）が偏っています: 0 [0-9]+ \([0-9.]+%\), 他 [0-9]+ \([0-9.]+%\)$'
check stats_off 0 st -g yn -n 40 --seed 1 --no-stats
log_lacks stats_off_no_ans stats_off '答え'
log_lacks stats_off_no_tag stats_off '^タグ:'
check stats_checker_noref 0 st -c checker.cpp -g yn -n 20 --seed 1
log_lacks stats_checker_noref_no_ans stats_checker_noref '答え'
log_has   stats_checker_noref_tag    stats_checker_noref '^タグ: '
check stats_checker_ref 0 st -c checker.cpp --ref -g yn -n 20 --seed 1
log_has stats_checker_ref_ans stats_checker_ref '^答え: '
log_has stats_checker_ref_tag stats_checker_ref '^タグ: '
check stats_wa 1 st --main main_wa_late.cpp -g yn_late -n 40 --seed 1 --no-shrink --no-to-in
log_has stats_wa_ans stats_wa '^答え: '
log_has stats_wa_tag stats_wa '^タグ: '
log_has bench_tle_rss bench_tle '[0-9]+\.[0-9]MB'

# WA の位置の前後を表示（20 行目より後のときだけ diff ブロックを追加、st last でも再現）
work_df="$build_dir/diff"
rm -rf "$work_df" && mkdir -p "$work_df" && cd "$work_df"
cat > gen.cpp << 'EOF'
#include "stress/gen.hpp"
void gen(Gen& g) { g.out(30); (void)g; }
EOF
cat > guchoku.cpp << 'EOF'
#include <iostream>
int main() { int n; std::cin >> n; for (int i = 1; i <= n; ++i) std::cout << i << "\n"; }
EOF
cat > main_line5.cpp << 'EOF'
#include <iostream>
int main() { int n; std::cin >> n; for (int i = 1; i <= n; ++i) std::cout << (i == 5 ? 999 : i) << "\n"; }
EOF
cat > main_line25.cpp << 'EOF'
#include <iostream>
int main() { int n; std::cin >> n; for (int i = 1; i <= n; ++i) std::cout << (i == 25 ? 999 : i) << "\n"; }
EOF
cat > main_short25.cpp << 'EOF'
#include <iostream>
int main() { int n; std::cin >> n; for (int i = 1; i <= 25; ++i) std::cout << i << "\n"; }
EOF
cat > main_float25.cpp << 'EOF'
#include <iomanip>
#include <iostream>
int main() {
    int n; std::cin >> n;
    std::cout << std::fixed << std::setprecision(4);
    for (int i = 1; i <= n; ++i) std::cout << (i == 25 ? 25.05 : double(i)) << "\n";
}
EOF
check diff_line5 1 st --main main_line5.cpp -n 1 --seed 1 --no-shrink --no-to-in
log_lacks diff_line5_none diff_line5 '^---- diff:'
check diff_line25_token 1 st --main main_line25.cpp -n 1 --seed 1 --no-shrink --no-to-in
log_has diff_line25_token_hm  diff_line25_token '^---- diff: main \(line 25\) ----$'
log_has diff_line25_token_bm  diff_line25_token '^> 25: 999$'
log_has diff_line25_token_ctx diff_line25_token '^  22: 22$'
log_has diff_line25_token_hr  diff_line25_token '^---- diff: guchoku \(line 25\) ----$'
log_has diff_line25_token_br  diff_line25_token '^> 25: 25$'
check diff_line25_last 0 st last
log_has diff_line25_last_hm diff_line25_last '^---- diff: main \(line 25\) ----$'
log_has diff_line25_last_bm diff_line25_last '^> 25: 999$'
check diff_line25_exact 1 st --main main_line25.cpp --cmp exact -n 1 --seed 1 --no-shrink --no-to-in
log_has diff_line25_exact_hm diff_line25_exact '^---- diff: main \(line 25\) ----$'
log_has diff_line25_exact_bm diff_line25_exact '^> 25: 999$'
check diff_line25_float 1 st --main main_float25.cpp --cmp float:1e-3 -n 1 --seed 1 --no-shrink --no-to-in
log_has diff_line25_float_hm diff_line25_float '^---- diff: main \(line 25\) ----$'
log_has diff_line25_float_bm diff_line25_float '^> 25: 25\.0500$'
check diff_short25 1 st --main main_short25.cpp -n 1 --seed 1 --no-shrink --no-to-in
log_has diff_short25_eof diff_short25 '^> 26: \(EOF\)$'
log_has diff_short25_ref diff_short25 '^> 26: 26$'

# st bench の最大メモリ表示（MB）
work_rss="$build_dir/rss"
rm -rf "$work_rss" && mkdir -p "$work_rss" && cd "$work_rss"
cat > gen.cpp << 'EOF'
#include "stress/gen.hpp"
STRESS_BIG(big) { g.out(1); }
EOF
cat > main_small.cpp << 'EOF'
#include <iostream>
int main() { int x; std::cin >> x; std::cout << x << "\n"; }
EOF
cat > main_bigmem.cpp << 'EOF'
#include <iostream>
#include <vector>
int main() {
    int x;
    std::cin >> x;
    std::vector<char> buf(64 << 20, 1);  // 64 MB 確保して全ページに触る
    long long s = 0;
    for (size_t i = 0; i < buf.size(); i += 4096) s += buf[i];
    std::cout << (s + x) << "\n";
}
EOF
check bench_rss_small 0 st bench --main main_small.cpp -n 2 --seed 1
log_has bench_rss_small_row bench_rss_small 'big-01 .*[0-9]+ms +[0-9]+\.[0-9]MB'
log_has bench_rss_small_ok  bench_rss_small '^OK  max [0-9]+ms  max [0-9]+\.[0-9]MB'
check bench_rss_big 0 st bench --main main_bigmem.cpp -n 1 --seed 1
check bench_rss_compare 0 python3 - "$build_dir/bench_rss_small.log" "$build_dir/bench_rss_big.log" << 'PY'
import re, sys
from pathlib import Path
def max_mb(p):
    m = re.search(r"OK  max \d+ms  max (\d+\.\d+)MB", Path(p).read_text())
    assert m, Path(p).read_text()
    return float(m.group(1))
small = max_mb(sys.argv[1])
big = max_mb(sys.argv[2])
# Linux の wait4(ru_maxrss) は親プロセス（Python）の RSS で下限が付くため、64 MB 確保との差で確かめる
assert big >= 55.0 and big - small >= 25.0, (small, big)
PY

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
