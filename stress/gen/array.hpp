#pragma once

// 配列: vec / seq / sorted / distinct / perm / partition

#include "core.hpp"

namespace stress {

enum class SeqShape { mixed, random, same, few, asc, desc, edge };

struct SeqOpt {
    SeqShape shape = SeqShape::mixed;
};

struct SortedOpt {
    bool strict = false;  // true なら狭義（同じ値なし）
    bool desc = false;    // true なら降順
};

template <class G>
class ArrayOps {
    G& self() { return static_cast<G&>(*this); }

  public:

    std::vector<ll> vec(ll n, ll lo, ll hi) { return vec(n, Range{lo, hi}); }
    std::vector<ll> vec(ll n, Range r) {
        std::vector<ll> a(n);
        for (auto& x : a) x = self().i(r.lo, r.hi);
        return a;
    }
    template <class F, std::enable_if_t<std::is_invocable_v<F&>, int> = 0>
    std::vector<ll> vec(ll n, F&& f) {
        std::vector<ll> a(n);
        for (auto& x : a) x = static_cast<ll>(f());
        return a;
    }

    // 形を混ぜた配列。mixed で選んだ形は "seq=形" のタグにする。
    std::vector<ll> seq(ll n, ll lo, ll hi, SeqOpt opt = {}) { return seq(n, Range{lo, hi}, opt); }
    std::vector<ll> seq(ll n, Range r, SeqOpt opt = {}) {
        if (r.lo > r.hi) gen_error("seq: lo > hi (" + std::to_string(r.lo) + ", " + std::to_string(r.hi) + ")");
        SeqShape shape = opt.shape;
        if (shape == SeqShape::mixed) {
            static constexpr SeqShape all[] = {SeqShape::random, SeqShape::random, SeqShape::same, SeqShape::few,
                                               SeqShape::asc,    SeqShape::desc,   SeqShape::edge};
            shape = all[self().uniform(0, std::size(all) - 1)];
            self().tag(std::string("seq=") + shape_name(shape));
        }
        switch (shape) {
            case SeqShape::same: return std::vector<ll>(std::max(n, 0LL), self().i(r.lo, r.hi));
            case SeqShape::few: {
                const ll k = self().uniform(2, 3);
                std::vector<ll> vals(k);
                for (auto& v : vals) v = self().i(r.lo, r.hi);
                return vec(n, [&] { return self().choice(vals); });
            }
            case SeqShape::asc: return sorted(n, r.lo, r.hi);
            case SeqShape::desc: return sorted(n, r.lo, r.hi, {.desc = true});
            case SeqShape::edge: return vec(n, [&] { return self().coin() ? r.lo : r.hi; });
            default: return vec(n, r);
        }
    }

    // 単調な列。既定は広義増加。strict なら [lo, hi] から相異なる n 個を並べる。
    std::vector<ll> sorted(ll n, ll lo, ll hi, SortedOpt opt = {}) {
        auto a = opt.strict ? distinct(n, lo, hi) : vec(n, lo, hi);
        if (opt.desc) std::sort(a.begin(), a.end(), std::greater<ll>());
        else std::sort(a.begin(), a.end());
        return a;
    }

    // [lo, hi] から相異なる n 個（順番はランダム）。
    std::vector<ll> distinct(ll n, ll lo, ll hi) {
        const unsigned __int128 span = static_cast<unsigned __int128>(hi - lo) + 1;
        if (lo > hi || span < static_cast<unsigned __int128>(n))
            gen_error("distinct: range [" + std::to_string(lo) + ", " + std::to_string(hi) + "] has fewer than " + std::to_string(n) + " values");
        std::vector<ll> a;
        a.reserve(n);
        if (span <= static_cast<unsigned __int128>(4 * n + 16)) {
            for (ll x = lo; ; ++x) {
                a.push_back(x);
                if (x == hi) break;
            }
            // 部分シャッフル
            for (ll k = 0; k < n; ++k) std::swap(a[k], a[self().uniform(k, static_cast<ll>(a.size()) - 1)]);
            a.resize(n);
        } else {
            std::unordered_set<ll> seen;
            while (static_cast<ll>(a.size()) < n) {
                ll x = self().uniform(lo, hi);
                if (seen.insert(x).second) a.push_back(x);
            }
        }
        return a;
    }

    // 順列。one_indexed なら 1..n、そうでなければ 0..n-1。
    std::vector<ll> perm(ll n) {
        std::vector<ll> p(n);
        std::iota(p.begin(), p.end(), self().base());
        self().shuffle(p);
        return p;
    }

    // 和が sum になる k 個の非負整数。各要素に min_value を下駄として足せる。
    std::vector<ll> partition(ll sum, ll k, ll min_value = 0) {
        if (k <= 0) gen_error("partition: k must be positive");
        const ll rest = sum - min_value * k;
        if (rest < 0) gen_error("partition: sum is too small");
        std::vector<ll> cut(k - 1);
        for (auto& c : cut) c = self().uniform(0, rest);
        std::sort(cut.begin(), cut.end());
        std::vector<ll> a(k);
        ll prev = 0;
        for (ll j = 0; j < k - 1; ++j) {
            a[j] = cut[j] - prev + min_value;
            prev = cut[j];
        }
        a[k - 1] = rest - prev + min_value;
        return a;
    }

  private:
    static const char* shape_name(SeqShape s) {
        switch (s) {
            case SeqShape::random: return "random";
            case SeqShape::same: return "same";
            case SeqShape::few: return "few";
            case SeqShape::asc: return "asc";
            case SeqShape::desc: return "desc";
            case SeqShape::edge: return "edge";
            default: return "mixed";
        }
    }
};

}  // namespace stress
