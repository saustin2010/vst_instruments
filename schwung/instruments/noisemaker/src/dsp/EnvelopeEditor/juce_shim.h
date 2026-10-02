/*
 * juce_shim.h — SCHWUNG PORT: minimal JUCE stand-ins for the TAL Envelope
 * Editor DSP core (EnvelopeEditor / SplinePoint / SplineUtility).
 *
 * The vendored TAL envelope files use a tiny slice of JUCE: Array<T>,
 * juce::Point<float>, the Timer base class, and CriticalSection/ScopedLock.
 * None of the drawable-UI code is ported, so we only need the DSP-visible
 * surface. This header supplies std::vector-backed / trivial replacements so
 * the engine builds JUCE-free. It is NOT a JUCE port — only what these files
 * touch (verified by grep) is provided.
 */
#ifndef NM_JUCE_SHIM_H
#define NM_JUCE_SHIM_H

#include <vector>
#include <algorithm>
#include <cstddef>   // NULL
#include <cmath>     // floorf, sqrtf

/* ---- Array<T> (JUCE juce::Array subset) ------------------------------- *
 * Methods used by the envelope core: add, clear, insert, removeAndReturn,
 * size, operator[], sort(comparator, retainOrder). Pointer elements are NOT
 * deleted on clear() — matches JUCE Array semantics (OwnedArray would). */
template <class T>
class Array
{
    std::vector<T> v;
public:
    void add(const T& e)                 { v.push_back(e); }
    void clear()                         { v.clear(); }
    void insert(int index, const T& e)
    {
        if (index < 0 || index >= (int)v.size()) v.push_back(e);
        else                                     v.insert(v.begin() + index, e);
    }
    int  size() const                    { return (int)v.size(); }
    T&       operator[](int i)           { return v[i]; }
    const T& operator[](int i) const     { return v[i]; }

    template <class Comparator>
    void sort(Comparator& comparator, bool /*retainOrderOfEquivalentItems*/)
    {
        std::stable_sort(v.begin(), v.end(),
            [](const T& a, const T& b) { return Comparator::compareElements(a, b) < 0; });
    }

    T removeAndReturn(int index)
    {
        T e = v[index];
        v.erase(v.begin() + index);
        return e;
    }
};

/* ---- juce::Point<float> (subset) -------------------------------------- */
namespace juce
{
    template <class T>
    struct Point
    {
        T x, y;
        Point() : x(0), y(0) {}
        Point(T x_, T y_) : x(x_), y(y_) {}

        T  getX() const           { return x; }
        T  getY() const           { return y; }
        void setX(T v)            { x = v; }
        void setY(T v)            { y = v; }
        void setXY(T x_, T y_)    { x = x_; y = y_; }

        Point  operator- (const Point& o) const { return Point(x - o.x, y - o.y); }
        Point& operator-=(const Point& o)       { x -= o.x; y -= o.y; return *this; }
        Point  operator+ (const Point& o) const { return Point(x + o.x, y + o.y); }
    };
}

/* ---- Timer / locking no-ops ------------------------------------------- *
 * The port is single-threaded (audio thread only). The buffered-recalc timer
 * never fires; EnvelopeEditor::getEnvelopeValue then always takes the direct
 * (dirty) spline-eval path, which is correct — just slightly more CPU than the
 * cached path. CriticalSection/ScopedLock become no-ops. */
class Timer
{
public:
    virtual ~Timer() {}
    void startTimer(int /*intervalMs*/) {}
    void stopTimer() {}
    virtual void timerCallback() {}
};

struct CriticalSection {};
struct ScopedLock { ScopedLock(const CriticalSection&) {} };

#endif /* NM_JUCE_SHIM_H */
