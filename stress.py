#!/usr/bin/env python3
"""競プロ用ランダムテストのドライバ。

カレントディレクトリ（普段は ~/atcoder）の main.cpp / guchoku.cpp / gen.cpp を
ビルドして、gen の出力で main と guchoku を比べる。-c で checker モード。

  st                      # 1000 回
  st -n 5000 --seed 42
  st -t 60                # 60 秒
  st -c checker.cpp       # スペシャルジャッジ
  st -g max -g all_same  # STRESS_GEN の生成器を絞る
  st repro 12345:7:gen    # 失敗ケースを 1 つ再現（seed:cap:生成器）
  st make max -n 5 -o cases/   # 入力ファイルだけ書き出す
  st bench max            # 最大ケースで main の実行時間を測る
  st last [--to-in]       # 直前の失敗を表示（in.txt へコピー）
  st init                 # gen.cpp などの雛形を置く
  st clean                # 保存した失敗ケースを消す

終了コード: 0 = 全部 OK, 1 = 失敗を見つけた, 2 = ビルドや引数のエラー
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import math
import os
import shutil
import signal
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

TOOL_ROOT = Path(__file__).resolve().parent
MASK64 = (1 << 64) - 1

EXIT_OK, EXIT_FOUND, EXIT_ERROR = 0, 1, 2


# ---------------------------------------------------------------- 表示


class Out:
    color = sys.stderr.isatty()
    tty = sys.stderr.isatty()

    @staticmethod
    def c(text: str, code: str) -> str:
        return f"\033[{code}m{text}\033[0m" if Out.color else text

    @staticmethod
    def red(t: str) -> str:
        return Out.c(t, "1;31")

    @staticmethod
    def green(t: str) -> str:
        return Out.c(t, "1;32")

    @staticmethod
    def yellow(t: str) -> str:
        return Out.c(t, "1;33")

    @staticmethod
    def dim(t: str) -> str:
        return Out.c(t, "2")

    _status_len = 0

    @staticmethod
    def status(text: str) -> None:
        if not Out.tty:
            return
        sys.stderr.write("\r" + text + " " * max(0, Out._status_len - len(text)))
        sys.stderr.flush()
        Out._status_len = len(text)

    @staticmethod
    def clear_status() -> None:
        if Out.tty and Out._status_len:
            sys.stderr.write("\r" + " " * Out._status_len + "\r")
            sys.stderr.flush()
            Out._status_len = 0

    @staticmethod
    def log(text: str = "") -> None:
        Out.clear_status()
        print(text, file=sys.stderr)


def die(msg: str) -> "NoReturn":  # noqa: F821
    Out.log(Out.red("error: ") + msg)
    sys.exit(EXIT_ERROR)


# ---------------------------------------------------------------- 乱数


def splitmix64(x: int) -> int:
    z = (x + 0x9E3779B97F4A7C15) & MASK64
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK64
    return z ^ (z >> 31)


def case_seed(base: int, index: int) -> int:
    return splitmix64((base + index) & MASK64)


def cap_for(index: int, max_cap: int | None) -> int:
    """小さいサイズから試す。10 ケースごとに cap を 1 増やす。"""
    cap = 1 + index // 10
    return min(cap, max_cap) if max_cap else cap


# ---------------------------------------------------------------- ビルド


@dataclass
class BuildConfig:
    workdir: Path
    sanitize: bool = False
    extra: list[str] = field(default_factory=list)

    def include_dirs(self) -> list[Path]:
        dirs = [TOOL_ROOT]
        for acl in (self.workdir / "ac-library", Path.home() / "atcoder" / "ac-library"):
            if acl.is_dir():
                dirs.append(acl)
                break
        lib = Path(os.environ.get("CP_LIBRARY_DIR", Path.home() / "cp-library"))
        if lib.is_dir():
            dirs.append(lib)
        return dirs

    def flags(self, src: Path, use_pch: bool) -> list[str]:
        f = ["-std=gnu++20", "-O2", "-DLOCAL"]
        f += [f"-I{d}" for d in self.include_dirs()]
        if self.sanitize:
            f += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g", "-D_GLIBCXX_DEBUG"]
        elif use_pch and self._pch_usable(src):
            # build.sh と同じ PCH を使う（フラグが合わなければ g++ が黙って無視する）
            f += ["-include", "pch_acl.hpp"]
        return f + self.extra

    def _pch_usable(self, src: Path) -> bool:
        if not (self.workdir / "pch_acl.hpp.gch").exists():
            return False
        try:
            text = src.read_text(errors="replace")
        except OSError:
            return False
        return any(line.lstrip().startswith("#include") and "<atcoder/" in line for line in text.splitlines())


def build(cfg: BuildConfig, src: Path, name: str, use_pch: bool = True) -> Path:
    """必要なときだけビルドする。依存は g++ -MMD の .d ファイルで追う。"""
    bindir = cfg.workdir / ".stress" / "bin"
    bindir.mkdir(parents=True, exist_ok=True)
    flags = cfg.flags(src, use_pch)
    tag = hashlib.sha1((str(src.resolve()) + "\0" + " ".join(flags)).encode()).hexdigest()[:8]
    exe = bindir / f"{name}-{tag}"
    dep = exe.with_suffix(".d")
    if exe.exists() and dep.exists() and not _stale(exe, dep):
        return exe
    cmd = ["g++", *flags, str(src), "-o", str(exe), "-MMD", "-MF", str(dep)]
    proc = subprocess.run(cmd, cwd=cfg.workdir, capture_output=True, text=True)
    if proc.returncode != 0:
        Out.log(Out.red(f"build failed: {src.name}"))
        Out.log(proc.stderr.rstrip())
        raise BuildError(src.name)
    return exe


def _stale(exe: Path, dep: Path) -> bool:
    built = exe.stat().st_mtime
    text = dep.read_text().replace("\\\n", " ")
    _, _, deps = text.partition(":")
    for d in deps.split():
        p = Path(d)
        if not p.is_absolute():
            p = exe.parent.parent.parent / p
        try:
            if p.stat().st_mtime > built:
                return True
        except FileNotFoundError:
            return True
    return False


class BuildError(Exception):
    pass


# ---------------------------------------------------------------- 実行


@dataclass
class RunResult:
    status: str  # OK / RE / TLE
    code: int
    elapsed: float


def run(exe: Path | list[str], stdin: Path | None, stdout: Path, stderr: Path, tl: float,
        cwd: Path, args: list[str] | None = None) -> RunResult:
    cmd = [str(exe)] if isinstance(exe, Path) else list(exe)
    cmd += args or []
    start = time.perf_counter()
    with open(stdout, "wb") as fo, open(stderr, "wb") as fe:
        fi = open(stdin, "rb") if stdin else subprocess.DEVNULL
        try:
            proc = subprocess.Popen(cmd, stdin=fi, stdout=fo, stderr=fe, cwd=cwd, start_new_session=True)
            try:
                code = proc.wait(timeout=tl)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
                return RunResult("TLE", -9, time.perf_counter() - start)
        finally:
            if stdin:
                fi.close()
    elapsed = time.perf_counter() - start
    return RunResult("OK" if code == 0 else "RE", code, elapsed)


def describe_code(code: int) -> str:
    if code < 0:
        try:
            return f"signal {signal.Signals(-code).name}"
        except ValueError:
            return f"signal {-code}"
    return f"exit {code}"


# ---------------------------------------------------------------- 比較


def compare(a: bytes, b: bytes, mode: str) -> tuple[bool, str]:
    if mode == "exact":
        if a == b:
            return True, ""
        n = next((k for k in range(min(len(a), len(b))) if a[k] != b[k]), min(len(a), len(b)))
        return False, f"first difference at byte {n}"
    ta, tb = a.split(), b.split()
    eps = None
    if mode.startswith("float"):
        _, _, e = mode.partition(":")
        eps = float(e) if e else 1e-6
    for k, (x, y) in enumerate(zip(ta, tb)):
        if x == y:
            continue
        if eps is not None:
            try:
                fx, fy = float(x), float(y)
                if math.isfinite(fx) and math.isfinite(fy) and abs(fx - fy) <= eps * max(1.0, abs(fy)):
                    continue
            except ValueError:
                pass
        return False, f"token {k + 1}: main={x.decode(errors='replace')!r} ref={y.decode(errors='replace')!r}"
    if len(ta) != len(tb):
        return False, f"token count differs: main={len(ta)} ref={len(tb)}"
    return True, ""


# ---------------------------------------------------------------- 1 ケース


@dataclass
class Verdict:
    kind: str          # OK / WA / RE / TLE / REF_RE / REF_TLE / CHECKER_FAIL / GEN_FAIL
    detail: str = ""
    elapsed: float = 0.0


@dataclass
class Programs:
    gen: Path
    main: Path | None
    ref: Path | None
    checker: Path | None
    gens: list[str] = field(default_factory=list)  # 生成器の名前
    big: set[str] = field(default_factory=set)     # STRESS_BIG の生成器


@dataclass
class Case:
    seed: int
    cap: int | None  # None = 上限なし
    name: str

    @property
    def id(self) -> str:
        return f"{self.seed}:{'-' if self.cap is None else self.cap}:{self.name}"

    def gen_args(self) -> list[str]:
        return [str(self.seed), "-" if self.cap is None else str(self.cap), self.name]

    @staticmethod
    def parse(text: str, gens: list[str]) -> "Case":
        parts = text.split(":")
        try:
            seed = int(parts[0])
            cap = None if len(parts) < 2 or parts[1] in ("", "-") else int(parts[1])
        except ValueError:
            die(f"invalid case: {text} (expected SEED:CAP[:NAME])")
        name = parts[2] if len(parts) >= 3 and parts[2] else gens[0]
        if name not in gens:
            die(f"unknown generator: {name} (available: {', '.join(gens)})")
        return Case(seed, cap, name)


def generate(progs: Programs, case: Case, d: Path, opt: argparse.Namespace) -> Verdict | None:
    """d/in.txt に入力を作る。失敗したら Verdict を返す。"""
    g = run(progs.gen, None, d / "in.txt", d / "stderr_gen.txt", opt.gen_tl, d, case.gen_args())
    if g.status != "OK":
        msg = (d / "stderr_gen.txt").read_text(errors="replace").strip()
        return Verdict("GEN_FAIL", f"[{case.name}] {g.status} ({describe_code(g.code)}) {msg}")
    return None


def fresh_dir(d: Path) -> Path:
    if d.exists():
        shutil.rmtree(d)  # 前のケースの出力が残らないように
    d.mkdir(parents=True)
    return d


def run_case(progs: Programs, case: Case, d: Path, opt: argparse.Namespace) -> Verdict:
    fresh_dir(d)
    inp = d / "in.txt"
    if (bad := generate(progs, case, d, opt)) is not None:
        return bad

    m = run(progs.main, inp, d / "out_main.txt", d / "stderr_main.txt", opt.tl, d)
    if m.status == "TLE":
        return Verdict("TLE", f"main exceeded {opt.tl}s", m.elapsed)
    if m.status == "RE":
        return Verdict("RE", f"main: {describe_code(m.code)}", m.elapsed)

    if progs.ref:
        r = run(progs.ref, inp, d / "out_ref.txt", d / "stderr_ref.txt", opt.ref_tl, d)
        if r.status == "TLE":
            return Verdict("REF_TLE", f"guchoku exceeded {opt.ref_tl}s", m.elapsed)
        if r.status == "RE":
            return Verdict("REF_RE", f"guchoku: {describe_code(r.code)}", m.elapsed)

    if progs.checker:
        args = [str(inp), str(d / "out_main.txt")]
        if progs.ref:
            args.append(str(d / "out_ref.txt"))
        c = run(progs.checker, None, d / "stdout_checker.txt", d / "stderr_checker.txt", opt.checker_tl, d, args)
        msg = (d / "stderr_checker.txt").read_text(errors="replace").strip()
        if c.status == "OK":
            return Verdict("OK", "", m.elapsed)
        if c.status == "TLE" or c.code == 3:
            return Verdict("CHECKER_FAIL", f"{c.status} ({describe_code(c.code)}) {msg}", m.elapsed)
        if msg.startswith("WA: "):
            msg = msg[4:]
        return Verdict("WA", msg or f"checker {describe_code(c.code)}", m.elapsed)

    ok, why = compare((d / "out_main.txt").read_bytes(), (d / "out_ref.txt").read_bytes(), opt.cmp)
    return Verdict("OK" if ok else "WA", why, m.elapsed)


# ---------------------------------------------------------------- 失敗の保存と表示


def save_failure(workdir: Path, run_dir: Path, case: Case, v: Verdict, opt: argparse.Namespace,
                 sources: dict[str, Path]) -> Path:
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    dest = workdir / ".stress" / "fail" / f"{stamp}-{v.kind}-{case.name}-{case.seed:016x}"
    k = 1
    while dest.exists():
        dest = dest.with_name(f"{dest.name.rsplit('~', 1)[0]}~{k}")
        k += 1
    shutil.copytree(run_dir, dest)
    meta = {
        "kind": v.kind,
        "detail": v.detail,
        "seed": case.seed,
        "cap": case.cap,
        "gen": case.name,
        "case": case.id,
        "repro": f"st repro {case.id}" + (f" -c {opt.checker}" if opt.checker else ""),
        "mode": "checker" if opt.checker else "compare",
        "cmp": opt.cmp,
        "tl": opt.tl,
        "sources": {k: {"path": str(p), "sha1": hashlib.sha1(p.read_bytes()).hexdigest()} for k, p in sources.items()},
        "time": dt.datetime.now().isoformat(timespec="seconds"),
    }
    (dest / "meta.json").write_text(json.dumps(meta, indent=2, ensure_ascii=False) + "\n")
    last = workdir / ".stress" / "last"
    if last.is_symlink() or last.exists():
        last.unlink()
    last.symlink_to(dest.relative_to(last.parent))
    return dest


def head(path: Path, lines: int = 20, width: int = 200) -> str:
    if not path.exists():
        return Out.dim("(none)")
    text = path.read_text(errors="replace").splitlines()
    shown = [ln if len(ln) <= width else ln[:width] + Out.dim(" ...") for ln in text[:lines]]
    if len(text) > lines:
        shown.append(Out.dim(f"... ({len(text) - lines} more lines)"))
    return "\n".join(shown) if shown else Out.dim("(empty)")


def show_failure(d: Path, v: Verdict, case_id: str, repro: str, workdir: Path) -> None:
    Out.log()
    Out.log(Out.red(f"{v.kind}") + f"  {v.detail}")
    Out.log(Out.dim(f"case={case_id}  ->  {repro}"))
    Out.log(Out.yellow("---- input ----"))
    Out.log(head(d / "in.txt"))
    Out.log(Out.yellow("---- main ----"))
    Out.log(head(d / "out_main.txt"))
    if (d / "out_ref.txt").exists():
        Out.log(Out.yellow("---- guchoku ----"))
        Out.log(head(d / "out_ref.txt"))
    for name in ("stderr_main.txt", "stderr_ref.txt", "stderr_checker.txt"):
        p = d / name
        if p.exists() and p.stat().st_size:
            Out.log(Out.yellow(f"---- {name} ----"))
            Out.log(head(p, 15))
    try:
        rel = d.relative_to(workdir)
    except ValueError:
        rel = d
    Out.log(Out.dim(f"saved: {rel}   (st last --to-in で in.txt にコピー)"))


# ---------------------------------------------------------------- 最小化（サイズ縮小）


def shrink(progs: Programs, case: Case, run_dir: Path, opt: argparse.Namespace,
           deadline: float) -> tuple[Case, Verdict] | None:
    """同じ生成器で cap を小さくして、失敗する入力を探し直す。"""
    cap = case.cap or 1
    caps = sorted({c for c in [1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 25, 32, 40, 50, 64, 80, 100] if c < cap}
                  | ({cap // 2, cap * 3 // 4} - {0, cap}))
    base = splitmix64(case.seed ^ 0x5EED)
    for c in caps:
        for t in range(opt.shrink_tries):
            if time.monotonic() > deadline:
                return None
            cand = Case(case_seed(base, c * 1_000_003 + t), c, case.name)
            Out.status(f"shrinking: cap={c} try {t + 1}/{opt.shrink_tries}")
            v = run_case(progs, cand, run_dir, opt)
            if v.kind != "OK":
                return cand, v
    return None


# ---------------------------------------------------------------- ビルドの準備


def find_source(workdir: Path, name: str, what: str) -> Path:
    p = (workdir / name)
    if not p.exists():
        hint = "  (st init で雛形を置けます)" if what == "generator" else ""
        die(f"{what} not found: {p}{hint}")
    return p


def prepare(opt: argparse.Namespace, workdir: Path, need: set[str]) -> tuple[Programs, dict[str, Path]]:
    cfg = BuildConfig(workdir, sanitize=getattr(opt, "san", False),
                      extra=opt.cxxflags.split() if getattr(opt, "cxxflags", "") else [])
    sources = {"gen": find_source(workdir, opt.gen_file, "generator")}
    if "main" in need:
        sources["main"] = find_source(workdir, opt.main, "solution")
    if "judge" in need:
        if opt.checker:
            sources["checker"] = find_source(workdir, opt.checker, "checker")
            if opt.ref:
                sources["ref"] = find_source(workdir, opt.guchoku, "reference (guchoku)")
        else:
            sources["ref"] = find_source(workdir, opt.guchoku, "reference (guchoku)")

    Out.status("building " + ", ".join(p.name for p in sources.values()) + " ...")
    t0 = time.perf_counter()
    try:
        with ThreadPoolExecutor(max_workers=len(sources)) as ex:
            # gen / checker は ACL を使わないことが多いので PCH なし
            jobs = {key: ex.submit(build, cfg, src, key, key in ("main", "ref")) for key, src in sources.items()}
            exes = {k: f.result() for k, f in jobs.items()}
    except BuildError:
        sys.exit(EXIT_ERROR)
    Out.clear_status()
    Out.log(Out.dim(f"build ok ({time.perf_counter() - t0:.1f}s)"))

    listed = subprocess.run([str(exes["gen"]), "--list"], capture_output=True, text=True, cwd=workdir)
    if listed.returncode != 0:
        Out.log(listed.stderr.rstrip())
        die(f"{opt.gen_file}: could not list generators")
    rows = [line.split() for line in listed.stdout.splitlines() if line.strip()]
    progs = Programs(gen=exes["gen"], main=exes.get("main"), ref=exes.get("ref"),
                     checker=exes.get("checker"), gens=[r[0] for r in rows],
                     big={r[0] for r in rows if "big" in r[1:]})
    return progs, sources


def select_gens(progs: Programs, wanted: list[str] | None, big: bool) -> list[str]:
    """省略時: st では STRESS_BIG 以外、make / bench では STRESS_BIG（無ければ全部）。"""
    if not wanted:
        normal = [n for n in progs.gens if n not in progs.big]
        bigs = [n for n in progs.gens if n in progs.big]
        if big:
            return bigs or progs.gens
        if not normal:
            die("st で使える生成器がない（STRESS_BIG 以外の生成器を書くか、-g で指定する）")
        return normal
    names = [n for w in wanted for n in w.split(",") if n]
    unknown = [n for n in names if n not in progs.gens]
    if unknown:
        die(f"unknown generator: {', '.join(unknown)} (available: {', '.join(progs.gens)})")
    return names


# ---------------------------------------------------------------- コマンド


def cmd_run(opt: argparse.Namespace) -> int:
    workdir = Path.cwd()
    progs, sources = prepare(opt, workdir, {"main", "judge"})
    gens = select_gens(progs, opt.gens, big=False)
    run_dir = workdir / ".stress" / "run"
    base = opt.seed if opt.seed is not None else int.from_bytes(os.urandom(8), "little")
    start = time.monotonic()
    deadline = start + opt.time if opt.time else math.inf
    total = opt.iters if not opt.time else None
    mode = "checker" if opt.checker else f"compare/{opt.cmp}"
    Out.log(Out.dim(f"mode={mode} gens={','.join(gens)} base_seed={base} tl={opt.tl}s"))

    slowest = 0.0
    k = 0
    last_status = 0.0
    while (total is None or k < total) and time.monotonic() < deadline:
        # 生成器は順番に回し、cap は生成器ごとに小さい方から増やす
        case = Case(case_seed(base, k), cap_for(k // len(gens), opt.max_cap), gens[k % len(gens)])
        v = run_case(progs, case, run_dir, opt)
        slowest = max(slowest, v.elapsed)
        k += 1
        now = time.monotonic()
        if now - last_status > 0.1:
            prog = f"{k}/{total}" if total else f"{k}"
            Out.status(f"[{prog}] cap={case.cap} OK  max {slowest * 1000:.0f}ms  {now - start:.1f}s")
            last_status = now
        if v.kind == "OK":
            continue

        if v.kind in ("GEN_FAIL", "CHECKER_FAIL"):
            Out.log(Out.red(v.kind) + f"  {v.detail}")
            Out.log(Out.dim(f"case={case.id}"))
            return EXIT_ERROR

        if opt.shrink and (case.cap or 1) > 1:
            snapshot = workdir / ".stress" / "run_found"
            if snapshot.exists():
                shutil.rmtree(snapshot)
            shutil.copytree(run_dir, snapshot)
            found = shrink(progs, case, run_dir, opt, time.monotonic() + opt.shrink_time)
            Out.clear_status()
            if found:
                Out.log(Out.dim(f"shrunk: cap {case.cap} -> {found[0].cap}"))
                case, v = found
            else:
                shutil.rmtree(run_dir)
                shutil.copytree(snapshot, run_dir)
            shutil.rmtree(snapshot, ignore_errors=True)

        dest = save_failure(workdir, run_dir, case, v, opt, sources)
        meta = json.loads((dest / "meta.json").read_text())
        show_failure(dest, v, case.id, meta["repro"], workdir)
        Out.log(Out.dim(f"found after {k} cases ({time.monotonic() - start:.1f}s)"))
        return EXIT_FOUND

    Out.log(Out.green("OK") + f"  {k} cases  max {slowest * 1000:.0f}ms  {time.monotonic() - start:.1f}s"
            + Out.dim(f"  base_seed={base}"))
    return EXIT_OK


def cmd_repro(opt: argparse.Namespace) -> int:
    workdir = Path.cwd()
    progs, sources = prepare(opt, workdir, {"main", "judge"})
    case = Case.parse(opt.case, progs.gens)
    run_dir = workdir / ".stress" / "run"
    v = run_case(progs, case, run_dir, opt)
    if v.kind == "OK":
        Out.log(Out.green("OK") + f"  case={case.id} (再現しなかった)")
        Out.log(Out.dim(f"input: {(run_dir / 'in.txt').relative_to(workdir)}"))
        return EXIT_OK
    dest = save_failure(workdir, run_dir, case, v, opt, sources)
    meta = json.loads((dest / "meta.json").read_text())
    show_failure(dest, v, case.id, meta["repro"], workdir)
    return EXIT_FOUND


def make_cases(opt: argparse.Namespace, progs: Programs) -> list[Case]:
    gens = select_gens(progs, opt.names, big=True)
    base = opt.seed if opt.seed is not None else int.from_bytes(os.urandom(8), "little")
    return [Case(case_seed(base, gi * 1_000_003 + k), opt.cap, name)
            for gi, name in enumerate(gens) for k in range(opt.count)]


def cmd_make(opt: argparse.Namespace) -> int:
    workdir = Path.cwd()
    progs, _ = prepare(opt, workdir, set())
    outdir = workdir / opt.out
    outdir.mkdir(parents=True, exist_ok=True)
    tmp = workdir / ".stress" / "make"
    for k, case in enumerate(make_cases(opt, progs)):
        fresh_dir(tmp)
        if (bad := generate(progs, case, tmp, opt)) is not None:
            die(bad.detail)
        idx = k % opt.count + 1
        dst = outdir / f"{case.name}-{idx:02d}.txt"
        shutil.copy2(tmp / "in.txt", dst)
        size = dst.stat().st_size
        Out.log(f"{dst.relative_to(workdir)}  {size / 1024:.1f}KB  " + Out.dim(f"case={case.id}"))
    shutil.rmtree(tmp, ignore_errors=True)
    return EXIT_OK


def cmd_bench(opt: argparse.Namespace) -> int:
    workdir = Path.cwd()
    progs, _ = prepare(opt, workdir, {"main"})
    benchdir = workdir / ".stress" / "bench"
    if benchdir.exists():
        shutil.rmtree(benchdir)
    benchdir.mkdir(parents=True)
    tmp = workdir / ".stress" / "run"
    worst = 0.0
    bad_count = 0
    for k, case in enumerate(make_cases(opt, progs)):
        fresh_dir(tmp)
        if (bad := generate(progs, case, tmp, opt)) is not None:
            die(bad.detail)
        label = f"{case.name}-{k % opt.count + 1:02d}"
        shutil.copy2(tmp / "in.txt", benchdir / f"{label}.txt")
        Out.status(f"running {label} ...")
        r = run(progs.main, tmp / "in.txt", tmp / "out_main.txt", tmp / "stderr_main.txt", opt.tl, tmp)
        kb = (tmp / "in.txt").stat().st_size / 1024
        if r.status == "OK":
            worst = max(worst, r.elapsed)
            ms = r.elapsed * 1000
            mark = Out.yellow(f"{ms:7.0f}ms") if r.elapsed > opt.tl * 0.8 else f"{ms:7.0f}ms"
            Out.log(f"{label:<20} {mark}  " + Out.dim(f"input {kb:.0f}KB  case={case.id}"))
        else:
            bad_count += 1
            what = f"TLE (> {opt.tl}s)" if r.status == "TLE" else f"RE ({describe_code(r.code)})"
            Out.log(f"{label:<20} {Out.red(what)}  " + Out.dim(f"input {kb:.0f}KB  case={case.id}"))
    Out.log(Out.dim(f"inputs: {benchdir.relative_to(workdir)}/"))
    if bad_count:
        Out.log(Out.red(f"{bad_count} case(s) failed") + f"  (TL {opt.tl}s)")
        return EXIT_FOUND
    Out.log(Out.green("OK") + f"  max {worst * 1000:.0f}ms  (TL {opt.tl}s)")
    return EXIT_OK


def cmd_last(opt: argparse.Namespace) -> int:
    workdir = Path.cwd()
    last = workdir / ".stress" / "last"
    if not last.exists():
        die("no saved failure in .stress/")
    meta = json.loads((last / "meta.json").read_text())
    v = Verdict(meta["kind"], meta.get("detail", ""))
    case_id = meta.get("case", f"{meta['seed']}:{meta['cap']}")
    show_failure(last.resolve(), v, case_id, meta.get("repro", f"st repro {case_id}"), workdir)
    if opt.to_in:
        target = workdir / "in.txt"
        if target.exists() and target.stat().st_size:
            backup = workdir / ".stress" / "in.txt.bak"
            shutil.copy2(target, backup)
            Out.log(Out.dim(f"backed up old in.txt -> {backup.relative_to(workdir)}"))
        shutil.copy2(last / "in.txt", target)
        Out.log(Out.green("copied") + " failing input -> in.txt")
    return EXIT_OK


def cmd_clean(opt: argparse.Namespace) -> int:
    d = Path.cwd() / ".stress"
    for name in ("fail", "run", "last", "bench", "make"):
        p = d / name
        if p.is_symlink():
            p.unlink()
        elif p.exists():
            shutil.rmtree(p)
    if opt.all and (d / "bin").exists():
        shutil.rmtree(d / "bin")
    Out.log("cleaned .stress/" + (" (including binaries)" if opt.all else ""))
    return EXIT_OK


def cmd_init(opt: argparse.Namespace) -> int:
    workdir = Path.cwd()
    names = ["gen.cpp"] + (["checker.cpp"] if opt.checker else [])
    for name in names:
        src = TOOL_ROOT / "templates" / name
        dst = workdir / (opt.name if opt.name and len(names) == 1 else name)
        if dst.exists() and not opt.force:
            Out.log(Out.yellow("skip") + f" {dst.name} already exists (--force で上書き)")
            continue
        shutil.copy2(src, dst)
        Out.log(Out.green("created") + f" {dst.name}")
    return EXIT_OK


# ---------------------------------------------------------------- 引数


def add_build(p: argparse.ArgumentParser) -> None:
    p.add_argument("--gen-file", default="gen.cpp", help="ジェネレータのソース (default: gen.cpp)")
    p.add_argument("--san", action="store_true", help="AddressSanitizer / UBSan / _GLIBCXX_DEBUG でビルド")
    p.add_argument("--cxxflags", default=os.environ.get("STRESS_CXXFLAGS", ""), help="追加のコンパイルフラグ")
    p.add_argument("--gen-tl", type=float, default=10.0, help=argparse.SUPPRESS)


def add_common(p: argparse.ArgumentParser) -> None:
    add_build(p)
    p.add_argument("--main", default="main.cpp", help="解答 (default: main.cpp)")
    p.add_argument("--guchoku", default="guchoku.cpp", help="愚直解 (default: guchoku.cpp)")
    p.add_argument("-c", "--checker", metavar="SRC", help="checker モード（例: -c checker.cpp）")
    p.add_argument("--ref", action="store_true", help="checker モードでも愚直解を走らせ、出力を第 3 引数で渡す")
    p.add_argument("--tl", type=float, default=2.0, help="main の制限時間 [s] (default: 2)")
    p.add_argument("--ref-tl", type=float, default=10.0, help="愚直解の制限時間 [s] (default: 10)")
    p.add_argument("--checker-tl", type=float, default=10.0, help=argparse.SUPPRESS)
    p.add_argument("--cmp", default="token", help="比較方法: token (default) / exact / float[:EPS]")


def add_cases(p: argparse.ArgumentParser, count: int) -> None:
    p.add_argument("names", nargs="*", help="生成器の名前（省略時は STRESS_BIG のもの、無ければ全部）")
    p.add_argument("-n", "--count", type=int, default=count, help=f"生成器ごとの個数 (default: {count})")
    p.add_argument("--seed", type=int, help="基準 seed（省略時はランダム）")
    p.add_argument("--cap", type=int, help="g.size() の上限（省略時は上限なし）")


def parse(argv: list[str]) -> argparse.Namespace:
    commands = {"run", "repro", "make", "bench", "last", "clean", "init"}
    if not argv or argv[0] not in commands:
        argv = ["run", *argv]

    ap = argparse.ArgumentParser(prog="st", description="ランダムテストのドライバ")
    sub = ap.add_subparsers(dest="command", required=True)

    p = sub.add_parser("run", help="ランダムテストを回す（既定）")
    add_common(p)
    p.add_argument("-g", "--gen", dest="gens", action="append", metavar="NAME",
                   help="使う生成器（複数可、カンマ区切りも可。省略時は STRESS_BIG 以外を順番に）")
    p.add_argument("-n", "--iters", type=int, default=1000, help="回数 (default: 1000)")
    p.add_argument("-t", "--time", type=float, help="回数の代わりに秒数で回す")
    p.add_argument("--seed", type=int, help="基準 seed（省略時はランダム）")
    p.add_argument("--max-cap", type=int, help="g.size() に渡す cap の上限")
    p.add_argument("--no-shrink", dest="shrink", action="store_false", help="失敗後の縮小をしない")
    p.add_argument("--shrink-tries", type=int, default=200, help=argparse.SUPPRESS)
    p.add_argument("--shrink-time", type=float, default=20.0, help=argparse.SUPPRESS)

    p = sub.add_parser("repro", help="SEED:CAP:NAME のケースを 1 つ再現する")
    add_common(p)
    p.add_argument("case", help="SEED:CAP:NAME（失敗時に表示されるもの）")

    p = sub.add_parser("make", help="入力ファイルだけ書き出す")
    add_build(p)
    add_cases(p, 5)
    p.add_argument("-o", "--out", default="cases", help="出力先 (default: cases/)")

    p = sub.add_parser("bench", help="生成した入力で main の実行時間を測る")
    add_build(p)
    add_cases(p, 3)
    p.add_argument("--main", default="main.cpp", help="解答 (default: main.cpp)")
    p.add_argument("--tl", type=float, default=2.0, help="制限時間 [s] (default: 2)")

    p = sub.add_parser("last", help="直前の失敗を表示する")
    p.add_argument("--to-in", action="store_true", help="失敗した入力を in.txt にコピーする")

    p = sub.add_parser("clean", help=".stress/ の失敗ケースを消す")
    p.add_argument("--all", action="store_true", help="ビルド済みバイナリも消す")

    p = sub.add_parser("init", help="gen.cpp（と checker.cpp）の雛形を置く")
    p.add_argument("-c", "--checker", action="store_true", help="checker.cpp の雛形も置く")
    p.add_argument("--force", action="store_true")
    p.add_argument("name", nargs="?", help=argparse.SUPPRESS)

    return ap.parse_args(argv)


def main(argv: list[str]) -> int:
    opt = parse(argv)
    handler = {"run": cmd_run, "repro": cmd_repro, "make": cmd_make, "bench": cmd_bench,
               "last": cmd_last, "clean": cmd_clean, "init": cmd_init}
    try:
        return handler[opt.command](opt)
    except KeyboardInterrupt:
        Out.log(Out.yellow("interrupted"))
        return 130


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
