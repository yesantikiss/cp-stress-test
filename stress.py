#!/usr/bin/env python3
"""競プロ用ランダムテストのドライバ。

カレントディレクトリの main.cpp / guchoku.cpp / gen.cpp を
ビルドして、gen の出力で main と guchoku を比べる。-c で checker モード、-i でインタラクティブ。

  st                      # 1000 回
  st -n 5000 --seed 42
  st -t 60                # 60 秒
  st -c checker.cpp       # スペシャルジャッジ
  st -i interactor.cpp    # インタラクティブ
  st -g max -g all_same  # STRESS_GEN の生成器を絞る
  st repro 12345:7:gen    # 失敗ケースを 1 つ再現（seed:cap:生成器）
  st make max -n 5 -o cases/   # 入力ファイルだけ書き出す
  st bench max            # 最大ケースで main の実行時間を測る
  st last [--to-in]       # 直前の失敗を表示（in.txt へコピー）
  st restore              # 退避した手書きの in.txt を戻す
  st init [-c] [-i]       # gen.cpp などの雛形を置く
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
import threading
import time
from collections import deque
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
        acl = Path(os.environ["ACL_DIR"]).expanduser() if os.environ.get("ACL_DIR") else self.workdir / "ac-library"
        if acl.is_dir():
            dirs.append(acl)
        lib = Path(os.environ.get("CP_LIBRARY_DIR", Path.home() / "cp-library")).expanduser()
        if lib.is_dir():
            dirs.append(lib)
        return dirs

    def flags(self, src: Path, use_pch: bool) -> list[str]:
        f = ["-std=gnu++20", "-O2", "-DLOCAL"]
        f += [f"-I{d}" for d in self.include_dirs()]
        if self.sanitize:
            f += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g", "-D_GLIBCXX_DEBUG"]
        elif use_pch and self._pch_usable(src):
            # 問題のディレクトリの PCH を使う（フラグが合わなければ g++ が黙って無視する）
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
        cwd: Path, args: list[str] | None = None, env: dict[str, str] | None = None) -> RunResult:
    cmd = [str(exe)] if isinstance(exe, Path) else list(exe)
    cmd += args or []
    start = time.perf_counter()
    with open(stdout, "wb") as fo, open(stderr, "wb") as fe:
        fi = open(stdin, "rb") if stdin else subprocess.DEVNULL
        try:
            proc = subprocess.Popen(cmd, stdin=fi, stdout=fo, stderr=fe, cwd=cwd, start_new_session=True,
                                    env={**os.environ, **env} if env else None)
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
    kind: str          # OK / WA / RE / TLE / REF_RE / REF_TLE / CHECKER_FAIL / INTERACTOR_FAIL / GEN_FAIL
    detail: str = ""
    elapsed: float = 0.0
    queries: tuple[int, int] | None = None  # インタラクティブ: (使った回数, 制限 or -1)


@dataclass
class Programs:
    gen: Path
    main: Path | None
    ref: Path | None
    checker: Path | None
    interactor: Path | None = None
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
    g = run(progs.gen, None, d / "in.txt", d / "stderr_gen.txt", opt.gen_tl, d, case.gen_args(),
            env={"STRESS_TAGS": str(d / "tags.txt")})
    if g.status != "OK":
        msg = (d / "stderr_gen.txt").read_text(errors="replace").strip()
        return Verdict("GEN_FAIL", f"[{case.name}] {g.status} ({describe_code(g.code)}) {msg}")
    return None


def read_tags(d: Path) -> list[str]:
    """gen が書いた tags.txt（"タグ<TAB>回数"）を "str=palindrome x3" のような文字列の列にする。"""
    p = d / "tags.txt"
    if not p.exists():
        return []
    tags = []
    for line in p.read_text(errors="replace").splitlines():
        name, _, count = line.rpartition("\t")
        if not name:
            continue
        tags.append(name if count == "1" else f"{name} x{count}")
    return tags


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
    if progs.interactor:
        return run_interactive(progs, d, opt)

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


# ---------------------------------------------------------------- インタラクティブ


class Transcript:
    """main と interactor のやり取りを行単位で記録する。長すぎるときは先頭と末尾だけ残す。"""

    HEAD, TAIL, WIDTH = 5000, 5000, 2000

    def __init__(self) -> None:
        self.head: list[str] = []
        self.tail: deque[str] = deque(maxlen=self.TAIL)
        self.dropped = 0
        self.partial = {">": b"", "<": b""}
        self.last = ""  # 最後に記録した行の向き
        self.lock = threading.Lock()

    def _line(self, mark: str, text: str) -> None:
        if len(text) > self.WIDTH:
            text = text[:self.WIDTH] + f" ... (+{len(text) - self.WIDTH} chars)"
        self.last = mark
        line = f"{mark} {text}"
        if len(self.head) < self.HEAD:
            self.head.append(line)
            return
        if len(self.tail) == self.TAIL:
            self.dropped += 1
        self.tail.append(line)

    def feed(self, mark: str, data: bytes) -> None:
        with self.lock:
            *lines, rest = (self.partial[mark] + data).split(b"\n")
            for ln in lines:
                self._line(mark, ln.decode(errors="replace").rstrip("\r"))
            self.partial[mark] = rest

    def finish(self, mark: str) -> None:
        with self.lock:
            if self.partial[mark]:
                self._line(mark, self.partial[mark].decode(errors="replace") + "  (no newline)")
                self.partial[mark] = b""

    def note(self, text: str) -> None:
        with self.lock:
            line = f"# {text}"
            (self.head if len(self.head) < self.HEAD else self.tail).append(line)

    def lines(self) -> list[str]:
        mid = [f"... ({self.dropped} lines omitted) ..."] if self.dropped else []
        return self.head + mid + list(self.tail)


def _pump(src, dst, mark: str, log: Transcript) -> None:
    """src を読んで記録し、dst に流す。src が閉じたら dst も閉じる。"""
    fd = src.fileno()
    while True:
        try:
            data = os.read(fd, 1 << 16)
        except OSError:
            data = b""
        if not data:
            break
        log.feed(mark, data)
        if dst is not None:
            try:
                dst.write(data)
                dst.flush()
            except (BrokenPipeError, OSError):
                dst = None  # 相手はもう読まない。記録だけ続ける
    log.finish(mark)
    for f in (src, dst):
        if f is not None:
            try:
                f.close()
            except OSError:
                pass


def _kill(p: subprocess.Popen) -> None:
    try:
        os.killpg(p.pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        pass


def run_interactive(progs: Programs, d: Path, opt: argparse.Namespace) -> Verdict:
    """main と interactor の間に st が入って中継する。やり取りは d/transcript.txt に残る。"""
    report = d / "report_interactor.txt"
    env = dict(os.environ, STRESS_REPORT=str(report))
    log = Transcript()
    with open(d / "stderr_main.txt", "wb") as em, open(d / "stderr_interactor.txt", "wb") as ei:
        start = time.perf_counter()
        main = subprocess.Popen([str(progs.main)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=em,
                                cwd=d, start_new_session=True)
        inter = subprocess.Popen([str(progs.interactor), str(d / "in.txt")], stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE, stderr=ei, cwd=d, start_new_session=True, env=env)
    pumps = [threading.Thread(target=_pump, args=(main.stdout, inter.stdin, ">", log), daemon=True),
             threading.Thread(target=_pump, args=(inter.stdout, main.stdin, "<", log), daemon=True)]
    for t in pumps:
        t.start()

    # どちらが先に終わったかで判定が変わるので、終了を見つけた順番を記録する
    main_end = inter_end = None  # 何回目のポーリングで終了を見つけたか
    main_killed = main_tle = inter_tle = False
    main_time = 0.0
    tick = 0
    while main_end is None or inter_end is None:
        tick += 1
        now = time.perf_counter()
        if main_end is None and main.poll() is not None:
            main_end, main_time = tick, now - start
        if inter_end is None and inter.poll() is not None:
            inter_end = tick
        if main_end is None:
            if now - start > opt.tl:
                main_tle = True
                _kill(main)
                main.wait()
                main_end, main_time = tick, now - start
            elif inter_end is not None and inter.returncode != 0:
                main_killed = True  # interactor が WA / 失敗で終わったら main は待たない
                _kill(main)
                main.wait()
                main_end, main_time = tick, now - start
        if inter_end is None:
            if now - start > opt.checker_tl:
                inter_tle = True
                _kill(inter)
                inter.wait()
                inter_end = tick
            elif main_end is not None and (main_tle or main.returncode != 0):
                _kill(inter)  # main が落ちたら interactor は待たない
                inter.wait()
                inter_end = tick
        if main_end is None or inter_end is None:
            time.sleep(0.0005 if now - start < 0.05 else 0.002)
    for p in (main, inter):
        _kill(p)  # 子プロセスが残っていてもパイプが閉じるように
    for t in pumps:
        t.join(timeout=5)

    msg = (d / "stderr_interactor.txt").read_text(errors="replace").strip()
    last_msg = msg.splitlines()[-1] if msg else ""
    mc, ic = main.returncode, inter.returncode
    if main_tle:
        hint = "  (main が応答していない。flush 忘れ？)" if log.last in ("<", "") else ""
        v = Verdict("TLE", f"main exceeded {opt.tl}s{hint}", main_time)
    elif mc != 0 and not main_killed and main_end <= inter_end:
        v = Verdict("RE", f"main: {describe_code(mc)}", main_time)
    elif inter_tle:
        v = Verdict("INTERACTOR_FAIL", f"interactor exceeded {opt.checker_tl}s", main_time)
    elif ic == 1:
        v = Verdict("WA", last_msg[4:] if last_msg.startswith("WA: ") else last_msg or "interactor exit 1", main_time)
    elif ic != 0:
        v = Verdict("INTERACTOR_FAIL", f"{describe_code(ic)} {msg}".strip(), main_time)
    elif mc != 0:
        v = Verdict("RE", f"main: {describe_code(mc)}", main_time)
    else:
        v = Verdict("OK", "", main_time)
    if main_tle:
        log.note(f"[st] main killed after {opt.tl}s")
    elif v.kind == "RE":
        log.note(f"[st] main: {describe_code(mc)}")
    elif v.kind != "OK" and last_msg:
        log.note(f"[interactor] {last_msg}")
    (d / "transcript.txt").write_text("\n".join(log.lines()) + "\n")
    if report.exists():
        info = dict(ln.split(None, 1) for ln in report.read_text().splitlines() if ln.strip())
        v.queries = (int(info.get("queries", 0)), int(info.get("limit", -1)))
    return v


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
        "tags": read_tags(dest),
        "case": case.id,
        "repro": f"st repro {case.id}" + judge_flags(opt),
        "mode": mode_name(opt),
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


def judge_flags(opt: argparse.Namespace) -> str:
    if getattr(opt, "interactor", None):
        return f" -i {opt.interactor}"
    return f" -c {opt.checker}" if opt.checker else ""


def mode_name(opt: argparse.Namespace) -> str:
    if getattr(opt, "interactor", None):
        return "interactive"
    return "checker" if opt.checker else "compare"


def head(path: Path, lines: int = 20, width: int = 200) -> str:
    if not path.exists():
        return Out.dim("(none)")
    text = path.read_text(errors="replace").splitlines()
    shown = [ln if len(ln) <= width else ln[:width] + Out.dim(" ...") for ln in text[:lines]]
    if len(text) > lines:
        shown.append(Out.dim(f"... ({len(text) - lines} more lines)"))
    return "\n".join(shown) if shown else Out.dim("(empty)")


def head_tail(path: Path, lines: int = 20, width: int = 200) -> str:
    """長いときは最初と最後を lines 行ずつ。インタラクティブの失敗はたいてい最後の方にある。"""
    text = path.read_text(errors="replace").splitlines()
    if len(text) <= 2 * lines:
        return head(path, 2 * lines, width)
    cut = [ln if len(ln) <= width else ln[:width] + Out.dim(" ...") for ln in text]
    return "\n".join(cut[:lines] + [Out.dim(f"... ({len(text) - 2 * lines} lines) ...")] + cut[-lines:])


def show_failure(d: Path, v: Verdict, case_id: str, repro: str, workdir: Path) -> None:
    Out.log()
    Out.log(Out.red(f"{v.kind}") + f"  {v.detail}")
    Out.log(Out.dim(f"case={case_id}  ->  {repro}"))
    if tags := read_tags(d):
        Out.log("tags: " + ", ".join(tags))
    Out.log(Out.yellow("---- input ----"))
    Out.log(head(d / "in.txt"))
    if (d / "transcript.txt").exists():
        Out.log(Out.yellow("---- transcript ----") + Out.dim("  (< interactor -> main, > main -> interactor)"))
        Out.log(head_tail(d / "transcript.txt"))
    else:
        Out.log(Out.yellow("---- main ----"))
        Out.log(head(d / "out_main.txt"))
    if (d / "out_ref.txt").exists():
        Out.log(Out.yellow("---- guchoku ----"))
        Out.log(head(d / "out_ref.txt"))
    for name in ("stderr_main.txt", "stderr_ref.txt", "stderr_checker.txt", "stderr_interactor.txt"):
        p = d / name
        if p.exists() and p.stat().st_size:
            Out.log(Out.yellow(f"---- {name} ----"))
            Out.log(head(p, 15))
    try:
        rel = d.relative_to(workdir)
    except ValueError:
        rel = d
    Out.log(Out.dim(f"saved: {rel}"))


# ---------------------------------------------------------------- in.txt への書き出し


def _sha1(path: Path) -> str:
    return hashlib.sha1(path.read_bytes()).hexdigest()


def copy_to_in(workdir: Path, src: Path) -> None:
    """失敗した入力を workdir/in.txt にコピーする。

    st が前に書いた in.txt の SHA-1 を .stress/in.txt.sha1 に残しておき、in.txt がそれと違う
    （手で書いた、書き換えた）ときだけ .stress/in.txt.bak に退避する。st が書いたものどうしの
    上書きでは退避しないので、失敗が続いても手書きの in.txt は残る。
    """
    target = workdir / "in.txt"
    stress = workdir / ".stress"
    mark = stress / "in.txt.sha1"
    if target.exists() and target.stat().st_size:
        written = mark.read_text().strip() if mark.exists() else None
        if _sha1(target) != written:
            backup = stress / "in.txt.bak"
            shutil.copy2(target, backup)
            Out.log(Out.dim(f"backed up hand-written in.txt -> {backup.relative_to(workdir)}  (st restore で戻す)"))
    shutil.copy2(src, target)
    mark.write_text(_sha1(target) + "\n")
    Out.log(Out.green("copied") + " failing input -> in.txt")


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
        if getattr(opt, "interactor", None):
            sources["interactor"] = find_source(workdir, opt.interactor, "interactor")
        elif opt.checker:
            sources["checker"] = find_source(workdir, opt.checker, "checker")
            if opt.ref:
                sources["ref"] = find_source(workdir, opt.guchoku, "reference (guchoku)")
        else:
            sources["ref"] = find_source(workdir, opt.guchoku, "reference (guchoku)")

    Out.status("building " + ", ".join(p.name for p in sources.values()) + " ...")
    t0 = time.perf_counter()
    try:
        with ThreadPoolExecutor(max_workers=len(sources)) as ex:
            # gen / checker / interactor は ACL を使わないことが多いので PCH なし
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
                     checker=exes.get("checker"), interactor=exes.get("interactor"), gens=[r[0] for r in rows],
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


def fmt_queries(q: tuple[int, int] | None) -> str:
    if not q:
        return ""
    return f"  max queries {q[0]}" + (f"/{q[1]}" if q[1] >= 0 else "")


def check_mode(opt: argparse.Namespace) -> None:
    if opt.interactor:
        bad = [f for f, on in (("-c", opt.checker), ("--ref", opt.ref), ("--cmp", opt.cmp is not None)) if on]
        if bad:
            die(f"-i と {', '.join(bad)} は一緒に使えない")
    if opt.cmp is None:
        opt.cmp = "token"


def cmd_run(opt: argparse.Namespace) -> int:
    check_mode(opt)
    workdir = Path.cwd()
    progs, sources = prepare(opt, workdir, {"main", "judge"})
    gens = select_gens(progs, opt.gens, big=False)
    run_dir = workdir / ".stress" / "run"
    base = opt.seed if opt.seed is not None else int.from_bytes(os.urandom(8), "little")
    start = time.monotonic()
    deadline = start + opt.time if opt.time else math.inf
    # -n / -t を書かなかったときは、STRESS_BIG の生成器を 1 つにつき --big-iters 回で止める
    big_limit = opt.big_iters if opt.iters is None and not opt.time else None
    iters = opt.iters if opt.iters is not None else 1000
    limited = {n for n in gens if n in progs.big} if big_limit is not None else set()
    if limited and len(limited) == len(gens):
        iters = min(iters, big_limit * len(gens))
    total = iters if not opt.time else None
    mode = mode_name(opt) if mode_name(opt) != "compare" else f"compare/{opt.cmp}"
    Out.log(Out.dim(f"mode={mode} gens={','.join(gens)} base_seed={base} tl={opt.tl}s"))

    slowest = 0.0
    most_queries: tuple[int, int] | None = None
    k = 0
    last_status = 0.0
    done = {n: 0 for n in gens}
    active = list(gens)
    while (total is None or k < total) and time.monotonic() < deadline:
        # 生成器は順番に回し、cap は生成器ごとに小さい方から増やす
        name = active[k % len(active)]
        case = Case(case_seed(base, k), cap_for(done[name], opt.max_cap), name)
        done[name] += 1
        if name in limited and done[name] >= big_limit:
            active.remove(name)
            if not active:
                total = k + 1
        v = run_case(progs, case, run_dir, opt)
        slowest = max(slowest, v.elapsed)
        if v.queries and (most_queries is None or v.queries[0] > most_queries[0]):
            most_queries = v.queries
        k += 1
        now = time.monotonic()
        if now - last_status > 0.1:
            prog = f"{k}/{total}" if total else f"{k}"
            Out.status(f"[{prog}] cap={case.cap} OK  max {slowest * 1000:.0f}ms{fmt_queries(most_queries)}"
                       f"  {now - start:.1f}s")
            last_status = now
        if v.kind == "OK":
            continue

        if v.kind in ("GEN_FAIL", "CHECKER_FAIL", "INTERACTOR_FAIL"):
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
        if opt.to_in:
            copy_to_in(workdir, dest / "in.txt")
        Out.log(Out.dim(f"found after {k} cases ({time.monotonic() - start:.1f}s)"))
        return EXIT_FOUND

    Out.log(Out.green("OK") + f"  {k} cases  max {slowest * 1000:.0f}ms{fmt_queries(most_queries)}"
            f"  {time.monotonic() - start:.1f}s"
            + Out.dim(f"  base_seed={base}"))
    return EXIT_OK


def cmd_repro(opt: argparse.Namespace) -> int:
    check_mode(opt)
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
    if opt.to_in:
        copy_to_in(workdir, dest / "in.txt")
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
        copy_to_in(workdir, last / "in.txt")
    return EXIT_OK


def cmd_restore(opt: argparse.Namespace) -> int:
    workdir = Path.cwd()
    target = workdir / "in.txt"
    backup = workdir / ".stress" / "in.txt.bak"
    mark = workdir / ".stress" / "in.txt.sha1"
    if not backup.exists():
        die("no backup (.stress/in.txt.bak)")
    if target.exists() and target.stat().st_size and not opt.force:
        written = mark.read_text().strip() if mark.exists() else None
        if _sha1(target) != written and _sha1(target) != _sha1(backup):
            die("in.txt was edited after st wrote it; st restore --force で上書きする")
    shutil.copy2(backup, target)
    # 戻したのは手書きのものなので、次の失敗ではまた退避する
    mark.unlink(missing_ok=True)
    Out.log(Out.green("restored") + " .stress/in.txt.bak -> in.txt")
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
    names = ["gen.cpp"] + (["checker.cpp"] if opt.checker else []) + (["interactor.cpp"] if opt.interactor else [])
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
    p.add_argument("--gen-tl", type=float, default=10.0, help="gen の制限時間 [s] (default: 10)")


def add_common(p: argparse.ArgumentParser) -> None:
    add_build(p)
    p.add_argument("--main", default="main.cpp", help="解答 (default: main.cpp)")
    p.add_argument("--guchoku", default="guchoku.cpp", help="愚直解 (default: guchoku.cpp)")
    p.add_argument("-c", "--checker", metavar="SRC", help="checker モード（例: -c checker.cpp）")
    p.add_argument("--ref", action="store_true", help="checker モードでも愚直解を走らせ、出力を第 3 引数で渡す")
    p.add_argument("-i", "--interactor", metavar="SRC", help="インタラクティブ（例: -i interactor.cpp）。愚直解は使わない")
    p.add_argument("--tl", type=float, default=2.0, help="main の制限時間 [s] (default: 2)")
    p.add_argument("--ref-tl", type=float, default=10.0, help="愚直解の制限時間 [s] (default: 10)")
    p.add_argument("--checker-tl", type=float, default=10.0, help="checker / interactor の制限時間 [s] (default: 10)")
    p.add_argument("--cmp", default=None, help="比較方法: token (default) / exact / float[:EPS]")
    p.add_argument("--no-to-in", dest="to_in", action="store_false",
                   help="失敗した入力を in.txt にコピーしない（既定ではコピーする）")


def add_cases(p: argparse.ArgumentParser, count: int) -> None:
    p.add_argument("names", nargs="*", help="生成器の名前（省略時は STRESS_BIG のもの、無ければ全部）")
    p.add_argument("-n", "--count", type=int, default=count, help=f"生成器ごとの個数 (default: {count})")
    p.add_argument("--seed", type=int, help="基準 seed（省略時はランダム）")
    p.add_argument("--cap", type=int, help="g.size() の上限（省略時は上限なし）")


def parse(argv: list[str]) -> argparse.Namespace:
    commands = {"run", "repro", "make", "bench", "last", "restore", "clean", "init"}
    if not argv or argv[0] not in commands:
        argv = ["run", *argv]

    ap = argparse.ArgumentParser(prog="st", description="ランダムテストのドライバ")
    sub = ap.add_subparsers(dest="command", required=True)

    p = sub.add_parser("run", help="ランダムテストを回す（既定）")
    add_common(p)
    p.add_argument("-g", "--gen", dest="gens", action="append", metavar="NAME",
                   help="使う生成器（複数可、カンマ区切りも可。省略時は STRESS_BIG 以外を順番に）")
    p.add_argument("-n", "--iters", type=int, help="回数 (default: 1000)")
    p.add_argument("--big-iters", type=int, default=20,
                   help="-n / -t を書かないとき、STRESS_BIG の生成器 1 つあたりの回数 (default: 20)")
    p.add_argument("-t", "--time", type=float, help="回数の代わりに秒数で回す")
    p.add_argument("--seed", type=int, help="基準 seed（省略時はランダム）")
    p.add_argument("--max-cap", type=int, help="g.size() に渡す cap の上限")
    p.add_argument("--no-shrink", dest="shrink", action="store_false", help="失敗後の縮小をしない")
    p.add_argument("--shrink-tries", type=int, default=200, help="縮小で cap 1 つあたりに試す回数 (default: 200)")
    p.add_argument("--shrink-time", type=float, default=20.0, help="縮小にかける時間の上限 [s] (default: 20)")

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

    p = sub.add_parser("restore", help="退避した手書きの in.txt（.stress/in.txt.bak）を戻す")
    p.add_argument("--force", action="store_true", help="今の in.txt を手で書き換えていても上書きする")

    p = sub.add_parser("clean", help=".stress/ の失敗ケースを消す")
    p.add_argument("--all", action="store_true", help="ビルド済みバイナリも消す")

    p = sub.add_parser("init", help="gen.cpp（と checker.cpp / interactor.cpp）の雛形を置く")
    p.add_argument("-c", "--checker", action="store_true", help="checker.cpp の雛形も置く")
    p.add_argument("-i", "--interactor", action="store_true", help="interactor.cpp の雛形も置く")
    p.add_argument("--force", action="store_true", help="同じ名前のファイルがあっても上書きする")
    p.add_argument("name", nargs="?", help="置くファイルの名前（gen.cpp だけを置くとき。例: st init gen2.cpp）")

    return ap.parse_args(argv)


def main(argv: list[str]) -> int:
    opt = parse(argv)
    handler = {"run": cmd_run, "repro": cmd_repro, "make": cmd_make, "bench": cmd_bench,
               "last": cmd_last, "restore": cmd_restore, "clean": cmd_clean, "init": cmd_init}
    try:
        return handler[opt.command](opt)
    except KeyboardInterrupt:
        Out.log(Out.yellow("interrupted"))
        return 130


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
