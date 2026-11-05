#pragma once

// ランダムテスト用ジェネレータ。gen.cpp ではこれだけ include すればいい。
//
//   #include "stress/gen.hpp"
//   void gen(Gen& g) {
//       int n = g.size(1, 10);
//       g.out(n);
//       g.out(g.vec(n, 1, 100));
//   }
//
// 実行: ./gen <seed> [cap]
//   seed が同じなら、どの処理系でも同じ入力を出す（std の分布は使わない）。
//   cap は g.size() の上限。ドライバが小さい値から順に渡す。
//
// 中身はファイルごとに分かれている:
//   gen/core.hpp    乱数、i / uniform / size / real / coin / choice / shuffle、Range、tag
//   gen/array.hpp   vec / sorted / distinct / perm / partition
//   gen/graph.hpp   tree / parents / graph / digraph / dag / bipartite
//   gen/big.hpp     big / big_range、digits::*
//   gen/string.hpp  str / mutate / strs / grid
//   gen/output.hpp  out / out_lines / raw

#include "gen/array.hpp"
#include "gen/big.hpp"
#include "gen/core.hpp"
#include "gen/graph.hpp"
#include "gen/output.hpp"
#include "gen/string.hpp"

namespace stress {

class Gen : public Core,
            public ArrayOps<Gen>,
            public GraphOps<Gen>,
            public BigOps<Gen>,
            public StringOps<Gen>,
            public Output {
  public:
    using Core::Core;
};

}  // namespace stress

namespace digits = stress::digits;
using stress::Edges;
using stress::Gen;
using stress::DigraphShape;
using stress::GraphShape;
using stress::Reach;
using stress::Range;
using stress::StrShape;
using stress::TreeShape;

// ---------- 名前付きの生成器 ----------
//
//   STRESS_GEN(random)   { ... }
//   STRESS_GEN(all_same) { ... }
//
// 1 つの gen.cpp に何個でも書ける。ドライバは順番に混ぜて回す。
// void gen(Gen& g) と書いた場合は "gen" という名前の 1 個として扱う。

namespace stress::detail {

struct GenEntry {
    const char* name;
    void (*fn)(Gen&);
    bool big = false;  // STRESS_BIG: st では使わず、make / bench 用
};

inline std::vector<GenEntry>& registry() {
    static std::vector<GenEntry> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, void (*fn)(Gen&), bool big) { registry().push_back({name, fn, big}); }
};

}  // namespace stress::detail

#define STRESS_GEN_IMPL(name, big)                                                              \
    static void stress_gen_##name(::stress::Gen& g);                                            \
    static const ::stress::detail::Registrar stress_registrar_##name(#name, stress_gen_##name, big); \
    static void stress_gen_##name([[maybe_unused]] ::stress::Gen& g)

// 普通の生成器。st で順番に使われる。
#define STRESS_GEN(name) STRESS_GEN_IMPL(name, false)
// 最大ケースなど大きい入力用。st では使わず（愚直解が終わらないので）、st make / st bench で使う。
#define STRESS_BIG(name) STRESS_GEN_IMPL(name, true)

#ifndef STRESS_NO_MAIN
// 書かれていなければリンク時に nullptr になる。
__attribute__((weak)) void gen(stress::Gen& g);

// ./gen <seed> [cap] [name]   1 ケース出力（name 省略時は先頭の生成器）
// ./gen --list                 生成器の名前を 1 行ずつ出す（STRESS_BIG は名前の後に " big"）
int main(int argc, char** argv) {
    auto& gens = stress::detail::registry();
    if (gen != nullptr) gens.insert(gens.begin(), {"gen", gen, false});
    if (gens.empty()) {
        std::cerr << "[gen error] no generator: write void gen(Gen& g) or STRESS_GEN(name)\n";
        return 3;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--list") {
        for (auto& e : gens) std::cout << e.name << (e.big ? " big" : "") << '\n';
        return 0;
    }
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <seed> [cap] [name]\n       " << argv[0] << " --list\n";
        return 2;
    }
    const std::uint64_t seed = std::strtoull(argv[1], nullptr, 10);
    long long cap = std::numeric_limits<long long>::max();
    if (argc >= 3 && std::string_view(argv[2]) != "-") cap = std::strtoll(argv[2], nullptr, 10);
    const stress::detail::GenEntry* entry = &gens.front();
    if (argc >= 4) {
        entry = nullptr;
        for (auto& e : gens)
            if (std::string_view(e.name) == argv[3]) entry = &e;
        if (!entry) {
            std::cerr << "[gen error] unknown generator: " << argv[3] << '\n';
            return 3;
        }
    }
    stress::Gen g(seed, cap);
    entry->fn(g);
    std::fwrite(g.buffer().data(), 1, g.buffer().size(), stdout);
    // st が環境変数 STRESS_TAGS にファイル名を渡したときだけ、タグを "タグ<TAB>回数" で書く。
    if (const char* path = std::getenv("STRESS_TAGS"); path && *path) {
        if (std::FILE* f = std::fopen(path, "w")) {
            for (auto& [t, c] : g.tags()) std::fprintf(f, "%s\t%lld\n", t.c_str(), c);
            std::fclose(f);
        }
    }
    return 0;
}
#endif
