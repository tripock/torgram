/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/const_string.h"
#include "mtproto/mtproto_proxy_data.h"

#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QTimer>
#include <QtNetwork/QTcpSocket>

namespace Core {

enum class TorgramStatus {
	Unknown,
	Probing,
	Connected,
	Failed,
};

[[nodiscard]] QString TorgramStatusText(TorgramStatus status);

class TorgramController final : public QObject {
public:
	static constexpr auto kTorHost = "127.0.0.1";
	static constexpr auto kTorPort = uint32(9050);
	static constexpr auto kProbeIntervalMs = 15000;
	static constexpr auto kProbeTimeoutMs = 4000;

	TorgramController();
	~TorgramController();

	[[nodiscard]] static MTP::ProxyData RequiredProxy();

	[[nodiscard]] TorgramStatus status() const;
	[[nodiscard]] rpl::producer<TorgramStatus> statusValue() const;

	void enforceSettings();
	void probeNow();
	void shutdown();

	[[nodiscard]] QString bundledTorBinary() const;

private:
	void setStatus(TorgramStatus status);
	void startProbe();
	void finishProbe(TorgramStatus result);
	void ensureBundledRunning();

	rpl::variable<TorgramStatus> _status = TorgramStatus::Unknown;
	std::unique_ptr<QTcpSocket> _socket;
	std::unique_ptr<QProcess> _torProcess;
	QString _dataDirectory;
	QString _torrcPath;
	QTimer _probeTimer;
	QTimer _scheduleTimer;
	bool _probing = false;
	bool _shuttingDown = false;
	int _consecutiveFailures = 0;

};

} // namespace Core

namespace Torgram {

[[nodiscard]] bool AllowVoiceCallsOutsideTor();
[[nodiscard]] bool AllowVideoCallsOutsideTor();
void SetAllowVoiceCallsOutsideTor(bool allow);
void SetAllowVideoCallsOutsideTor(bool allow);

[[nodiscard]] bool IsCallAllowed(bool video);

inline constexpr auto kAllowVoiceCallsOutsideTorKey
	= "torgram-allow-voice-calls-outside-tor"_cs;
inline constexpr auto kAllowVideoCallsOutsideTorKey
	= "torgram-allow-video-calls-outside-tor"_cs;

} // namespace Torgram
