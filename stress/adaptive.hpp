#pragma once

// 適応的（adaptive）な interactor 用。隠し情報を 1 つに決めず、
// 「これまでの返事と矛盾しない候補」を全部持っておき、質問のたびに
// 候補が一番多く残る返事を選ぶ。
//
//   #include "stress/interact.hpp"
//   #include "stress/adaptive.hpp"
//   Adaptive ad(cand::permutations(n), seed);       // ありうる隠し情報を全部渡す
//   bool r = ad.reply([&](const auto& p) {          // 「p が答えだったら何と返すか」
//       return p[a - 1] < p[b - 1];
//   });
//   ad.ensure_all([&](const auto& p) { return p == ans; }, "not determined");
//
// 候補を並べられない問題では AdaptiveState（状態 + 点数）を使う。

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "reader.hpp"

namespace stress {

// 返事の選び方。
//   max_remaining: 候補が一番多く残る返事（同数なら seed で選ぶ）
//   fixed:         最初に候補を 1 つ seed で選び、それに従って返す（普通のジャッジと同じ）
enum class Strategy { max_remaining, fixed };

namespace adaptive_detail {

inline std::uint64_t splitmix64(std::uint64_t& x) {
    std::uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

template <class T, class = void>
struct is_iterable : std::false_type {};
template <class T>
struct is_iterable<T, std::void_t<decltype(std::begin(std::declval<const T&>())),
                                  decltype(std::end(std::declval<const T&>()))>> : std::true_type {};

template <class T>
struct is_pair : std::false_type {};
template <class A, class B>
struct is_pair<std::pair<A, B>> : std::true_type {};

// WA のメッセージに候補を出すための文字列化。
template <class T>
void write(std::ostringstream& os, const T& x) {
    if constexpr (std::is_same_v<T, bool>) {
        os << (x ? "true" : "false");
    } else if constexpr (std::is_same_v<T, char>) {
        os << x;
    } else if constexpr (std::is_arithmetic_v<T> && sizeof(T) == 1) {
        os << static_cast<int>(x);
    } else if constexpr (std::is_arithmetic_v<T>) {
        os << x;
    } else if constexpr (std::is_convertible_v<const T&, std::string>) {
        os << std::string(x);
    } else if constexpr (is_pair<T>::value) {
        os << '(';
        write(os, x.first);
        os << ", ";
        write(os, x.second);
        os << ')';
    } else if constexpr (is_iterable<T>::value) {
        os << '[';
        bool first = true;
        for (const auto& e : x) {
            if (!first) os << ", ";
            first = false;
            write(os, e);
        }
        os << ']';
    } else {
        os << "(?)";
    }
}

template <class T>
std::string to_text(const T& x) {
    std::ostringstream os;
    write(os, x);
    std::string s = os.str();
    if (s.size() > 300) s = s.substr(0, 300) + "...";
    return s;
}

[[noreturn]] inline void fail(const std::string& msg) { judge_exit(kJudgeFail, "interactor failure: " + msg); }

}  // namespace adaptive_detail

// ---------- 候補を全部持つ版 ----------

template <class T>
class Adaptive {
  public:
    Adaptive(std::vector<T> candidates, std::uint64_t seed, Strategy strategy = Strategy::max_remaining)
        : cands_(std::move(candidates)), rng_(seed), strategy_(strategy) {
        if (cands_.empty()) adaptive_detail::fail("Adaptive: no candidates");
        if (strategy_ == Strategy::fixed) hidden_ = pick(cands_.size());
    }

    // f(候補) = その候補が隠し情報だったときの返事。
    // 選んだ返事を返し、それと矛盾する候補を捨てる。
    // 返事の型は == と < で比べられるもの（bool, int, string, pair, vector など）。
    template <class F>
    auto reply(F&& f) {
        using R = std::decay_t<decltype(f(cands_[0]))>;
        std::vector<R> res;
        res.reserve(cands_.size());
        for (const auto& c : cands_) res.push_back(f(c));

        R chosen;
        if (strategy_ == Strategy::fixed) {
            chosen = res[hidden_];
        } else {
            // 返事の種類ごとに数える。種類は少ないことが多いので、まず線形に探す
            std::vector<std::pair<R, std::size_t>> count;
            std::map<R, std::size_t> many;
            for (const auto& r : res) {
                if (many.empty()) {
                    auto p = std::find_if(count.begin(), count.end(), [&](const auto& e) { return e.first == r; });
                    if (p != count.end()) {
                        ++p->second;
                        continue;
                    }
                    if (count.size() < 16) {
                        count.emplace_back(r, 1);
                        continue;
                    }
                    for (auto& [k, c] : count) many[k] = c;
                }
                ++many[r];
            }
            if (!many.empty()) count.assign(many.begin(), many.end());
            std::sort(count.begin(), count.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            std::size_t best = 0;
            for (const auto& [r, k] : count) best = std::max(best, k);
            std::vector<const R*> tied;
            for (const auto& [r, k] : count)
                if (k == best) tied.push_back(&r);
            chosen = *tied[pick(tied.size())];
        }

        std::size_t w = 0, new_hidden = 0;
        for (std::size_t i = 0; i < cands_.size(); ++i) {
            if (!(res[i] == chosen)) continue;
            if (i == hidden_) new_hidden = w;
            if (w != i) cands_[w] = std::move(cands_[i]);
            ++w;
        }
        cands_.resize(w);
        hidden_ = new_hidden;
        return chosen;
    }

    // 残っている候補すべてで pred が成り立たなければ WA。
    // 「答えが 1 つに決まっていて、それを当てた」は pred = (候補 == 答え) で書ける。
    template <class P>
    void ensure_all(P&& pred, const std::string& msg) const {
        for (const auto& c : cands_) {
            if (!pred(c))
                judge_exit(kWrong, "WA: " + msg + " (hidden could be " + adaptive_detail::to_text(c) + ", " +
                                       std::to_string(cands_.size()) + " candidates left)");
        }
    }

    std::size_t size() const { return cands_.size(); }
    bool determined() const { return cands_.size() == 1; }
    const std::vector<T>& candidates() const { return cands_; }
    // 候補が 1 つに決まっているときのその候補。決まっていなければ interactor の失敗。
    const T& only() const {
        if (cands_.size() != 1)
            adaptive_detail::fail("Adaptive::only(): " + std::to_string(cands_.size()) + " candidates left");
        return cands_[0];
    }

  private:
    std::vector<T> cands_;
    std::uint64_t rng_;
    Strategy strategy_;
    std::size_t hidden_ = 0;

    std::size_t pick(std::size_t n) { return adaptive_detail::splitmix64(rng_) % n; }
};

// ---------- 状態と点数を渡す版 ----------
//
// 候補を並べられない問題用。返事の候補ごとに状態のコピーへ apply し、
// score が一番大きい返事を選ぶ（同点なら seed で選ぶ）。
//   apply(S&, const R&): 状態を更新する。bool を返すなら false = その返事は矛盾するので選ばない
//   score(const S&):     大きいほど解答に不利（例: 残っている候補の数）

template <class S>
class AdaptiveState {
  public:
    AdaptiveState(S init, std::uint64_t seed) : state_(std::move(init)), rng_(seed) {}

    template <class R, class Apply, class Score>
    R reply(const std::vector<R>& options, Apply&& apply, Score&& score) {
        using V = std::decay_t<decltype(score(state_))>;
        std::vector<std::pair<std::size_t, S>> best;
        V best_score{};
        for (std::size_t i = 0; i < options.size(); ++i) {
            S s = state_;
            if constexpr (std::is_same_v<decltype(apply(s, options[i])), bool>) {
                if (!apply(s, options[i])) continue;
            } else {
                apply(s, options[i]);
            }
            V v = score(s);
            if (best.empty() || best_score < v) {
                best.clear();
                best_score = v;
            } else if (v < best_score) {
                continue;
            }
            best.emplace_back(i, std::move(s));
        }
        if (best.empty()) adaptive_detail::fail("AdaptiveState: every reply contradicts the state");
        auto& [i, s] = best[adaptive_detail::splitmix64(rng_) % best.size()];
        state_ = std::move(s);
        return options[i];
    }
    template <class R, class Apply, class Score>
    R reply(std::initializer_list<R> options, Apply&& apply, Score&& score) {
        return reply(std::vector<R>(options), apply, score);
    }

    S& state() { return state_; }
    const S& state() const { return state_; }

  private:
    S state_;
    std::uint64_t rng_;
};

// ---------- 候補の全列挙 ----------
//
// 列はどれも 0 始まりで、値は 1 始まり（permutations(3) の p は p[0], p[1], p[2] で、値は 1..3）。
// 問題の i 番目（1 始まり）は p[i - 1] で読む。
// 個数が 10^7 を超えるときは interactor の失敗にする（候補は 10^6 くらいまでが現実的）。

namespace cand {

inline constexpr long long kMaxCandidates = 10'000'000;

namespace detail {
inline void check_count(long double count, const std::string& what) {
    if (count > kMaxCandidates)
        adaptive_detail::fail(what + ": too many candidates (" + std::to_string(static_cast<long double>(count)) +
                              " > 1e7)");
}
}  // namespace detail

// l..r の整数。
inline std::vector<long long> range(long long l, long long r) {
    if (l > r) adaptive_detail::fail("cand::range: l > r");
    detail::check_count(static_cast<long double>(r) - l + 1, "cand::range");
    std::vector<long long> v;
    v.reserve(r - l + 1);
    for (long long x = l; x <= r; ++x) v.push_back(x);
    return v;
}

// 各要素が l..r の長さ k の列（重複あり）。辞書順。
inline std::vector<std::vector<int>> tuples(int k, int l, int r) {
    if (k < 0 || l > r) adaptive_detail::fail("cand::tuples: bad arguments");
    long double cnt = 1;
    for (int i = 0; i < k; ++i) cnt *= static_cast<long double>(r) - l + 1, detail::check_count(cnt, "cand::tuples");
    std::vector<std::vector<int>> res;
    res.reserve(static_cast<std::size_t>(cnt));
    std::vector<int> cur(k, l);
    while (true) {
        res.push_back(cur);
        int i = k - 1;
        while (i >= 0 && cur[i] == r) cur[i--] = l;
        if (i < 0) break;
        ++cur[i];
    }
    return res;
}

// alphabet の文字を使う長さ k の文字列。alphabet の順で辞書順。
inline std::vector<std::string> strings(int k, const std::string& alphabet) {
    if (alphabet.empty()) adaptive_detail::fail("cand::strings: empty alphabet");
    std::vector<std::string> res;
    for (const auto& t : tuples(k, 0, static_cast<int>(alphabet.size()) - 1)) {
        std::string s(k, ' ');
        for (int i = 0; i < k; ++i) s[i] = alphabet[t[i]];
        res.push_back(std::move(s));
    }
    return res;
}

// 1..n の順列。辞書順。
inline std::vector<std::vector<int>> permutations(int n) {
    if (n < 0) adaptive_detail::fail("cand::permutations: n < 0");
    long double cnt = 1;
    for (int i = 2; i <= n; ++i) cnt *= i, detail::check_count(cnt, "cand::permutations");
    std::vector<int> p(n);
    for (int i = 0; i < n; ++i) p[i] = i + 1;
    std::vector<std::vector<int>> res;
    res.reserve(static_cast<std::size_t>(cnt));
    do res.push_back(p);
    while (std::next_permutation(p.begin(), p.end()));
    return res;
}

// 1..n から k 個選んだ昇順の列。辞書順。
inline std::vector<std::vector<int>> combinations(int n, int k) {
    if (k < 0 || n < 0 || k > n) adaptive_detail::fail("cand::combinations: bad arguments");
    long double cnt = 1;
    for (int i = 0; i < k; ++i) cnt = cnt * (n - i) / (i + 1);
    detail::check_count(cnt, "cand::combinations");
    std::vector<std::vector<int>> res;
    std::vector<int> cur(k);
    for (int i = 0; i < k; ++i) cur[i] = i + 1;
    while (true) {
        res.push_back(cur);
        int i = k - 1;
        while (i >= 0 && cur[i] == n - k + i + 1) --i;
        if (i < 0) break;
        ++cur[i];
        for (int j = i + 1; j < k; ++j) cur[j] = cur[j - 1] + 1;
    }
    return res;
}

// 頂点 1..n のラベル付き木（n^(n-2) 個、n ≤ 8 くらいまで）。
// 1 つの木は辺 (u, v)（u < v）を昇順に並べた列。
inline std::vector<std::vector<std::pair<int, int>>> trees(int n) {
    if (n < 1) adaptive_detail::fail("cand::trees: n < 1");
    if (n == 1) return {{}};
    if (n == 2) return {{{1, 2}}};
    std::vector<std::vector<std::pair<int, int>>> res;
    for (const auto& code : tuples(n - 2, 1, n)) {  // Prüfer 列から木を作る
        std::vector<int> deg(n + 1, 1);
        for (int x : code) ++deg[x];
        std::vector<std::pair<int, int>> es;
        es.reserve(n - 1);
        for (int x : code) {
            int leaf = 1;
            while (deg[leaf] != 1) ++leaf;
            es.emplace_back(std::min(leaf, x), std::max(leaf, x));
            --deg[leaf];
            --deg[x];
        }
        int u = 0, v = 0;
        for (int i = 1; i <= n; ++i)
            if (deg[i] == 1) (u ? v : u) = i;
        es.emplace_back(u, v);
        std::sort(es.begin(), es.end());
        res.push_back(std::move(es));
    }
    return res;
}

}  // namespace cand

}  // namespace stress

using stress::Adaptive;
using stress::AdaptiveState;
using stress::Strategy;
namespace cand = stress::cand;
