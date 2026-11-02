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

// name -> (total nanoseconds, sample count)
inline QHash<QString, QPair<qint64, int>> &samples()
{
    static QHash<QString, QPair<qint64, int>> registry;
    return registry;
}

inline void record(const char *name, qint64 nanoseconds)
{
    if (!isEnabled())
        return;

    QMutexLocker locker(&sampleMutex());
    auto &sample = samples()[QString::fromLatin1(name)];
    sample.first += nanoseconds;
    sample.second++;
}

inline void dump()
{
    if (!isEnabled())
        return;

    QHash<QString, QPair<qint64, int>> collected;
    {
        QMutexLocker locker(&sampleMutex());
        collected.swap(samples());
    }

    QList<QPair<QString, QPair<qint64, int>>> sorted;
    sorted.reserve(collected.size());
    for (auto it = collected.constBegin(); it != collected.constEnd(); ++it)
        sorted.append({ it.key(), it.value() });

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

#define QV_PROFILE_CONCAT_INNER(first, second) first##second
#define QV_PROFILE_CONCAT(first, second) QV_PROFILE_CONCAT_INNER(first, second)
#define QV_PROFILE_SCOPE(name) QVProfile::Scope QV_PROFILE_CONCAT(qvProfileScope, __LINE__)(name)

#endif // QVPROFILER_H
