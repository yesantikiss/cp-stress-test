#pragma once
// 入力の読み残しを調べる。プログラムが終わるとき（main の return か exit）に、
// stdin に空白以外の文字が残っていたら stderr に警告を出す。止めたり終了コードを変えたりはしない。
//
// main.cpp には書かず、コンパイル時に差し込んで使う（提出するコードには入らない）。
//   g++ ... -include pch_acl.hpp -include ~/cp-stress-test/stress/leftover.hpp main.cpp
// PCH の -include より後ろに置く（前に置くと PCH が使われなくなる）。st は main と愚直解に自動で差し込む。
//
// stdin が通常のファイルのとき（./a.out < in.txt）だけ調べる。パイプや端末（st -i の対話、手打ち）では何もしない。
// ストリームからは何も読まず、ファイルを先頭から読み直して「どこまで読んだか」と比べる。
// どこまで読んだかは、ファイルの読み取り位置から cin と stdio のバッファに残っている分を引いて求める。
// そのため read(0, ...) で自前のバッファに読む高速入力では、読み残しを見逃すことがある。

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
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
    struct stat st;
    if (fstat(0, &st) != 0 || !S_ISREG(st.st_mode)) return;
    off_t pos = lseek(0, 0, SEEK_CUR);
    if (pos < 0) return;
    long pending = stdio_pending();
    // sync_with_stdio(true)（既定）の cin は stdio を通して読むので、cin 自身のバッファは空のまま
    if (dynamic_cast<std::filebuf*>(std::cin.rdbuf())) pending += BufPeek::pending(std::cin.rdbuf());
    long used = static_cast<long>(pos) - pending;
    if (used < 0) used = 0;

    std::string text;
    char buf[1 << 16];
    for (off_t off = 0;;) {
        ssize_t r = pread(0, buf, sizeof buf, off);
        if (r <= 0) break;
        text.append(buf, static_cast<size_t>(r));
        off += r;
    }
    size_t first = static_cast<size_t>(used);
    while (first < text.size() && space(text[first])) ++first;
    if (first >= text.size()) return;

    long line = 1;
    for (size_t i = 0; i < first; ++i) line += text[i] == '\n';
    size_t last = text.size();
    while (last > first && space(text[last - 1])) --last;
    long lines = 1;
    for (size_t i = first; i < last; ++i) lines += text[i] == '\n';

    // 先頭の数トークンを見せる
    std::string preview;
    int tokens = 0;
    for (size_t i = first; i < last && tokens < 5 && preview.size() < 60;) {
        size_t j = i;
        while (j < last && !space(text[j])) ++j;
        if (!preview.empty()) preview += ' ';
        preview.append(text, i, j - i);
        ++tokens;
        i = j;
        while (i < last && space(text[i])) ++i;
        if (tokens == 5 && i < last) preview += " ...";
    }
    if (preview.size() > 80) preview = preview.substr(0, 77) + "...";

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
