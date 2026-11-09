#ifndef QVPROFILER_H
#define QVPROFILER_H

#include <QDebug>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QPair>
#include <QString>
#include <QtGlobal>

#include <algorithm>

// Lightweight scope profiler. It stays off unless QV_PROFILE is set in the environment, so an
// instrumented build costs one branch per sample when profiling is not wanted.
//
// Usage:
//   QV_PROFILE_SCOPE("name")                       // logs the enclosing block
//   QVProfile::record("name", timer.nsecsElapsed())  // logs a manually measured section
//
// The samples are aggregated per name and printed once when the application quits.
namespace QVProfile {

// What a name has cost so far: total nanoseconds, and how many samples were added to it
using Totals = QPair<qint64, int>;

inline bool isEnabled()
{
    static const bool enabled = qEnvironmentVariableIsSet("QV_PROFILE");
    return enabled;
}

inline QMutex &sampleMutex()
{
    static QMutex mutex;
    return mutex;
}

inline QHash<QString, Totals> &samples()
{
    static QHash<QString, Totals> registry;
    return registry;
}

// Adds one sample of value nanoseconds to name. Callers check isEnabled() themselves, so that
// measuring costs nothing while profiling is off.
inline void accumulate(const char *name, qint64 value)
{
    QMutexLocker locker(&sampleMutex());
    Totals &sample = samples()[QString::fromLatin1(name)];
    sample.first += value;
    sample.second++;
}

inline void record(const char *name, qint64 nanoseconds)
{
    if (isEnabled())
        accumulate(name, nanoseconds);
}

// Wall clock for milestones that happen outside a scope, for example "when did the window really
// become visible" or "when did the first frame get painted". main() starts it, everything else
// reads the elapsed value from it.
inline QElapsedTimer &startupClock()
{
    static QElapsedTimer clock;
    return clock;
}

inline void startStartupClock()
{
    startupClock().start();
}

inline qint64 sinceStartup()
{
    const QElapsedTimer &clock = startupClock();
    return clock.isValid() ? clock.nsecsElapsed() : 0;
}

// Records a one-shot milestone as the time elapsed since startStartupClock()
inline void milestone(const char *name)
{
    if (isEnabled())
        accumulate(name, sinceStartup());
}

inline void dump()
{
    if (!isEnabled())
        return;

    // Take the samples out under the lock, then sort and print without holding it
    QHash<QString, Totals> collected;
    {
        QMutexLocker locker(&sampleMutex());
        collected.swap(samples());
    }

    QList<QPair<QString, Totals>> sorted;
    sorted.reserve(collected.size());
    for (auto it = collected.constBegin(); it != collected.constEnd(); ++it)
        sorted.append({ it.key(), it.value() });

    // Heaviest total first
    std::sort(sorted.begin(), sorted.end(), [](const auto &first, const auto &second) {
        return first.second.first > second.second.first;
    });

    qInfo().noquote() << "[profile] scope\tcount\ttotal ms\tavg ms";
    for (const auto &entry : sorted) {
        const qint64 totalNanoseconds = entry.second.first;
        const int count = entry.second.second;
        const double totalMilliseconds = totalNanoseconds / 1000000.0;
        const double averageMilliseconds = count ? totalMilliseconds / count : 0.0;
        qInfo().noquote() << QStringLiteral("[profile] %1\t%2\t%3\t%4")
                                     .arg(entry.first, -40)
                                     .arg(count)
                                     .arg(totalMilliseconds)
                                     .arg(averageMilliseconds);
    }
}

class Scope
{
public:
    explicit Scope(const char *name) : name(name), active(isEnabled())
    {
        if (active)
            timer.start();
    }

    ~Scope()
    {
        if (active)
            record(name, timer.nsecsElapsed());
    }

    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

private:
    const char *name;
    bool active;
    QElapsedTimer timer;
};

} // namespace QVProfile

// Two levels so that __LINE__ is expanded before it is pasted into the variable name
#define QV_PROFILE_CONCAT_INNER(first, second) first##second
#define QV_PROFILE_CONCAT(first, second) QV_PROFILE_CONCAT_INNER(first, second)
#define QV_PROFILE_SCOPE(name) QVProfile::Scope QV_PROFILE_CONCAT(qvProfileScope, __LINE__)(name)

#endif // QVPROFILER_H
