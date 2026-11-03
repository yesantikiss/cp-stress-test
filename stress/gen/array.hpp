#pragma once

// 配列: vec / sorted / distinct / perm / partition

#include "core.hpp"

namespace stress {

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

    std::vector<ll> sorted(ll n, ll lo, ll hi) {
        auto a = vec(n, lo, hi);
        std::sort(a.begin(), a.end());
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
};

}  // namespace stress
