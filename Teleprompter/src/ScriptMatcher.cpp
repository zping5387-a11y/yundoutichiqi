// ScriptMatcher.cpp
//
// 精确匹配：识别文本归一化（去掉标点/空白、统一大小写）后，直接在稿件的
// 归一化文本里做严格子串查找，命中即定位。不再做编辑距离 / n-gram 投票这类
// 模糊对齐——宁可这一句不动，也不要把浮层带到错误位置。
//
// 为了适配流式识别的两个固有现象，允许「只截取识别文本的一小段做精确查找」：
//   1) 句中最后一个字可能还没稳定 / 随后被引擎修正  -> 允许去掉尾部 1~3 字
//   2) 句首偶尔吞字或带进上一句的零头              -> 允许去掉头部 1~2 字
//   3) 句中个别同音字识别错（「浮力」→「福利」），整句必然对不上，
//      此时从句尾按 16 字分块，逐块做精确查找，取能对上的那一块
// 所有分支都是「逐字完全一致才算命中」，没有任何相似度/编辑距离放宽。
#include "ScriptMatcher.h"

namespace
{
    constexpr int kMinQuery   = 4;     // 少于 4 个字不定位：太短必然撞车
    constexpr int kMaxQuery   = 120;   // 一次最多看识别文本的最后 120 字
    constexpr int kMaxHeadCut = 2;     // 头部最多允许跳过的字数
    constexpr int kMaxTailCut = 3;     // 尾部最多允许跳过的字数
    constexpr int kBackTol    = 30;    // 允许命中点落在当前位置之前多少字内
    constexpr int kSmallBack  = 25;    // 小于该幅度的回退不跟随（防抖动）
    constexpr int kForwardCap = 3000;  // 正常状态下向前搜索的字数
    constexpr int kJumpGuard  = 400;   // 短句前进超过该距离视为误命中

    struct Hit
    {
        int start = 0;
        int end = 0;
    };

    // 收集 needle 在 [from, to) 内的所有精确出现（to 是命中终点的上界）
    void FindAll(const std::wstring& hay, const std::wstring& needle,
                 int from, int to, std::vector<Hit>& out)
    {
        const int n = (int)needle.size();
        if (n <= 0 || to - from < n) return;

        size_t p = hay.find(needle, (size_t)from);
        int guard = 0;
        while (p != std::wstring::npos && (int)p + n <= to && guard < 64)
        {
            Hit h;
            h.start = (int)p;
            h.end = (int)p + n;
            out.push_back(h);
            p = hay.find(needle, p + 1);
            ++guard;
        }
    }

    // 选位：优先「不倒退且前进最少」的那一处，即文字顺着念下来的位置
    int PickHit(const std::vector<Hit>& hits, int pos, int backTol)
    {
        int best = -1;
        for (int i = 0; i < (int)hits.size(); ++i)
        {
            if (hits[i].end < pos - backTol) continue;
            if (best < 0 || hits[i].end < hits[best].end) best = i;
        }
        return best;
    }
}

bool CScriptMatcher::Feed(const std::wstring& recognized, int& newPos, float& score)
{
    score = 0.0f;
    if (!m_norm || m_norm->empty()) return false;

    const std::wstring q0 = CScriptDoc::NormalizeText(recognized);
    if ((int)q0.size() < kMinQuery) return false;   // 太短：不算失败，等下一版识别

    std::wstring q = q0;
    if ((int)q.size() > kMaxQuery) q = q.substr(q.size() - kMaxQuery);

    const int N = (int)m_norm->size();
    const int Q = (int)q.size();

    // 搜索范围：默认在当前位置附近，长期匹配不上时放开整篇（老师跳读）
    int from = m_pos - 120; if (from < 0) from = 0;
    int to = m_pos + kForwardCap; if (to > N) to = N;
    if (m_failStreak >= 3) { from = 0; to = N; }

    // 按「截断字数总和」由少到多尝试，尽量用最长、最完整的片段去命中
    std::vector<Hit> hits;
    int usedLen = 0;

    for (int totalCut = 0; totalCut <= kMaxHeadCut + kMaxTailCut && hits.empty(); ++totalCut)
    {
        for (int headCut = 0; headCut <= totalCut && headCut <= kMaxHeadCut; ++headCut)
        {
            const int tailCut = totalCut - headCut;
            if (tailCut > kMaxTailCut) continue;

            const int len = Q - headCut - tailCut;
            if (len < kMinQuery) continue;
            if (headCut > 0 && len < 6) continue;   // 掐头后要求更长的片段，避免撞车

            FindAll(*m_norm, q.substr((size_t)headCut, (size_t)len), from, to, hits);
            if (!hits.empty()) { usedLen = len; break; }
        }
    }

    // 整句对不上：多半是句中某个同音字被识别成了别的字（如「浮力」→「福利」），
    // 这时句尾那一段通常仍是逐字准确的。从尾部开始按 16 字分块做精确查找，
    // 找到一块就算定位成功——块内仍然要求逐字完全一致。
    bool byChunk = false;
    if (hits.empty())
    {
        constexpr int kBlock = 16;
        constexpr int kStep  = 8;
        constexpr int kMaxBlocks = 6;

        for (int back = 0; back < Q && back / kStep < kMaxBlocks && hits.empty(); back += kStep)
        {
            const int len = (Q - back < kBlock) ? (Q - back) : kBlock;
            if (len < kMinQuery) break;

            FindAll(*m_norm, q.substr((size_t)(Q - back - len), (size_t)len), from, to, hits);
            if (!hits.empty()) { usedLen = len; byChunk = true; }
        }
    }

    if (hits.empty())
    {
        ++m_failStreak;
        return false;
    }

    int best = PickHit(hits, m_pos, kBackTol);

    if (byChunk)
    {
        // 分块只说明「句尾这十几个字对上了」，不足以支撑回跳或大跳跃：
        // 拿不准就原地不动，等下一版识别结果
        if (best < 0 || hits[best].end < m_pos - kSmallBack ||
            hits[best].end - m_pos > 200)
        {
            ++m_failStreak;
            return false;
        }
    }

    if (best < 0)
    {
        // 没有任何前向命中：只有足够长的句子才认定为「老师回去重念了一段」
        if (Q >= 8)
        {
            for (int i = 0; i < (int)hits.size(); ++i)
            {
                if (best < 0 ||
                    abs(hits[i].end - m_pos) < abs(hits[best].end - m_pos))
                    best = i;
            }
        }
        if (best < 0)
        {
            ++m_failStreak;
            return false;
        }
    }

    // 短片段不允许一跳几百字，否则多半是稿件别处的相同短语
    if (hits[best].end - m_pos > kJumpGuard && usedLen < 10)
    {
        ++m_failStreak;
        return false;
    }

    int base = hits[best].end;
    if (base > N) base = N;
    if (base < 0) base = 0;

    // 小幅回退不跟随：识别结果抖动时不来回晃
    if (base < m_pos && m_pos - base <= kSmallBack) base = m_pos;

    m_pos = base;
    newPos = base;
    // 整句逐字命中即 100%；掐头去尾按命中比例；只有句尾某块对上时给 85% 的可信度
    score = byChunk ? 0.85f : (float)usedLen / (float)Q;
    m_failStreak = 0;
    return true;
}
