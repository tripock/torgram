/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/torgram_image_scrub.h"

#include <QtCore/QByteArray>
#include <QtCore/QDataStream>
#include <QtCore/QString>
#include <QtEndian>

namespace Core::Torgram {
namespace {

constexpr quint8 kJpegSoi[] = { 0xFF, 0xD8 };

[[nodiscard]] bool IsJpegStandaloneMarker(quint8 marker) {
	return marker == 0xD8
		|| marker == 0xD9
		|| (marker >= 0xD0 && marker <= 0xD7)
		|| marker == 0x01;
}

[[nodiscard]] bool ShouldDropJpegSegment(quint8 marker) {
	if (marker >= 0xE1 && marker <= 0xEF) {
		return true;
	}
	if (marker == 0xE0) {
		return false;
	}
	if (marker == 0xFE) {
		return true;
	}
	return false;
}

[[nodiscard]] bool ShouldDropPngChunk(const char *type) {
	static constexpr const char *kDrop[] = {
		"tEXt", "zTXt", "iTXt",
		"eXIf", "EXIF",
		"tIME",
		"pHYs",
		"iCCP",
		"bKGD",
		"sPLT",
	};
	for (const auto drop : kDrop) {
		if (std::memcmp(type, drop, 4) == 0) {
			return true;
		}
	}
	return false;
}

} // namespace

QByteArray ScrubJpegMetadata(const QByteArray &bytes) {
	if (bytes.size() < 4) {
		return bytes;
	}
	const auto data = reinterpret_cast<const quint8*>(bytes.constData());
	if (data[0] != kJpegSoi[0] || data[1] != kJpegSoi[1]) {
		return bytes;
	}

	auto result = QByteArray();
	result.reserve(bytes.size());
	result.append(bytes.constData(), 2);

	int offset = 2;
	const int size = bytes.size();
	while (offset + 1 < size) {
		if (data[offset] != 0xFF) {
			result.append(bytes.constData() + offset, size - offset);
			break;
		}
		auto marker = data[offset + 1];
		while (marker == 0xFF && offset + 2 < size) {
			++offset;
			marker = data[offset + 1];
		}
		const int markerStart = offset;
		offset += 2;

		if (marker == 0xD9) {
			result.append(bytes.constData() + markerStart, 2);
			break;
		}
		if (marker == 0xDA) {
			result.append(bytes.constData() + markerStart, size - markerStart);
			break;
		}
		if (IsJpegStandaloneMarker(marker)) {
			result.append(bytes.constData() + markerStart, 2);
			continue;
		}
		if (offset + 2 > size) {
			break;
		}
		const auto length = (int(data[offset]) << 8) | int(data[offset + 1]);
		if (length < 2 || offset + length > size) {
			break;
		}
		if (!ShouldDropJpegSegment(marker)) {
			result.append(bytes.constData() + markerStart, 2 + length);
		}
		offset += length;
	}
	return result;
}

QByteArray ScrubPngMetadata(const QByteArray &bytes) {
	static constexpr char kPngSig[] = {
		char(0x89), 'P', 'N', 'G', '\r', '\n', char(0x1A), '\n'
	};
	if (bytes.size() < int(sizeof(kPngSig))
		|| std::memcmp(bytes.constData(), kPngSig, sizeof(kPngSig)) != 0) {
		return bytes;
	}

	auto result = QByteArray();
	result.reserve(bytes.size());
	result.append(bytes.constData(), int(sizeof(kPngSig)));

	int offset = int(sizeof(kPngSig));
	const int size = bytes.size();
	while (offset + 8 <= size) {
		const auto lengthPtr = reinterpret_cast<const uchar*>(
			bytes.constData() + offset);
		const auto length = qFromBigEndian<quint32>(lengthPtr);
		if (offset + 12 + int(length) > size) {
			break;
		}
		const auto type = bytes.constData() + offset + 4;
		const auto drop = ShouldDropPngChunk(type);
		if (!drop) {
			result.append(bytes.constData() + offset, 12 + int(length));
		}
		offset += 12 + int(length);
		if (std::memcmp(type, "IEND", 4) == 0) {
			break;
		}
	}
	if (offset < size) {
		result.append(bytes.constData() + offset, size - offset);
	}
	return result;
}

QByteArray ScrubImageMetadata(
		const QByteArray &bytes,
		const QString &mimeOrFormat) {
	if (bytes.isEmpty()) {
		return bytes;
	}
	const auto lower = mimeOrFormat.toLower();
	if (lower.contains(u"jpeg"_q) || lower.contains(u"jpg"_q)) {
		return ScrubJpegMetadata(bytes);
	} else if (lower.contains(u"png"_q)) {
		return ScrubPngMetadata(bytes);
	}
	if (bytes.size() >= 2
		&& quint8(bytes[0]) == 0xFF
		&& quint8(bytes[1]) == 0xD8) {
		return ScrubJpegMetadata(bytes);
	}
	if (bytes.size() >= 8
		&& quint8(bytes[0]) == 0x89
		&& bytes[1] == 'P'
		&& bytes[2] == 'N'
		&& bytes[3] == 'G') {
		return ScrubPngMetadata(bytes);
	}
	return bytes;
}

} // namespace Core::Torgram
