/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/torgram_controller.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "core/core_settings_proxy.h"
#include "lang/lang_keys.h"
#include "logs.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QStandardPaths>
#include <QtCore/QTextStream>
#include <QtNetwork/QAbstractSocket>

#ifndef TORGRAM_TOR_BUNDLE_RELATIVE_DIR
#define TORGRAM_TOR_BUNDLE_RELATIVE_DIR "tor-bundle"
#endif

namespace Core {
namespace {

[[nodiscard]] bool SameAsRequired(const MTP::ProxyData &proxy) {
	return proxy.type == MTP::ProxyData::Type::Socks5
		&& proxy.host == QString::fromLatin1(TorgramController::kTorHost)
		&& proxy.port == TorgramController::kTorPort;
}

} // namespace

QString TorgramStatusText(TorgramStatus status) {
	switch (status) {
	case TorgramStatus::Unknown: return tr::lng_torgram_status_unknown(tr::now);
	case TorgramStatus::Probing: return tr::lng_torgram_status_probing(tr::now);
	case TorgramStatus::Connected: return tr::lng_torgram_status_connected(tr::now);
	case TorgramStatus::Failed: return tr::lng_torgram_status_failed(tr::now);
	}
	return QString();
}

MTP::ProxyData TorgramController::RequiredProxy() {
	auto result = MTP::ProxyData();
	result.type = MTP::ProxyData::Type::Socks5;
	result.host = QString::fromLatin1(kTorHost);
	result.port = kTorPort;
	return result;
}

TorgramController::TorgramController() {
	_probeTimer.setSingleShot(true);
	_probeTimer.setInterval(kProbeTimeoutMs);
	QObject::connect(&_probeTimer, &QTimer::timeout, this, [=] {
		finishProbe(TorgramStatus::Failed);
	});

	_scheduleTimer.setSingleShot(false);
	_scheduleTimer.setInterval(kProbeIntervalMs);
	QObject::connect(&_scheduleTimer, &QTimer::timeout, this, [=] {
		startProbe();
	});
}

TorgramController::~TorgramController() {
	shutdown();
}

TorgramStatus TorgramController::status() const {
	return _status.current();
}

rpl::producer<TorgramStatus> TorgramController::statusValue() const {
	return _status.value();
}

void TorgramController::enforceSettings() {
	auto &proxy = Core::App().settings().proxy();
	const auto required = RequiredProxy();
	auto changed = false;

	if (!SameAsRequired(proxy.selected())) {
		auto list = proxy.list();
		const auto existing = std::find_if(
			begin(list),
			end(list),
			SameAsRequired);
		if (existing == end(list)) {
			list.insert(begin(list), required);
			proxy.setList(std::move(list));
		}
		proxy.setSelected(required);
		changed = true;
	}
	if (proxy.settings() != MTP::ProxyData::Settings::Enabled) {
		proxy.setSettings(MTP::ProxyData::Settings::Enabled);
		changed = true;
	}
	if (!proxy.useProxyForCalls()) {
		proxy.setUseProxyForCalls(true);
		changed = true;
	}
	if (changed) {
		proxy.connectionTypeChangesNotify();
		Core::App().saveSettingsDelayed();
	}
}

void TorgramController::probeNow() {
	if (!_scheduleTimer.isActive()) {
		_scheduleTimer.start();
	}
	startProbe();
}

void TorgramController::shutdown() {
	_shuttingDown = true;
	_probeTimer.stop();
	_scheduleTimer.stop();
	if (_socket) {
		_socket->disconnect(this);
		_socket->abort();
		_socket.reset();
	}
	if (_torProcess) {
		_torProcess->disconnect(this);
		_torProcess->terminate();
		if (!_torProcess->waitForFinished(3000)) {
			_torProcess->kill();
			_torProcess->waitForFinished(2000);
		}
		_torProcess.reset();
	}
}

QString TorgramController::bundledTorBinary() const {
	const auto exeDir = QCoreApplication::applicationDirPath();
	const auto relative = QString::fromLatin1(TORGRAM_TOR_BUNDLE_RELATIVE_DIR);
#ifdef Q_OS_MAC
	const auto candidates = {
		exeDir + u"/../Resources/"_q + relative,
		exeDir + u"/"_q + relative,
	};
#elif defined Q_OS_WIN
	const auto candidates = {
		exeDir + u"/"_q + relative,
	};
#else
	const auto candidates = {
		exeDir + u"/"_q + relative,
	};
#endif
	const auto binaryName =
#ifdef Q_OS_WIN
		u"tor.exe"_q
#else
		u"tor"_q
#endif
		;
	for (const auto &dir : candidates) {
		const auto path = dir + u"/"_q + binaryName;
		if (QFileInfo::exists(path)) {
			return path;
		}
	}
	return QString();
}

void TorgramController::ensureBundledRunning() {
	if (_shuttingDown
		|| (_torProcess
			&& _torProcess->state() != QProcess::NotRunning)) {
		return;
	}
	const auto binary = bundledTorBinary();
	if (binary.isEmpty()) {
		return;
	}

	if (_dataDirectory.isEmpty()) {
		const auto cache = QStandardPaths::writableLocation(
			QStandardPaths::AppDataLocation);
		_dataDirectory = QDir(cache).filePath(u"tor-data"_q);
		QDir().mkpath(_dataDirectory);
	}

	if (_torrcPath.isEmpty()) {
		_torrcPath = QDir(_dataDirectory).filePath(u"torrc"_q);
		QFile torrc(_torrcPath);
		if (torrc.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			QTextStream stream(&torrc);
			stream << u"SocksPort 127.0.0.1:9050\n"_q;
			stream << u"DataDirectory "_q << _dataDirectory << u"\n"_q;
			stream << u"ClientOnly 1\n"_q;
			stream << u"AvoidDiskWrites 1\n"_q;
			stream << u"Log notice stderr\n"_q;
			torrc.close();
		}
	}

	_torProcess = std::make_unique<QProcess>();
	_torProcess->setProgram(binary);
	const auto bundleDir = QFileInfo(binary).absolutePath();
	_torProcess->setWorkingDirectory(bundleDir);
	_torProcess->setArguments({
		u"-f"_q,
		_torrcPath,
	});
	auto env = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_LINUX
	const auto libDir = bundleDir;
	env.insert(
		u"LD_LIBRARY_PATH"_q,
		libDir + u":"_q + env.value(u"LD_LIBRARY_PATH"_q));
#elif defined Q_OS_MAC
	env.insert(
		u"DYLD_LIBRARY_PATH"_q,
		bundleDir + u":"_q + env.value(u"DYLD_LIBRARY_PATH"_q));
#endif
	_torProcess->setProcessEnvironment(env);

	const auto raw = _torProcess.get();
	QObject::connect(
		raw,
		&QProcess::errorOccurred,
		this,
		[=](QProcess::ProcessError error) {
			LOG(("Torgram: tor process error %1 (%2)"
				).arg(int(error)
				).arg(raw->errorString()));
		});
	QObject::connect(
		raw,
		QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
		this,
		[=](int code, QProcess::ExitStatus status) {
			LOG(("Torgram: tor process exited code=%1 status=%2"
				).arg(code
				).arg(int(status)));
		});
	_torProcess->start();
}

void TorgramController::startProbe() {
	if (_probing || _shuttingDown) {
		return;
	}
	_probing = true;
	setStatus(TorgramStatus::Probing);

	_socket = std::make_unique<QTcpSocket>();
	const auto raw = _socket.get();
	QObject::connect(raw, &QAbstractSocket::connected, this, [=] {
		static constexpr quint8 request[] = {
			0x05, 0x01, 0x00,
		};
		raw->write(reinterpret_cast<const char*>(request), sizeof(request));
	});
	QObject::connect(raw, &QAbstractSocket::readyRead, this, [=] {
		const auto data = raw->read(2);
		if (data.size() == 2
			&& quint8(data[0]) == 0x05
			&& quint8(data[1]) == 0x00) {
			finishProbe(TorgramStatus::Connected);
		} else {
			finishProbe(TorgramStatus::Failed);
		}
	});
	QObject::connect(raw, &QAbstractSocket::errorOccurred, this, [=] {
		finishProbe(TorgramStatus::Failed);
	});

	_probeTimer.start();
	_socket->connectToHost(
		QString::fromLatin1(kTorHost),
		quint16(kTorPort));
}

void TorgramController::finishProbe(TorgramStatus result) {
	if (!_probing) {
		return;
	}
	_probing = false;
	_probeTimer.stop();
	if (_socket) {
		_socket->disconnect(this);
		_socket->abort();
		_socket.reset();
	}
	setStatus(result);
	if (result == TorgramStatus::Connected) {
		_consecutiveFailures = 0;
	} else {
		++_consecutiveFailures;
		if (_consecutiveFailures >= 2) {
			ensureBundledRunning();
		}
	}
	if (!_scheduleTimer.isActive() && !_shuttingDown) {
		_scheduleTimer.start();
	}
}

void TorgramController::setStatus(TorgramStatus status) {
	if (_status.current() == status) {
		return;
	}
	_status = status;
	if (IsAppLaunched()) {
		Core::App().settings().proxy().connectionTypeChangesNotify();
		Core::App().refreshGlobalProxy();
	}
	LOG(("Torgram: status changed to %1").arg(int(status)));
}

} // namespace Core

namespace Torgram {

bool AllowVoiceCallsOutsideTor() {
	if (!Core::IsAppLaunched()) {
		return false;
	}
	return Core::App().settings().readPref<bool>(
		kAllowVoiceCallsOutsideTorKey);
}

bool AllowVideoCallsOutsideTor() {
	if (!Core::IsAppLaunched()) {
		return false;
	}
	return Core::App().settings().readPref<bool>(
		kAllowVideoCallsOutsideTorKey);
}

void SetAllowVoiceCallsOutsideTor(bool allow) {
	if (!Core::IsAppLaunched()) {
		return;
	}
	Core::App().settings().writePref<bool>(
		kAllowVoiceCallsOutsideTorKey,
		allow);
	Core::App().saveSettingsDelayed();
}

void SetAllowVideoCallsOutsideTor(bool allow) {
	if (!Core::IsAppLaunched()) {
		return;
	}
	Core::App().settings().writePref<bool>(
		kAllowVideoCallsOutsideTorKey,
		allow);
	Core::App().saveSettingsDelayed();
}

bool IsCallAllowed(bool video) {
	const auto torConnected = Core::IsAppLaunched()
		&& (Core::App().torgram().status()
			== Core::TorgramStatus::Connected);
	if (torConnected) {
		return true;
	}
	return video
		? AllowVideoCallsOutsideTor()
		: AllowVoiceCallsOutsideTor();
}

} // namespace Torgram
