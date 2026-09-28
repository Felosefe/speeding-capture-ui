#include "QtMultimediaRtspPlayer.h"

#include "QtMediaPlaybackBackend.h"

namespace rv1126b {

QtMultimediaRtspPlayer::QtMultimediaRtspPlayer(QObject* parent)
    : QtMultimediaRtspPlayer(new QtMediaPlaybackBackend, RtspPlayerTiming {}, parent)
{
}

} // namespace rv1126b
