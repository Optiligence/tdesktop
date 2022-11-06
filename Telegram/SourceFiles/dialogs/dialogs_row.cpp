/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_row.h"

#include "qtimer.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "dialogs/dialogs_entry.h"
#include "data/data_folder.h"
#include "data/data_peer_values.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "styles/style_dialogs.h"
#include <random>

namespace Dialogs {
namespace {

[[nodiscard]] TextWithEntities ComposeFolderListEntryText(
		not_null<Data::Folder*> folder) {
	const auto &list = folder->lastHistories();
	if (list.empty()) {
		return {};
	}

	const auto count = std::max(
		int(list.size()),
		folder->chatsList()->fullSize().current());

	const auto throwAwayLastName = (list.size() > 1)
		&& (count == list.size() + 1);
	auto &&peers = ranges::views::all(
		list
	) | ranges::views::take(
		list.size() - (throwAwayLastName ? 1 : 0)
	);
	const auto wrapName = [](not_null<History*> history) {
		const auto name = history->peer->name;
		return TextWithEntities{
			.text = name,
			.entities = (history->unreadCount() > 0)
				? EntitiesInText{
					{ EntityType::Semibold, 0, int(name.size()), QString() },
					{ EntityType::PlainLink, 0, int(name.size()), QString() },
				}
				: EntitiesInText{}
		};
	};
	const auto shown = int(peers.size());
	const auto accumulated = [&] {
		Expects(shown > 0);

		auto i = peers.begin();
		auto result = wrapName(*i);
		for (++i; i != peers.end(); ++i) {
			result = tr::lng_archived_last_list(
				tr::now,
				lt_accumulated,
				result,
				lt_chat,
				wrapName(*i),
				Ui::Text::WithEntities);
		}
		return result;
	}();
	return (shown < count)
		? tr::lng_archived_last(
			tr::now,
			lt_count,
			(count - shown),
			lt_chats,
			accumulated,
			Ui::Text::WithEntities)
		: accumulated;
}

} // namespace

BasicRow::BasicRow() = default;
BasicRow::~BasicRow() = default;

void BasicRow::setCornerBadgeShown(
		bool shouldShow,
		Fn<void()> updateCallback) const {
	if (_cornerBadgeVisible == shouldShow) {
		return;
	}
	_cornerBadgeVisible = shouldShow;
	if (_cornerBadgeUserpic && _cornerBadgeUserpic->animation.animating()) {
		_cornerBadgeUserpic->animation.change(
			_cornerBadgeVisible ? 1. : 0.,
			st::dialogsOnlineBadgeDuration);
	} else if (updateCallback) {
		ensureCornerBadgeUserpic();
		_cornerBadgeUserpic->animation.start(
			std::move(updateCallback),
			_cornerBadgeVisible ? 0. : 1.,
			_cornerBadgeVisible ? 1. : 0.,
			st::dialogsOnlineBadgeDuration);
	}
	if (!_cornerBadgeVisible
		&& _cornerBadgeUserpic
		&& !_cornerBadgeUserpic->animation.animating()) {
		_cornerBadgeUserpic = nullptr;
	}
	return;

	if (updateCallback) {
		qDebug() << "ensureCornerBadgeUserpic" << shouldShow;
		ensureCornerBadgeUserpic();
		_cornerBadgeUserpic->animation.start(
			std::move(updateCallback),
			_cornerBadgeVisible ? 0. : 1.,
			_cornerBadgeVisible ? 1. : 0.,
			st::dialogsOnlineBadgeDuration);
	}
//    qDebug() << !!_cornerBadgeUserpic << shouldShow;
//    qWarning() << "setCornerBadgeShown" << _cornerBadgeShown << shown << !!updateCallback
//               << !!_cornerBadgeUserpic << (_cornerBadgeUserpic && _cornerBadgeUserpic->animation.animating());
	if (_cornerBadgeUserpic && _cornerBadgeUserpic->animation.animating()) {
//        qDebug() << "animating" << _cornerBadgeUserpic->animation.value(_cornerBadgeVisible);
		_cornerBadgeUserpic->animation.change(
			_cornerBadgeVisible ? 1. : 0.,
			st::dialogsOnlineBadgeDuration);
	}
//    if (_cornerBadgeVisible == shouldShow) {
//        return;
//    }
	_cornerBadgeVisible = shouldShow;
	if (!_cornerBadgeVisible
		&& _cornerBadgeUserpic
		&& !_cornerBadgeUserpic->animation.animating()) {
		qDebug() << "deleting" << !!_cornerBadgeUserpic << _cornerBadgeUserpic->animation.animating();
//        exit(0);
		_cornerBadgeUserpic = nullptr;
	}
//    qWarning() << "setCornerBadgeShown2" << _cornerBadgeShown << shown << !!updateCallback
//               << !!_cornerBadgeUserpic << (_cornerBadgeUserpic && _cornerBadgeUserpic->animation.animating());
}

void BasicRow::addRipple(
		QPoint origin,
		QSize size,
		Fn<void()> updateCallback) {
	if (!_ripple) {
		auto mask = Ui::RippleAnimation::rectMask(size);
		_ripple = std::make_unique<Ui::RippleAnimation>(
			st::dialogsRipple,
			std::move(mask),
			std::move(updateCallback));
	}
	_ripple->add(origin);
}

void BasicRow::stopLastRipple() {
	if (_ripple) {
		_ripple->lastStop();
	}
}

void BasicRow::paintRipple(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		const QColor *colorOverride) const {
	if (_ripple) {
		_ripple->paint(p, x, y, outerWidth, colorOverride);
		if (_ripple->empty()) {
			_ripple.reset();
		}
	}
}

void BasicRow::updateCornerBadgeShown(
		not_null<PeerData*> peer,
		Fn<void()> updateCallback) const {
	const auto shown = [&] {
		if (const auto user = peer->asUser()) {
			return Data::IsUserOnline(user);
		} else if (const auto channel = peer->asChannel()) {
			return Data::ChannelHasActiveCall(channel);
		}
		return false;
	}();
	static bool shown2;
	static QTimer t;
	if (!t.isActive()) {
		t.start(2000);
		t.callOnTimeout([&](){
//            shown2 = !shown2;
//            qDebug() << "set shown" << shown << !!updateCallback;
//            setCornerBadgeShown(shown, nullptr);
		});
	}
//    qDebug() << "updateCornerBadgeShown" << shown;
	setCornerBadgeShown(shown, std::move(updateCallback));
}

void BasicRow::ensureCornerBadgeUserpic() const {
	if (_cornerBadgeUserpic) {
		return;
	}
	qDebug() << "ensureCornerBadgeUserpic2";
	_cornerBadgeUserpic = std::make_unique<CornerBadgeUserpic>();
}

void BasicRow::PaintCornerBadgeFrame(
		not_null<CornerBadgeUserpic*> data,
		not_null<PeerData*> peer,
		std::shared_ptr<Data::CloudImageView> &view) {
	data->frame.fill(Qt::transparent);
	Painter q(&data->frame);
	peer->paintUserpic(
		q,
		view,
		0,
		0,
		st::dialogsPhotoSize);

	PainterHighQualityEnabler hq(q);
	q.setCompositionMode(QPainter::CompositionMode_Source);

	const auto size = peer->isUser()
		? st::dialogsOnlineBadgeSize
		: st::dialogsCallBadgeSize;
	const auto stroke = st::dialogsOnlineBadgeStroke;
	const auto skip = peer->isUser()
		? st::dialogsOnlineBadgeSkip
		: st::dialogsCallBadgeSkip;
	const auto shrink = (size / 2) * (1. - data->shown);

	auto pen = QPen(Qt::transparent);
	pen.setWidthF(stroke * data->shown);
	q.setPen(pen);
	q.setBrush(data->active
		? st::dialogsOnlineBadgeFgActive
		: st::dialogsOnlineBadgeFg);
	qWarning() << "PaintCornerBadgeFrame" << "shown" << data->shown << "active" << data->active << "isUser" << peer->isUser() << shrink << skip << size << stroke << data->key;
	q.drawEllipse(QRectF(
		st::dialogsPhotoSize - skip.x() - size,
		st::dialogsPhotoSize - skip.y() - size,
		size,
		size
	).marginsRemoved({ shrink, shrink, shrink, shrink }));
}

void BasicRow::paintUserpic(
		Painter &p,
		not_null<PeerData*> peer,
		History *historyForCornerBadge,
		crl::time now,
		bool active,
		int fullWidth) const {
//    qDebug() << "paintUserpic";
	updateCornerBadgeShown(peer);

	auto shown = _cornerBadgeUserpic
		? _cornerBadgeUserpic->animation.value(_cornerBadgeVisible ? 1. : 0.)
		: _cornerBadgeVisible;
//    qDebug() << "shown" << !!_cornerBadgeUserpic << !!_cornerBadgeShown << shown;
	if (!historyForCornerBadge || shown == 0.) {
		peer->paintUserpicLeft(
			p,
			_userpic,
			st::dialogsPadding.x(),
			st::dialogsPadding.y(),
			fullWidth,
			st::dialogsPhotoSize);
		if (!historyForCornerBadge) {
			qDebug() << "deleting2" << !historyForCornerBadge << shown
					 << !!_cornerBadgeUserpic << (_cornerBadgeUserpic && _cornerBadgeUserpic->animation.animating());
			_cornerBadgeUserpic = nullptr;
		}
//        qDebug() << "return";
		return;
	}
	if (!_cornerBadgeUserpic) {
		qDebug() << "ensureCornerBadgeUserpic3";
	}
	ensureCornerBadgeUserpic();
	if (_cornerBadgeUserpic->frame.isNull()) {
		_cornerBadgeUserpic->frame = QImage(
			st::dialogsPhotoSize * cRetinaFactor(),
			st::dialogsPhotoSize * cRetinaFactor(),
			QImage::Format_ARGB32_Premultiplied);
		_cornerBadgeUserpic->frame.setDevicePixelRatio(cRetinaFactor());
	}
	const auto key = peer->userpicUniqueKey(_userpic);
	if (_cornerBadgeUserpic->shown != shown
		|| _cornerBadgeUserpic->key != key
		|| _cornerBadgeUserpic->active != active) {
		_cornerBadgeUserpic->shown = shown;
		_cornerBadgeUserpic->key = key;
		_cornerBadgeUserpic->active = active;
		PaintCornerBadgeFrame(_cornerBadgeUserpic.get(), peer, _userpic);
	}
	p.drawImage(st::dialogsPadding, _cornerBadgeUserpic->frame);
	if (historyForCornerBadge->peer->isUser()) {
		return;
	}
	const auto actionPainter = historyForCornerBadge->sendActionPainter();
	const auto bg = active
		? st::dialogsBgActive
		: st::dialogsBg;
	const auto size = st::dialogsCallBadgeSize;
	const auto skip = st::dialogsCallBadgeSkip;
	p.setOpacity(shown);
	p.translate(st::dialogsPadding);
	actionPainter->paintSpeaking(
		p,
		st::dialogsPhotoSize - skip.x() - size,
		st::dialogsPhotoSize - skip.y() - size,
		fullWidth,
		bg,
		now);
	p.translate(-st::dialogsPadding);
	p.setOpacity(1.);
}

Row::Row(Key key, int pos) : _id(key), _pos(pos) {
	if (const auto history = key.history()) {
		updateCornerBadgeShown(history->peer);
	}
}

uint64 Row::sortKey(FilterId filterId) const {
	return _id.entry()->sortKeyInChatList(filterId);
}

void Row::validateListEntryCache() const {
	const auto folder = _id.folder();
	if (!folder) {
		return;
	}
	const auto version = folder->chatListViewVersion();
	if (_listEntryCacheVersion == version) {
		return;
	}
	_listEntryCacheVersion = version;
	_listEntryCache.setMarkedText(
		st::dialogsTextStyle,
		ComposeFolderListEntryText(folder),
		// Use rich options as long as the entry text does not have user text.
		Ui::ItemTextDefaultOptions());
}

FakeRow::FakeRow(Key searchInChat, not_null<HistoryItem*> item)
: _searchInChat(searchInChat)
, _item(item) {
}

} // namespace Dialogs
