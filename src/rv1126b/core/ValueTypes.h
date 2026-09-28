#pragma once

#include <QMetaType>
#include <QString>

#include <tuple>

namespace rv1126b {

template<typename Enum>
struct WireEnum {
    Enum value = Enum::Unknown;
    QString rawValue;
};

enum class TimeQuality {
    NativeUtc = 0,
    ConfiguredOffset = 1,
    BoardEpochUnverified = 2,
    Unknown = 3,
    AppApiSetCurrentBoot = 4,
    RtcRestoredCurrentBoot = 5
};

struct NormalizedTime {
    qint64 epochMs = 0;
    qint64 sourceEpochMs = 0;
    qint64 offsetAppliedMs = 0;
    WireEnum<TimeQuality> quality;
    QString bootId;
};

struct EventIdentity {
    QString deviceId;
    qint64 eventId = 0;
    qint64 trackId = 0;

    friend bool operator==(const EventIdentity& left, const EventIdentity& right) noexcept
    {
        return left.deviceId == right.deviceId
            && left.eventId == right.eventId
            && left.trackId == right.trackId;
    }

    friend bool operator!=(const EventIdentity& left, const EventIdentity& right) noexcept
    {
        return !(left == right);
    }

    friend bool operator<(const EventIdentity& left, const EventIdentity& right) noexcept
    {
        return std::tie(left.deviceId, left.eventId, left.trackId)
            < std::tie(right.deviceId, right.eventId, right.trackId);
    }
};

inline size_t qHash(const EventIdentity& identity, size_t seed = 0) noexcept
{
    seed = ::qHash(identity.deviceId, seed);
    seed = ::qHash(identity.eventId, seed);
    return ::qHash(identity.trackId, seed);
}

struct EventSortKey {
    qint64 sourceEpochMs = 0;
    qint64 eventId = 0;
    qint64 trackId = 0;

    friend bool operator==(const EventSortKey& left, const EventSortKey& right) noexcept
    {
        return std::tie(left.sourceEpochMs, left.eventId, left.trackId)
            == std::tie(right.sourceEpochMs, right.eventId, right.trackId);
    }

    friend bool operator<(const EventSortKey& left, const EventSortKey& right) noexcept
    {
        return std::tie(left.sourceEpochMs, left.eventId, left.trackId)
            < std::tie(right.sourceEpochMs, right.eventId, right.trackId);
    }
};

} // namespace rv1126b

Q_DECLARE_METATYPE(rv1126b::NormalizedTime)
Q_DECLARE_METATYPE(rv1126b::EventIdentity)
Q_DECLARE_METATYPE(rv1126b::EventSortKey)

