/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_audio_track.h"

#include "media/streaming/media_streaming_utility.h"
#include "media/audio/media_audio.h"
#include "media/audio/media_child_ffmpeg_loader.h"
#include "media/player/media_player_instance.h"

namespace Media {
namespace Streaming {

AudioTrack::AudioTrack(
	const PlaybackOptions &options,
	Stream &&stream,
	AudioMsgId audioId,
	FnMut<void(const Information &)> ready,
	Fn<void()> error)
: _options(options)
, _stream(std::move(stream))
, _audioId(audioId)
, _ready(std::move(ready))
, _error(std::move(error))
, _playPosition(options.position) {
	Expects(_ready != nullptr);
	Expects(_error != nullptr);
	Expects(_audioId.playId() != 0);
}

int AudioTrack::streamIndex() const {
	// Thread-safe, because _stream.index is immutable.
	return _stream.index;
}

AVRational AudioTrack::streamTimeBase() const {
	return _stream.timeBase;
}

void AudioTrack::process(Packet &&packet) {
	_noMoreData = packet.empty();
	if (initialized()) {
		mixerEnqueue(std::move(packet));
	} else if (!tryReadFirstFrame(std::move(packet))) {
		_error();
	}
}

bool AudioTrack::initialized() const {
	return !_ready;
}

bool AudioTrack::tryReadFirstFrame(Packet &&packet) {
	// #TODO streaming fix seek to the end.
	if (ProcessPacket(_stream, std::move(packet)).failed()) {
		return false;
	}
	if (const auto error = ReadNextFrame(_stream)) {
		if (error.code() == AVERROR_EOF) {
			// #TODO streaming fix seek to the end.
			return false;
		} else if (error.code() != AVERROR(EAGAIN) || _noMoreData) {
			return false;
		}
		return true;
	} else if (!fillStateFromFrame()) {
		return false;
	}
	mixerInit();
	callReady();
	return true;
}

bool AudioTrack::fillStateFromFrame() {
	_startedPosition = FramePosition(_stream);
	return (_startedPosition != kTimeUnknown);
}

void AudioTrack::mixerInit() {
	Expects(!initialized());

	auto data = std::make_unique<VideoSoundData>();
	data->frame = _stream.frame.release();
	data->context = _stream.codec.release();
	data->frequency = _stream.frequency;
	data->length = (_stream.duration * data->frequency) / 1000LL;
	data->speed = _options.speed;
	Media::Player::mixer()->play(
		_audioId,
		std::move(data),
		_startedPosition);
}

void AudioTrack::callReady() {
	Expects(_ready != nullptr);

	auto data = AudioInformation();
	data.state.duration = _stream.duration;
	data.state.position = _startedPosition;
	data.state.receivedTill = _noMoreData
		? _stream.duration
		: _startedPosition;
	base::take(_ready)({ VideoInformation(), data });
}

void AudioTrack::mixerEnqueue(Packet &&packet) {
	Media::Player::mixer()->feedFromVideo({
		&packet.fields(),
		_audioId
	});
	packet.release();
}

void AudioTrack::start(crl::time startTime) {
	Expects(initialized());

	// #TODO streaming support start() when paused.
	Media::Player::mixer()->resume(_audioId, true);
}

rpl::producer<crl::time> AudioTrack::playPosition() {
	Expects(_ready == nullptr);

	if (!_subscription) {
		_subscription = Media::Player::Updated(
		).add_subscription([=](const AudioMsgId &id) {
			using State = Media::Player::State;
			if (id != _audioId) {
				return;
			}
			const auto type = AudioMsgId::Type::Video;
			const auto state = Media::Player::mixer()->currentState(type);
			if (state.id != _audioId) {
				// #TODO streaming muted by other
				return;
			} else switch (state.state) {
			case State::Stopped:
			case State::StoppedAtEnd:
			case State::PausedAtEnd:
				_playPosition.reset();
				return;
			case State::StoppedAtError:
			case State::StoppedAtStart:
				_error();
				return;
			case State::Starting:
			case State::Playing:
			case State::Stopping:
			case State::Pausing:
			case State::Resuming:
				_playPosition = state.position * 1000 / state.frequency;
				return;
			case State::Paused:
				return;
			}
		});
	}
	return _playPosition.value();
}

AudioTrack::~AudioTrack() {
	if (_audioId.playId()) {
		Media::Player::mixer()->stop(_audioId);
	}
}

} // namespace Streaming
} // namespace Media
