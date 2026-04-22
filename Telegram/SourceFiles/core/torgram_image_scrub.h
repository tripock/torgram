/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QByteArray;
class QString;

namespace Core::Torgram {

[[nodiscard]] QByteArray ScrubImageMetadata(
	const QByteArray &bytes,
	const QString &mimeOrFormat);

[[nodiscard]] QByteArray ScrubJpegMetadata(const QByteArray &bytes);
[[nodiscard]] QByteArray ScrubPngMetadata(const QByteArray &bytes);

} // namespace Core::Torgram
