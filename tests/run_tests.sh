#!/usr/bin/env bash
# gen.hpp の単体テストと、わざとバグらせた解でのドライバの動作確認。
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

g++ -std=gnu++20 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$root" \
    "$root/tests/gen_test.cpp" -o "$build_dir/gen_test" && check gen_test 0 "$build_dir/gen_test" || fail=1

work="$build_dir/sample"
rm -rf "$work" && cp -r "$root/tests/sample" "$work" && cd "$work"
st() { python3 "$root/stress.py" "$@"; }
check ok          0 st --main main_ok.cpp -n 300 --seed 1
check wa          1 st --main main_wa.cpp --seed 1
check re          1 st --main main_re.cpp --seed 2
check tle         1 st --main main_tle.cpp --seed 3 --tl 1
check checker_ok  0 st -c checker.cpp --main main_ok.cpp -n 300 --seed 5
check checker_wa  1 st -c checker.cpp --main main_wa.cpp --seed 5
check checker_ref 0 st -c checker.cpp --ref --main main_ok.cpp -n 100 --seed 5

# repro: 直前の失敗の seed:cap で同じ入力が出る
st --main main_wa.cpp --seed 7 >/dev/null 2>&1
case_id="$(python3 -c 'import json;m=json.load(open(".stress/last/meta.json"));print(str(m["seed"])+":"+str(m["cap"]))')"
cp .stress/last/in.txt "$build_dir/expected_in.txt"
check repro 1 st repro "$case_id" --main main_wa.cpp
if cmp -s .stress/last/in.txt "$build_dir/expected_in.txt"; then echo "ok   repro_same_input"; else echo "FAIL repro_same_input"; fail=1; fi
check last_to_in 0 st last --to-in
if cmp -s in.txt "$build_dir/expected_in.txt"; then echo "ok   to_in_copied"; else echo "FAIL to_in_copied"; fail=1; fi

# 名前付き生成器、make、bench
work2="$build_dir/multi"
rm -rf "$work2" && cp -r "$root/tests/multi" "$work2" && cd "$work2"
check multi_random_only 0 st --main main_same.cpp -g random -n 300 --seed 1
check multi_all_same    1 st --main main_same.cpp --seed 1
if grep -q '"gen": "all_same"' .stress/last/meta.json; then echo "ok   multi_gen_name_saved"; else echo "FAIL multi_gen_name_saved"; fail=1; fi
check multi_repro       1 st repro "$(python3 -c 'import json;print(json.load(open(".stress/last/meta.json"))["case"])')" --main main_same.cpp
check make              0 st make max all_same -n 2 --seed 3 -o cases
if [ "$(ls cases | wc -l)" -eq 4 ]; then echo "ok   make_files"; else echo "FAIL make_files"; fail=1; fi
check bench_ok          0 st bench --main main_ok.cpp -n 1 --seed 3
check bench_tle         1 st bench --main main_slow.cpp -n 1 --seed 3 --tl 0.5

[ "$fail" -eq 0 ] && echo "All tests passed" || { echo "Some tests failed"; exit 1; }
