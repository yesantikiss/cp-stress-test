#pragma once
// 入力の読み残しを調べる。プログラムが終わるとき（main の return か exit）に、
// stdin に空白以外の文字が残っていたら stderr に警告を出す。止めたり終了コードを変えたりはしない。
//
// main.cpp には書かず、コンパイル時に差し込んで使う（提出するコードには入らない）。
//   g++ ... -include pch_acl.hpp -include ~/cp-stress-test/stress/leftover.hpp main.cpp
// PCH の -include より後ろに置く（前に置くと PCH が使われなくなる）。st は main と愚直解に自動で差し込む。
//
// stdin がシークできる通常のファイルのとき（./a.out < in.txt）だけ調べる。パイプや端末（st -i の対話、手打ち）では何もしない。
// ストリームからは何も読まず、ファイルの読み取り位置から cin と stdio のバッファに残っている分を引いて
// 「どこまで読んだか」を求め、その位置より後ろに空白以外の文字が残っているかを見る。
// そのため read(0, ...) で自前のバッファに読む高速入力では、読み残しを見逃すことがある。

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace stress_leftover {

// cin が自前のバッファ（sync_with_stdio(false) のとき）にまだ持っている文字数
struct BufPeek : std::streambuf {
    static long pending(std::streambuf* b) {
        if (!b) return 0;
        auto* p = static_cast<BufPeek*>(b);  // protected の gptr / egptr を読むだけ
        return p->gptr() && p->egptr() > p->gptr() ? static_cast<long>(p->egptr() - p->gptr()) : 0;
    }
};

inline long stdio_pending() {
#if defined(__GLIBC__)
    // glibc の FILE が読んだがまだ使っていない分（scanf / getchar の先読み）
    if (stdin->_IO_read_ptr && stdin->_IO_read_end > stdin->_IO_read_ptr)
        return static_cast<long>(stdin->_IO_read_end - stdin->_IO_read_ptr);
#endif
    return 0;
}

inline bool space(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\v' || c == '\f'; }

inline void check() {
    off_t pos = lseek(0, 0, SEEK_CUR);
    if (pos < 0) return;
    off_t size = lseek(0, 0, SEEK_END);
    if (size < 0) return;
    lseek(0, pos, SEEK_SET);
    long pending = stdio_pending();
    // sync_with_stdio(true)（既定）の cin は stdio を通して読むので、cin 自身のバッファは空のまま
    if (dynamic_cast<std::filebuf*>(std::cin.rdbuf())) pending += BufPeek::pending(std::cin.rdbuf());
    off_t used = pos - static_cast<off_t>(pending);
    if (used < 0) used = 0;
    if (used >= size) return;

    char buf[1 << 16];
    off_t first = -1;
    long lines = 1;
    long pending_nl = 0;  // 最後の非空白文字より後ろの改行（末尾の空行は数えない）
    std::string preview;
    int tokens = 0;
    bool in_tok = false;
    bool preview_more = false;
    for (off_t off = used; off < size;) {
        size_t want = sizeof buf;
        if (size - off < static_cast<off_t>(want)) want = static_cast<size_t>(size - off);
        ssize_t r = pread(0, buf, want, off);
        if (r <= 0) break;
        for (ssize_t k = 0; k < r; ++k) {
            const char c = buf[k];
            if (first < 0) {
                if (space(c)) continue;
                first = off + k;
            }
            if (space(c)) {
                if (c == '\n') ++pending_nl;
                in_tok = false;
                continue;
            }
            lines += pending_nl;
            pending_nl = 0;
            if (!in_tok) {
                if (tokens < 5 && preview.size() < 60) {
                    if (!preview.empty()) preview += ' ';
                    if (preview.size() <= 80) preview += c;
                    ++tokens;
                    in_tok = true;
                } else if (tokens == 5) {
                    preview_more = true;
                }
            } else if (preview.size() <= 80) {
                preview += c;
            }
        }
        off += r;
    }
    if (first < 0) return;
    if (preview_more) preview += " ...";
    if (preview.size() > 80) preview = preview.substr(0, 77) + "...";

    long line = 1;
    for (off_t off = 0; off < first;) {
        size_t want = sizeof buf;
        if (first - off < static_cast<off_t>(want)) want = static_cast<size_t>(first - off);
        ssize_t r = pread(0, buf, want, off);
        if (r <= 0) break;
        for (ssize_t k = 0; k < r; ++k) line += buf[k] == '\n';
        off += r;
    }

    std::cout.flush();
    std::fprintf(stderr, "[leftover] 入力を読み残しています: %ld 行目から（残り %ld 行）: %s\n", line, lines,
                 preview.c_str());
#ifdef STRESS_LEFTOVER_NAME
#define STRESS_LEFTOVER_STR2(x) #x
#define STRESS_LEFTOVER_STR(x) STRESS_LEFTOVER_STR2(x)
    // st が付けるマクロ。ケースのディレクトリに結果を書き、st がまとめて報告する
    if (std::FILE* f = std::fopen("leftover_" STRESS_LEFTOVER_STR(STRESS_LEFTOVER_NAME) ".txt", "w")) {
        std::fprintf(f, "%ld\t%ld\t%s\n", line, lines, preview.c_str());
        std::fclose(f);
    }
#endif
}

struct Guard {
    ~Guard() { check(); }
};
// <iostream> より後に作られるので、cout / cin が片付けられる前に調べられる
inline Guard guard;

}  // namespace stress_leftover
