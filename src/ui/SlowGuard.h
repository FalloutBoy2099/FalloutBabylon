#pragma once

#include <QDebug>
#include <QElapsedTimer>

// Debug aid: reports sections of the UI thread that take longer than `limit` milliseconds.
// Enabled with the environment variable BABYLON_PROFILE=1.
class SlowGuard
{
public:
    explicit SlowGuard(const char *what, qint64 limit = 30)
        : m_what(what), m_limit(limit)
    {
        m_timer.start();
    }
    ~SlowGuard()
    {
        static const bool enabled = qEnvironmentVariableIsSet("BABYLON_PROFILE");
        if (enabled && m_timer.elapsed() >= m_limit)
            qInfo().nospace() << "[slow] " << m_what << " took " << m_timer.elapsed() << " ms";
    }

private:
    const char *m_what;
    qint64 m_limit;
    QElapsedTimer m_timer;
};
