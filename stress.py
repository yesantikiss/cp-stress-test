#!/usr/bin/env python3
"""競プロ用ランダムテストのドライバ。

カレントディレクトリの main.cpp / guchoku.cpp / gen.cpp を
ビルドして、gen の出力で main と guchoku を比べる。-c で checker モード、-i でインタラクティブ。

  st                      # 1000 回
  st -n 5000 --seed 42
  st -t 60                # 60 秒
  st -j 4                 # 4 本並列（-j だけならコア数）
  st -c                   # スペシャルジャッジ（checker.cpp。別名なら -c foo.cpp）
  st -i                   # インタラクティブ（interactor.cpp。別名なら -i foo.cpp）
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
import contextlib
import datetime as dt
import hashlib
import heapq
import json
import math
import os
import re
import resource
import shlex
import select
import shutil
import signal
import subprocess
import sys
import threading
import time
from collections import deque
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait
from dataclasses import dataclass, field
from pathlib import Path

TOOL_ROOT = Path(__file__).resolve().parent
_spawn_lock = threading.Lock()  # child_limits の中（st 自身の上限を書き換えている間）は 1 スレッドだけ
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
    _lock = threading.RLock()

    @staticmethod
    def status(text: str) -> None:
        if not Out.tty:
            return
        with Out._lock:
            sys.stderr.write("\r" + text + " " * max(0, Out._status_len - len(text)))
            sys.stderr.flush()
            Out._status_len = len(text)

    @staticmethod
    def clear_status() -> None:
        if not Out.tty:
            return
        with Out._lock:
            if Out._status_len:
                sys.stderr.write("\r" + " " * Out._status_len + "\r")
                sys.stderr.flush()
                Out._status_len = 0

    @staticmethod
    def log(text: str = "") -> None:
        with Out._lock:
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
        if not self.sanitize:
            # 静的リンクにすると、起動のたびの動的リンク（libstdc++ の読み込み）が無くなり、1 回 0.5 ms ほど速く起動する。
            # 数 ms で終わるケースを何千回も回すときに効く。ASan は静的リンクできないので --san のときは付けない
            f.append("-static")
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
    if proc.returncode != 0 and use_pch and "-include" in flags and not (cfg.workdir / "pch_acl.hpp").exists() and "pch_acl.hpp:" in proc.stderr:
        # pch_acl.hpp 本体が無くフラグ不一致で .gch を使えなかったときは、PCH なしでやり直す
        flags = cfg.flags(src, False)  # このあと静的リンクなしでやり直すときも PCH なしのまま
        proc = subprocess.run(["g++", *flags, str(src), "-o", str(exe), "-MMD", "-MF", str(dep)],
                              cwd=cfg.workdir, capture_output=True, text=True)
    if proc.returncode != 0 and "-static" in flags and _link_error(proc.stderr):
        # libstdc++ や libc の静的ライブラリが無い環境では、静的リンクなしでやり直す（コンパイルエラーではやり直さない）
        flags = [f for f in flags if f != "-static"]
        proc = subprocess.run(["g++", *flags, str(src), "-o", str(exe), "-MMD", "-MF", str(dep)],
                              cwd=cfg.workdir, capture_output=True, text=True)
    if proc.returncode != 0:
        Out.log(Out.red(f"build failed: {src.name}"))
        Out.log(proc.stderr.rstrip())
        raise BuildError(src.name)
    return exe


def _link_error(stderr: str) -> bool:
    # リンクで失敗すると gcc は collect2 の行を出す。「collect2」はロケールによらないので、日本語のメッセージでも拾える
    return "collect2" in stderr or "ld returned" in stderr or "cannot find -l" in stderr


def _parse_make_deps(exe: Path, text: str) -> list[str]:
    text = text.replace("\\\r\n", " ").replace("\\\n", " ")
    m = re.search(rf"(?:^|[/\\]){re.escape(exe.name)}:(?=\s|$)", text)
    if m:
        deps_text = text[m.end():]
    else:
        _, _, deps_text = text.partition(":")
    deps: list[str] = []
    cur: list[str] = []
    i = 0
    n = len(deps_text)
    while i < n:
        c = deps_text[i]
        if c == "\\" and i + 1 < n and deps_text[i + 1] in (" ", "\t", "#", "\\", "$", ":"):
            cur.append(deps_text[i + 1])
            i += 2
        elif c == "$" and i + 1 < n and deps_text[i + 1] == "$":
            cur.append("$")
            i += 2
        elif c in (" ", "\t", "\n", "\r"):
            if cur:
                deps.append("".join(cur))
                cur.clear()
            i += 1
        else:
            cur.append(c)
            i += 1
    if cur:
        deps.append("".join(cur))
    return deps


def _stale(exe: Path, dep: Path) -> bool:
    built = exe.stat().st_mtime
    for d in _parse_make_deps(exe, dep.read_text(errors="replace")):
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


STACK_MB = 1024  # 子プロセスのスタックの上限。AtCoder と同じくメモリ制限（1024 MB）まで使えるようにする
MB = 1 << 20


@contextlib.contextmanager
def child_limits(output_mb: float | None):
    """この中で起動した子プロセスに、スタックの上限と、書けるファイルの大きさの上限（output_mb）を付ける。

    子プロセスは起動したときの st の上限を受け継ぐので、起動する間だけ st 自身の上限を変えて戻す。
    スタックは今の上限より小さくはしない（ulimit -s unlimited ならそのまま）。
    ファイルの上限を超えた子プロセスは SIGXFSZ で止まる（Popen が SIGXFSZ を既定の動作に戻すため）。
    """
    saved: list[tuple[int, tuple[int, int]]] = []
    inf = resource.RLIM_INFINITY
    # 上限は st 全体で 1 つなので、-j で同時に起動するときは 1 本ずつ通す（起動の一瞬だけ）
    _spawn_lock.acquire()
    try:
        for res, want in ((resource.RLIMIT_STACK, STACK_MB),
                          (resource.RLIMIT_FSIZE, output_mb)):
            if want is None:
                continue
            soft, hard = resource.getrlimit(res)
            new = int(want * MB) if hard == inf else min(int(want * MB), hard)
            if res == resource.RLIMIT_STACK and (soft == inf or soft >= new):
                continue
            resource.setrlimit(res, (new, hard))
            saved.append((res, (soft, hard)))
        yield
    finally:
        for res, lim in reversed(saved):
            resource.setrlimit(res, lim)
        _spawn_lock.release()


# ---------------------------------------------------------------- 並列実行で止めるための印


class Cancelled(Exception):
    """-j で回しているケースを途中で止めた。結果は捨てる。"""


class Token:
    """1 ケースぶんの子プロセスをまとめて止めるための印。止めたあとに起動したものもすぐ止める。"""

    def __init__(self) -> None:
        self.cancelled = False
        self.procs: list[subprocess.Popen] = []
        self.lock = threading.Lock()

    def add(self, p: subprocess.Popen) -> None:
        with self.lock:
            self.procs.append(p)
            if self.cancelled:
                _kill(p)

    def remove(self, p: subprocess.Popen) -> None:
        with self.lock:
            if p in self.procs:
                self.procs.remove(p)

    def cancel(self) -> None:
        with self.lock:
            self.cancelled = True
            procs = list(self.procs)
        for p in procs:
            _kill(p)


_tls = threading.local()  # 作業スレッドごとの Token


def _token() -> Token | None:
    return getattr(_tls, "token", None)


def check_cancel() -> None:
    t = _token()
    if t is not None and t.cancelled:
        raise Cancelled


def _track(p: subprocess.Popen) -> None:
    if (t := _token()) is not None:
        t.add(p)


def _untrack(p: subprocess.Popen) -> None:
    if (t := _token()) is not None:
        t.remove(p)


def wait_exit(proc: subprocess.Popen, timeout: float) -> bool:
    """proc が終わるまで最大 timeout 秒待つ。終わったら True（回収はしないので、続けて proc.wait() を呼ぶ）。

    Popen.wait(timeout=...) は最大 50 ms 間隔で終わったかを見に行くので、そのぶん遅く、時間も長く測ってしまう。
    Linux では pidfd で終わった瞬間に起きる。使えないときだけ Popen.wait に任せる。
    """
    if hasattr(os, "pidfd_open"):
        try:
            fd = os.pidfd_open(proc.pid)
        except OSError:
            fd = -1
        if fd >= 0:
            try:
                ready, _, _ = select.select([fd], [], [], max(0.0, timeout))
            finally:
                os.close(fd)
            return bool(ready)
    try:
        proc.wait(timeout=timeout)
        return True
    except subprocess.TimeoutExpired:
        return False


@dataclass
class RunResult:
    status: str  # OK / RE / TLE / OLE（出力が output_mb を超えた）
    code: int
    elapsed: float


def run(exe: Path | list[str], stdin: Path | None, stdout: Path, stderr: Path, tl: float,
        cwd: Path, args: list[str] | None = None, env: dict[str, str] | None = None,
        output_mb: float | None = None) -> RunResult:
    """1 回実行する。Ctrl+C などで抜けるときも、子プロセスは必ず止める。"""
    check_cancel()
    cmd = [str(exe)] if isinstance(exe, Path) else list(exe)
    cmd += args or []
    proc = None
    with open(stdout, "wb") as fo, open(stderr, "wb") as fe, \
            (open(stdin, "rb") if stdin else contextlib.nullcontext(subprocess.DEVNULL)) as fi:
        try:
            with child_limits(output_mb):
                start = time.perf_counter()
                proc = subprocess.Popen(cmd, stdin=fi, stdout=fo, stderr=fe, cwd=cwd, start_new_session=True,
                                        env={**os.environ, **env} if env else None)
            _track(proc)
            if not wait_exit(proc, tl):
                _kill(proc)
                proc.wait()
                check_cancel()
                return RunResult("TLE", -9, time.perf_counter() - start)
            code = proc.wait()
            elapsed = time.perf_counter() - start
        finally:
            if proc is not None:
                if proc.returncode is None:
                    _kill(proc)
                    proc.wait()
                _untrack(proc)
    check_cancel()  # 止められて終わった結果は使わない
    status = "OK" if code == 0 else "OLE" if code == -signal.SIGXFSZ and output_mb is not None else "RE"
    return RunResult(status, code, elapsed)


def describe_code(code: int) -> str:
    if code == -signal.SIGXFSZ:
        return "signal SIGXFSZ (output over --output-limit)"
    if code < 0:
        try:
            return f"signal {signal.Signals(-code).name}"
        except ValueError:
            return f"signal {-code}"
    return f"exit {code}"


# ---------------------------------------------------------------- 比較


def parse_cmp_mode(mode: str) -> tuple[str, float | None]:
    if mode in ("token", "exact"):
        return mode, None
    if mode == "float":
        return "float", 1e-6
    if mode.startswith("float:"):
        raw = mode[6:]
        try:
            eps = float(raw)
        except ValueError:
            eps = math.nan
        if not raw or not math.isfinite(eps) or eps < 0:
            die(f"invalid --cmp float epsilon: {mode!r} (expected non-negative finite number)")
        return "float", eps
    die(f"unknown --cmp mode: {mode!r} (expected token, exact, float, or float:EPS)")


def compare(a: bytes, b: bytes, mode: str) -> tuple[bool, str]:
    kind, eps = parse_cmp_mode(mode)
    if kind == "exact":
        if a == b:
            return True, ""
        n = next((k for k in range(min(len(a), len(b))) if a[k] != b[k]), min(len(a), len(b)))
        return False, f"first difference at byte {n}"
    ta, tb = a.split(), b.split()
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
    kind: str          # OK / WA / RE / TLE / OLE / REF_RE / REF_TLE / REF_OLE / CHECKER_FAIL / INTERACTOR_FAIL / GEN_FAIL
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
    g = run(progs.gen, None, d / "in.txt", d / "stderr_gen.txt", opt.gen_tl, d, case.gen_args())
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


SHM = Path("/dev/shm")


def _shm_dir(workdir: Path) -> Path | None:
    """問題のディレクトリごとの /dev/shm の作業場所。使えなければ None。"""
    if not SHM.is_dir() or not os.access(SHM, os.W_OK):
        return None
    base = SHM / f"cp-stress-test-{os.getuid()}"
    try:
        base.mkdir(mode=0o700, exist_ok=True)
        st = base.lstat()
        if base.is_symlink() or st.st_uid != os.getuid():
            return None  # 他人が作ったものは使わない
    except OSError:
        return None
    return base / hashlib.sha1(str(workdir.resolve()).encode()).hexdigest()[:12]


def work_root(workdir: Path) -> Path:
    """ケースを走らせる作業場所を返す。

    /dev/shm（メモリ上のファイルシステム）が使えればそこに置き、.stress/run をそこへのシンボリックリンクにする。
    1 ケースごとにファイルを作って消すので、ディスクより速い（WSL では特に差が大きい）。使えなければ .stress/run。
    """
    link = workdir / ".stress" / "run"
    real = _shm_dir(workdir)
    if real is not None:
        try:
            real.mkdir(exist_ok=True)
            if not (link.is_symlink() and os.readlink(link) == str(real)):
                if link.is_symlink() or link.is_file():
                    link.unlink()
                elif link.is_dir():
                    shutil.rmtree(link)
                link.parent.mkdir(parents=True, exist_ok=True)
                link.symlink_to(real)
            return real
        except OSError:
            pass
    if link.is_symlink():
        link.unlink()
    return link


def fresh_dir(d: Path) -> Path:
    """d を空にする（前のケースの出力が残らないように）。ディレクトリ自体は消さずに中身だけ消す（その方が速い）。"""
    if d.is_dir() and not d.is_symlink():
        with os.scandir(d) as it:
            for e in it:
                if e.is_dir(follow_symlinks=False):
                    shutil.rmtree(e.path)
                else:
                    os.unlink(e.path)
        return d
    if d.is_symlink() or d.exists():
        d.unlink()
    d.mkdir(parents=True)
    return d


def run_case(progs: Programs, case: Case, d: Path, opt: argparse.Namespace) -> Verdict:
    fresh_dir(d)
    inp = d / "in.txt"
    if (bad := generate(progs, case, d, opt)) is not None:
        return bad
    if progs.interactor:
        return run_interactive(progs, d, opt)

    m = run(progs.main, inp, d / "out_main.txt", d / "stderr_main.txt", opt.tl, d, output_mb=opt.output_limit)
    if m.status == "TLE":
        return Verdict("TLE", f"main exceeded {opt.tl}s", m.elapsed)
    if m.status == "OLE":
        return Verdict("OLE", f"main output exceeded {opt.output_limit:g} MB", m.elapsed)
    if m.status == "RE":
        return Verdict("RE", f"main: {describe_code(m.code)}", m.elapsed)

    if progs.ref:
        r = run(progs.ref, inp, d / "out_ref.txt", d / "stderr_ref.txt", opt.ref_tl, d,
                output_mb=opt.output_limit)
        if r.status == "TLE":
            return Verdict("REF_TLE", f"guchoku exceeded {opt.ref_tl}s", m.elapsed)
        if r.status == "OLE":
            return Verdict("REF_OLE", f"guchoku output exceeded {opt.output_limit:g} MB", m.elapsed)
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
        if c.status == "TLE" or c.code != 1:
            # 0 と 1 以外（3、ほかの値、例外やシグナルで落ちた、TL 超え）は checker 自身の失敗
            what = f"checker exceeded {opt.checker_tl}s" if c.status == "TLE" else describe_code(c.code)
            return Verdict("CHECKER_FAIL", f"{what} {msg}".strip(), m.elapsed)
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
            if len(self.head) < self.HEAD:
                self.head.append(line)
                return
            if len(self.tail) == self.TAIL:
                self.dropped += 1
            self.tail.append(line)

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
    log = Transcript()
    main = inter = None
    check_cancel()
    try:
        with open(d / "stderr_main.txt", "wb") as em, open(d / "stderr_interactor.txt", "wb") as ei, \
                child_limits(opt.output_limit):
            start = time.perf_counter()
            main = subprocess.Popen([str(progs.main)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=em,
                                    cwd=d, start_new_session=True)
            inter = subprocess.Popen([str(progs.interactor), str(d / "in.txt")], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=ei, cwd=d, start_new_session=True)
            _track(main)
            _track(inter)
        v = _interact(main, inter, start, log, report, d, opt)
        check_cancel()
        return v
    finally:
        for p in (main, inter):  # Ctrl+C などで抜けたときも、子プロセスを残さない
            if p is not None:
                _kill(p)
                p.wait()
                _untrack(p)


def _interact(main: subprocess.Popen, inter: subprocess.Popen, start: float, log: Transcript, report: Path,
              d: Path, opt: argparse.Namespace) -> Verdict:
    pumps = [threading.Thread(target=_pump, args=(main.stdout, inter.stdin, ">", log), daemon=True),
             threading.Thread(target=_pump, args=(inter.stdout, main.stdin, "<", log), daemon=True)]
    for t in pumps:
        t.start()

    # 両方が終わるまで待つ。片方が失敗で終わったら、もう片方は待たずに止める
    main_end = inter_end = None  # 何回目のポーリングで終了を見つけたか
    main_killed = main_tle = inter_tle = inter_killed = False
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
                inter_killed = True
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
    # どちらが先に終わったかはポーリングの間隔しだいで入れ替わるので、判定には使わない。
    # 代わりに interactor が書いた理由を見る: WA の理由が「main の出力が尽きた」なら main が先に終わった。
    main_crashed = mc != 0 and not main_killed and not main_tle
    inter_failed = inter_tle or (ic not in (0, 1) and not inter_killed)
    by_main_end = ic == 1 and (("unexpected end of file" in last_msg and last_msg.startswith(("output ", "WA: output ")))
                               or "main closed its input" in last_msg)
    if main_tle:
        hint = "  (main が応答していない。flush 忘れ？)" if log.last in ("<", "") else ""
        v = Verdict("TLE", f"main exceeded {opt.tl}s{hint}", main_time)
    elif inter_failed:
        what = f"interactor exceeded {opt.checker_tl}s" if inter_tle else f"{describe_code(ic)} {msg}".strip()
        v = Verdict("INTERACTOR_FAIL", what, main_time)
    elif ic == 1 and not (main_crashed and by_main_end):
        v = Verdict("WA", last_msg[4:] if last_msg.startswith("WA: ") else last_msg or "interactor exit 1", main_time)
    elif main_crashed and mc == -signal.SIGXFSZ:
        v = Verdict("OLE", f"main stderr exceeded {opt.output_limit:g} MB", main_time)
    elif main_crashed:
        v = Verdict("RE", f"main: {describe_code(mc)}", main_time)
    elif ic == 1:
        v = Verdict("WA", last_msg[4:] if last_msg.startswith("WA: ") else last_msg or "interactor exit 1", main_time)
    else:
        v = Verdict("OK", "", main_time)
    if main_tle:
        log.note(f"[st] main killed after {opt.tl}s")
    elif v.kind in ("RE", "OLE"):
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
    """失敗時に表示する `st repro ...` に付けるオプション。既定値と違うものだけ付ける。"""
    flags: list[str] = []
    if getattr(opt, "interactor", None):
        flags += ["-i", opt.interactor]
    elif opt.checker:
        flags += ["-c", opt.checker]
        if opt.ref:
            flags.append("--ref")
    for name, attr, default in (("--main", "main", "main.cpp"), ("--guchoku", "guchoku", "guchoku.cpp"),
                                ("--gen-file", "gen_file", "gen.cpp"), ("--cmp", "cmp", "token"),
                                ("--tl", "tl", 2.0), ("--ref-tl", "ref_tl", 10.0),
                                ("--checker-tl", "checker_tl", 10.0), ("--gen-tl", "gen_tl", 10.0),
                                ("--output-limit", "output_limit", 64.0),
                                ("--cxxflags", "cxxflags", os.environ.get("STRESS_CXXFLAGS", ""))):
        value = getattr(opt, attr, default)
        if value is None or value == default:
            continue
        if attr == "guchoku" and getattr(opt, "interactor", None):
            continue  # インタラクティブでは愚直解を使わない
        if attr == "guchoku" and opt.checker and not opt.ref:
            continue
        if attr == "cmp" and (opt.checker or getattr(opt, "interactor", None)):
            continue
        sval = f"{value:g}" if isinstance(value, float) else str(value)
        if sval.startswith("-"):
            flags.append(f"{name}={sval}")
        else:
            flags += [name, sval]
    if getattr(opt, "san", False):
        flags.append("--san")
    return "".join(" " + shlex.quote(f) for f in flags)


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


TOOL_FAILURES = ("GEN_FAIL", "CHECKER_FAIL", "INTERACTOR_FAIL")  # 道具の側のバグ。解答の失敗としては扱わない


def shrink(runner: "Runner", case: Case, opt: argparse.Namespace, deadline: float
           ) -> tuple[tuple[Case, Verdict, Path] | None, list[tuple[Case, Verdict]]]:
    """同じ生成器で cap を小さくして、失敗する入力を探し直す。

    返り値は (見つかった失敗とその作業ディレクトリ or None, 途中で出た道具の側の失敗)。GEN_FAIL などは解答の失敗ではないので
    採用せずに飛ばし、縮小を続ける（あとで警告として出す）。-j のときは候補を並列に試し、番号が一番小さい失敗を採る。
    """
    cap = case.cap or 1
    caps = sorted({c for c in [1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 25, 32, 40, 50, 64, 80, 100] if c < cap}
                  | ({cap // 2, cap * 3 // 4} - {0, cap}))
    base = splitmix64(case.seed ^ 0x5EED)
    tool_failures: list[tuple[int, Case, Verdict]] = []

    def candidates():
        for c in caps:
            for t in range(opt.shrink_tries):
                if time.monotonic() > deadline:
                    return
                yield Case(case_seed(base, c * 1_000_003 + t), c, case.name)

    def on_result(idx: int, cand: Case, v: Verdict) -> None:
        if v.kind in TOOL_FAILURES:
            tool_failures.append((idx, cand, v))
        Out.status(f"shrinking: cap={cand.cap} try {idx % opt.shrink_tries + 1}/{opt.shrink_tries}")

    found = None
    try:
        found = runner.search(candidates(), lambda v: v.kind != "OK" and v.kind not in TOOL_FAILURES, on_result)
    except KeyboardInterrupt:
        # 縮小は待ちきれずに止めることがあるので、Ctrl+C は「縮小をやめる」にして元の失敗を残す
        Out.log(Out.yellow("shrink interrupted") + "  縮小をやめて、見つけた失敗をそのまま保存する")
    limit = found[0] if found else math.inf
    tools = [(c, v) for i, c, v in sorted(tool_failures, key=lambda x: x[0]) if i < limit]
    if found is None:
        return None, tools
    _, cand, v, d = found
    return (cand, v, d), tools


# ---------------------------------------------------------------- 並列実行


RECHECK = ("TLE", "REF_TLE")  # -j で出たら、単独で測り直す


class Runner:
    """ケースを最大 jobs 本まで同時に走らせ、失敗のうち番号が一番小さいものを返す。

    それより前の番号で走っているケースは最後まで待ち、後ろの番号のものは止める。こうすると -j の値によらず、
    直列で回したときと同じ失敗になる。jobs = 1 のときはスレッドを使わず、今のスレッドでそのまま回す。
    -j で TLE / REF_TLE が出たら、ほかを止めてそのケースだけ単独で測り直す。単独で通ったら、
    並列だと時間がぶれるということなので、そこからは -j 1 で回す。
    """

    def __init__(self, root: Path, jobs: int, evaluate) -> None:
        self.jobs = jobs
        self.evaluate = evaluate  # (Case, 作業ディレクトリ) -> Verdict
        if jobs == 1:
            self.free = [root]
        else:
            fresh_dir(root)
            self.free = [root / str(i) for i in range(jobs, 0, -1)]
        self.pool = ThreadPoolExecutor(max_workers=jobs) if jobs > 1 else None

    def close(self) -> None:
        if self.pool is not None:
            self.pool.shutdown(wait=True)

    def release(self, d: Path) -> None:
        self.free.append(d)

    def _work(self, token: Token, case: Case, d: Path) -> Verdict:
        _tls.token = token
        try:
            return self.evaluate(case, d)
        finally:
            _tls.token = None

    def search(self, cases, is_fail, on_result=None) -> tuple[int, Case, Verdict, Path] | None:
        """cases を番号の順に試す。失敗が無ければ None。見つけたら (番号, ケース, 判定, 作業ディレクトリ)。

        返した作業ディレクトリは release するまで使わない。on_result(番号, ケース, 判定) は結果が出るたびに呼ぶ。
        """
        it = enumerate(cases)
        pending: list[tuple[int, int, Case]] = []  # 止めて戻したケース（番号の小さい順に取り出す）
        inflight: dict = {}  # Future -> (番号, ケース, 作業ディレクトリ, Token)
        best: tuple[int, Case, Verdict, Path] | None = None
        stopped = False

        def take() -> tuple[int, Case] | None:
            nonlocal stopped
            # pending のケースはどれも it から取り出し済みで、it の残りより番号が小さい。
            # stopped（it からはもう取らない）になったあとでも、測り直しで pending に戻したケースは走らせる
            if pending:
                if best is not None and pending[0][0] > best[0]:
                    pending.clear()  # 残りは全部 best より後ろ
                    return None
                idx, _, case = heapq.heappop(pending)
                return idx, case
            if stopped:
                return None
            try:
                idx, case = next(it)
            except StopIteration:
                stopped = True
                return None
            if best is not None and idx > best[0]:
                stopped = True  # これより後ろは要らない（番号は増える一方）
                return None
            return idx, case

        def accept(idx: int, case: Case, v: Verdict, d: Path) -> None:
            nonlocal best
            if on_result is not None:
                on_result(idx, case, v)
            if is_fail(v) and (best is None or idx < best[0]):
                if best is not None:
                    self.release(best[3])
                best = (idx, case, v, d)
                for i, _, _, tok in inflight.values():
                    if i > idx:
                        tok.cancel()
            else:
                self.release(d)

        def cancel_all() -> None:
            for _, _, _, tok in inflight.values():
                tok.cancel()
            for f, (i, c, d, _) in list(inflight.items()):
                try:
                    f.result()
                except Exception:  # Cancelled のほか、止めたせいで出た例外も捨てる
                    pass
                self.release(d)
                heapq.heappush(pending, (i, c.seed, c))
            inflight.clear()

        try:
            while True:
                while len(inflight) < self.jobs and (item := take()) is not None:
                    idx, case = item
                    d = self.free.pop()
                    if self.jobs == 1 and not inflight:
                        accept(idx, case, self.evaluate(case, d), d)
                        continue
                    tok = Token()
                    inflight[self.pool.submit(self._work, tok, case, d)] = (idx, case, d, tok)
                if not inflight:
                    break
                done, _ = wait(inflight, timeout=0.1, return_when=FIRST_COMPLETED)
                for f in sorted(done, key=lambda f: inflight[f][0]):
                    if f not in inflight:
                        continue  # 測り直しのときに片付けた
                    idx, case, d, tok = inflight.pop(f)
                    try:
                        v = f.result()
                    except Cancelled:
                        v = None
                    if v is None or tok.cancelled:
                        self.release(d)
                        heapq.heappush(pending, (idx, case.seed, case))
                        continue
                    if v.kind in RECHECK and self.jobs > 1:
                        v = self._recheck(case, v, d, cancel_all)
                    accept(idx, case, v, d)
        except BaseException:
            # Ctrl+C など: 走っている子プロセスを全部止めてから抜ける
            for _, _, _, tok in inflight.values():
                tok.cancel()
            for f in list(inflight):
                try:
                    f.result()
                except BaseException:
                    pass
            raise
        return best

    def _recheck(self, case: Case, v: Verdict, d: Path, cancel_all) -> Verdict:
        cancel_all()  # ほかのケースは止めて、あとで走らせ直す
        Out.status(f"{v.kind} under -j {self.jobs}; re-running case={case.id} alone ...")
        v2 = self.evaluate(case, d)
        if v2.kind in RECHECK:
            return v2
        # 単独では時間内に終わった。OK とは限らない（WA や RE なら、その失敗として報告される）
        if v2.kind == "OK":
            what = f"単独では通った（{v2.elapsed * 1000:.0f}ms）" if v.kind == "TLE" else "単独では通った"
        else:
            what = f"単独では時間内に終わって {v2.kind} だった"
        Out.log(Out.yellow("warning: ") + f"-j {self.jobs} では {v.kind} だったケースが{what}。"
                "並列だと時間がぶれるので、ここからは -j 1 で回す")
        Out.log(Out.dim(f"  case={case.id}"))
        self.jobs = 1
        return v2


def resolve_jobs(jobs: int | None) -> int:
    if jobs is None:
        # -j を書かなかったときは環境変数 STRESS_JOBS（~/.bashrc で自分の既定を決める用）、無ければ 1
        env = os.environ.get("STRESS_JOBS", "").strip()
        if not env:
            return 1
        try:
            jobs = int(env)
        except ValueError:
            die(f"invalid STRESS_JOBS: {env!r} (expected an integer >= 0)")
        if jobs < 0:
            die(f"invalid STRESS_JOBS: {env!r} (expected an integer >= 0)")
    if jobs < 0:
        die("-j must be >= 0")
    if jobs == 0:
        try:
            return max(1, len(os.sched_getaffinity(0)))
        except AttributeError:
            return max(1, os.cpu_count() or 1)
    return jobs


# ---------------------------------------------------------------- ビルドの準備


CASE_ID_RE = re.compile(r"^\d+(:.*)?$")  # SEED[:CAP[:NAME]]


def find_source(workdir: Path, name: str, what: str) -> Path:
    p = (workdir / name)
    if not p.exists():
        hint = {"generator": "  (st init で雛形を置けます)", "checker": "  (st init -c で雛形を置けます)",
                "interactor": "  (st init -i で雛形を置けます)"}.get(what, "")
        die(f"{what} not found: {p}{hint}")
    return p


def prepare(opt: argparse.Namespace, workdir: Path, need: set[str]) -> tuple[Programs, dict[str, Path]]:
    raw_flags = getattr(opt, "cxxflags", "")
    try:
        extra = shlex.split(raw_flags) if raw_flags else []
    except ValueError as e:
        die(f"invalid --cxxflags: {e}")
    cfg = BuildConfig(workdir, sanitize=getattr(opt, "san", False), extra=extra)
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
    names = list(dict.fromkeys(n for w in wanted for n in w.split(",") if n))
    if not names:
        die(f"no generator specified for -g (available: {', '.join(progs.gens)})")
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
    parse_cmp_mode(opt.cmp)


class CaseStream:
    """st run で試すケースの並び。k 番目のケースは k と基準 seed だけで決まる（-j でも同じ）。

    生成器は順番に回し、cap は生成器ごとに小さい方から増やす。STRESS_BIG の生成器は big_limit 回で外す。
    """

    def __init__(self, gens: list[str], limited: set[str], big_limit: int | None, total: int | None,
                 base: int, max_cap: int | None, deadline: float) -> None:
        self.limited, self.big_limit, self.total = limited, big_limit, total
        self.base, self.max_cap, self.deadline = base, max_cap, deadline
        self.done = {n: 0 for n in gens}
        self.active = [n for n in gens if not (n in limited and big_limit == 0)]
        if not self.active:
            self.total = 0

    def __iter__(self):
        k = pos = 0
        while self.active and (self.total is None or k < self.total) and time.monotonic() < self.deadline:
            name = self.active[pos]
            case = Case(case_seed(self.base, k), cap_for(self.done[name], self.max_cap), name)
            self.done[name] += 1
            if name in self.limited and self.done[name] >= self.big_limit:
                self.active.pop(pos)
                if self.active:
                    pos %= len(self.active)
                else:
                    self.total = k + 1
            else:
                pos = (pos + 1) % len(self.active)
            k += 1
            yield case


def cmd_run(opt: argparse.Namespace) -> int:
    check_mode(opt)
    if opt.big_iters < 0:
        die("--big-iters must be >= 0")
    jobs = resolve_jobs(opt.jobs)
    workdir = Path.cwd()
    progs, sources = prepare(opt, workdir, {"main", "judge"})
    gens = select_gens(progs, opt.gens, big=False)
    run_dir = work_root(workdir)
    base = opt.seed if opt.seed is not None else int.from_bytes(os.urandom(8), "little")
    start = time.monotonic()
    deadline = start + opt.time if opt.time else math.inf
    # -n / -t を書かなかったときは、STRESS_BIG の生成器を 1 つにつき --big-iters 回で止める
    big_limit = opt.big_iters if opt.iters is None and not opt.time else None
    iters = opt.iters if opt.iters is not None else 1000
    limited = {n for n in gens if n in progs.big} if big_limit is not None else set()
    if limited and len(limited) == len(gens):
        iters = min(iters, big_limit * len(gens))
    stream = CaseStream(gens, limited, big_limit, iters if not opt.time else None, base, opt.max_cap, deadline)
    mode = mode_name(opt) if mode_name(opt) != "compare" else f"compare/{opt.cmp}"
    Out.log(Out.dim(f"mode={mode} gens={','.join(gens)} base_seed={base} tl={opt.tl}s"
                    + (f" jobs={jobs}" if jobs > 1 else "")))

    slowest = 0.0
    most_queries: tuple[int, int] | None = None
    n_done = 0
    last_status = 0.0

    def on_result(idx: int, case: Case, v: Verdict) -> None:
        nonlocal slowest, most_queries, n_done, last_status
        slowest = max(slowest, v.elapsed)
        if v.queries and (most_queries is None or v.queries[0] > most_queries[0]):
            most_queries = v.queries
        n_done += 1
        now = time.monotonic()
        if now - last_status > 0.1:
            prog = f"{n_done}/{stream.total}" if stream.total else f"{n_done}"
            Out.status(f"[{prog}] cap={case.cap} OK  max {slowest * 1000:.0f}ms{fmt_queries(most_queries)}"
                       f"  {now - start:.1f}s")
            last_status = now

    runner = Runner(run_dir, jobs, lambda case, d: run_case(progs, case, d, opt))
    try:
        found = runner.search(stream, lambda v: v.kind != "OK", on_result)
        if found is None:
            Out.clear_status()
            Out.log(Out.green("OK") + f"  {n_done} cases  max {slowest * 1000:.0f}ms{fmt_queries(most_queries)}"
                    f"  {time.monotonic() - start:.1f}s"
                    + Out.dim(f"  base_seed={base}"))
            return EXIT_OK
        idx, case, v, d = found

        if v.kind in TOOL_FAILURES:
            Out.log(Out.red(v.kind) + f"  {v.detail}")
            Out.log(Out.dim(f"case={case.id}"))
            return EXIT_ERROR

        if opt.shrink and (case.cap or 1) > 1:
            snapshot = workdir / ".stress" / "run_found"
            if snapshot.exists():
                shutil.rmtree(snapshot)
            shutil.copytree(d, snapshot)
            runner.release(d)
            Out.log(Out.dim(f"{v.kind} at case={case.id}; shrinking (Ctrl+C で縮小をやめて保存)"))
            shrunk, tool_failures = shrink(runner, case, opt, time.monotonic() + opt.shrink_time)
            Out.clear_status()
            if tool_failures:
                first_case, first_v = tool_failures[0]
                Out.log(Out.yellow("warning: ") + f"縮小中に道具の側の失敗が {len(tool_failures)} 回出た（そのケースは飛ばした）")
                Out.log(f"  {first_v.kind}  {first_v.detail}")
                Out.log(Out.dim(f"  case={first_case.id}  ->  st repro {first_case.id}" + judge_flags(opt)))
            if shrunk:
                Out.log(Out.dim(f"shrunk: cap {case.cap} -> {shrunk[0].cap}"))
                case, v, d = shrunk
            else:
                if d.exists():
                    shutil.rmtree(d)
                shutil.copytree(snapshot, d)
            shutil.rmtree(snapshot, ignore_errors=True)

        dest = save_failure(workdir, d, case, v, opt, sources)
        meta = json.loads((dest / "meta.json").read_text())
        show_failure(dest, v, case.id, meta["repro"], workdir)
        if opt.to_in:
            copy_to_in(workdir, dest / "in.txt")
        Out.log(Out.dim(f"found after {idx + 1} cases ({time.monotonic() - start:.1f}s)"))
        return EXIT_FOUND
    finally:
        runner.close()


def cmd_repro(opt: argparse.Namespace) -> int:
    check_mode(opt)
    workdir = Path.cwd()
    progs, sources = prepare(opt, workdir, {"main", "judge"})
    case = Case.parse(opt.case, progs.gens)
    run_dir = work_root(workdir)
    v = run_case(progs, case, run_dir, opt)
    if v.kind == "OK":
        Out.log(Out.green("OK") + f"  case={case.id} (再現しなかった)")
        Out.log(Out.dim("input: .stress/run/in.txt"))
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
    tmp = work_root(workdir)
    worst = 0.0
    bad_count = 0
    for k, case in enumerate(make_cases(opt, progs)):
        fresh_dir(tmp)
        if (bad := generate(progs, case, tmp, opt)) is not None:
            die(bad.detail)
        label = f"{case.name}-{k % opt.count + 1:02d}"
        shutil.copy2(tmp / "in.txt", benchdir / f"{label}.txt")
        Out.status(f"running {label} ...")
        r = run(progs.main, tmp / "in.txt", tmp / "out_main.txt", tmp / "stderr_main.txt", opt.tl, tmp,
                output_mb=opt.output_limit)
        kb = (tmp / "in.txt").stat().st_size / 1024
        if r.status == "OK":
            worst = max(worst, r.elapsed)
            ms = r.elapsed * 1000
            mark = Out.yellow(f"{ms:7.0f}ms") if r.elapsed > opt.tl * 0.8 else f"{ms:7.0f}ms"
            Out.log(f"{label:<20} {mark}  " + Out.dim(f"input {kb:.0f}KB  case={case.id}"))
        else:
            bad_count += 1
            what = {"TLE": f"TLE (> {opt.tl}s)", "OLE": f"OLE (> {opt.output_limit:g} MB)"}.get(
                r.status, f"RE ({describe_code(r.code)})")
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
    shm = _shm_dir(Path.cwd())
    if shm is not None and shm.exists():
        shutil.rmtree(shm)
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
    p.add_argument("-c", "--checker", metavar="SRC", nargs="?", const="checker.cpp",
                   help="checker モード。SRC を省略すると checker.cpp（例: -c / -c foo.cpp）")
    p.add_argument("--ref", action="store_true", help="checker モードでも愚直解を走らせ、出力を第 3 引数で渡す")
    p.add_argument("-i", "--interactor", metavar="SRC", nargs="?", const="interactor.cpp",
                   help="インタラクティブ。SRC を省略すると interactor.cpp（例: -i / -i foo.cpp）。愚直解は使わない")
    p.add_argument("--tl", type=float, default=2.0, help="main の制限時間 [s] (default: 2)")
    p.add_argument("--ref-tl", type=float, default=10.0, help="愚直解の制限時間 [s] (default: 10)")
    p.add_argument("--checker-tl", type=float, default=10.0, help="checker / interactor の制限時間 [s] (default: 10)")
    add_output_limit(p)
    p.add_argument("--cmp", default=None, help="比較方法: token (default) / exact / float[:EPS]")
    p.add_argument("--no-to-in", dest="to_in", action="store_false",
                   help="失敗した入力を in.txt にコピーしない（既定ではコピーする）")


def add_output_limit(p: argparse.ArgumentParser) -> None:
    p.add_argument("--output-limit", type=float, default=64.0, metavar="MB",
                   help="main と愚直解が書ける出力（stdout と stderr それぞれ）の上限 [MB] (default: 64)")


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
    p.add_argument("-j", "--jobs", type=int, nargs="?", const=0, default=None, metavar="N",
                   help="N 本並列に回す。数を省くか 0 ならコア数 (default: 環境変数 STRESS_JOBS、無ければ 1)")
    p.add_argument("--no-shrink", dest="shrink", action="store_false", help="失敗後の縮小をしない")
    p.add_argument("--shrink-tries", type=int, default=200, help="縮小で cap 1 つあたりに試す回数 (default: 200)")
    p.add_argument("--shrink-time", type=float, default=20.0, help="縮小にかける時間の上限 [s] (default: 20)")

    p = p_repro = sub.add_parser("repro", help="SEED:CAP:NAME のケースを 1 つ再現する")
    add_common(p)
    p.add_argument("case", nargs="?", help="SEED:CAP:NAME（失敗時に表示されるもの）")

    p = sub.add_parser("make", help="入力ファイルだけ書き出す")
    add_build(p)
    add_cases(p, 5)
    p.add_argument("-o", "--out", default="cases", help="出力先 (default: cases/)")

    p = sub.add_parser("bench", help="生成した入力で main の実行時間を測る")
    add_build(p)
    add_cases(p, 3)
    p.add_argument("--main", default="main.cpp", help="解答 (default: main.cpp)")
    p.add_argument("--tl", type=float, default=2.0, help="制限時間 [s] (default: 2)")
    add_output_limit(p)

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

    opt = ap.parse_args(argv)
    if opt.command == "repro" and opt.case is None:
        # st repro -c SEED:CAP:NAME と書くと、-c がケース ID を SRC として取ってしまう
        for attr, default in (("checker", "checker.cpp"), ("interactor", "interactor.cpp")):
            v = getattr(opt, attr)
            if v and CASE_ID_RE.match(v):
                opt.case = v
                setattr(opt, attr, default)
                break
        else:
            p_repro.error("the following arguments are required: case")
    return opt


# gen と interactor に渡すファイル名。子プロセスは作業ディレクトリ（ケースごとの d）で動くので相対パスで足りる。
# ケースごとに環境変数の辞書を作って渡すと、起動のたびに全部の環境変数を変換し直して遅いので、最初に 1 回だけ設定する。
CHILD_ENV = {"STRESS_TAGS": "tags.txt", "STRESS_REPORT": "report_interactor.txt"}


def main(argv: list[str]) -> int:
    opt = parse(argv)
    os.environ.update(CHILD_ENV)
    handler = {"run": cmd_run, "repro": cmd_repro, "make": cmd_make, "bench": cmd_bench,
               "last": cmd_last, "restore": cmd_restore, "clean": cmd_clean, "init": cmd_init}
    try:
        return handler[opt.command](opt)
    except KeyboardInterrupt:
        Out.log(Out.yellow("interrupted"))
        return 130


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
