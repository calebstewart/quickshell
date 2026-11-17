#include "ipc.hpp"
#include <functional>
#include <variant>

#include <qbuffer.h>
#include <qcoreapplication.h>
#include <qlocalserver.h>
#include <qlocalsocket.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>

#include "../core/generation.hpp"
#include "../core/logcat.hpp"
#include "../core/paths.hpp"
#include "ipccommand.hpp"

namespace qs::ipc {

QS_LOGGING_CATEGORY(logIpc, "quickshell.ipc", QtWarningMsg);

IpcServer::IpcServer(const QString& path) {
	QObject::connect(&this->server, &QLocalServer::newConnection, this, &IpcServer::onNewConnection);

	QLocalServer::removeServer(path);

	if (!this->server.listen(path)) {
		qCCritical(logIpc) << "Failed to start IPC server on path" << path;
		return;
	}

	qCInfo(logIpc) << "Started IPC server on path" << path;
}

void IpcServer::start() {
	if (auto* run = QsPaths::instance()->instanceRunDir()) {
		auto path = run->filePath("ipc.sock");
		new IpcServer(path);
	} else {
		qCCritical(
		    logIpc
		) << "Could not start IPC server as the instance runtime path could not be created.";
	}
}

void IpcServer::onNewConnection() {
	while (auto* connection = this->server.nextPendingConnection()) {
		new IpcServerConnection(connection, this);
	}
}

IpcServerConnection::IpcServerConnection(QLocalSocket* socket, IpcServer* server)
    : QObject(server)
    , socket(socket) {
	socket->setParent(this);
	this->stream.setDevice(socket);
	QObject::connect(socket, &QLocalSocket::disconnected, this, &IpcServerConnection::onDisconnected);
	QObject::connect(socket, &QLocalSocket::readyRead, this, &IpcServerConnection::onReadyRead);

	qCInfo(logIpc) << "New IPC connection" << this;
}

void IpcServerConnection::onDisconnected() {
	qCInfo(logIpc) << "IPC connection disconnected" << this;
	QObject::disconnect(this->socket, nullptr, this, nullptr);
	this->deleteLater();
}

void IpcServerConnection::onReadyRead() {
	// A command can arrive in several pieces. A single transaction is rolled
	// back when the command is incomplete, so the next readyRead starts over
	// with everything received so far. Nesting a second transaction here left
	// the outer one open with a failed status, and every later read failed.
	this->stream.startTransaction();
	IpcCommand command;
	this->stream >> command;
	if (!this->stream.commitTransaction()) return;

	std::visit(
	    [this]<typename Command>(Command& command) {
		    if constexpr (std::is_same_v<std::monostate, Command>) {
			    qCCritical(logIpc) << "Received invalid IPC command from" << this;
			    this->socket->disconnectFromServer();
		    } else {
			    command.exec(this);
		    }
	    },
	    command
	);

#ifdef Q_OS_WIN
	// Closing the server end of a named pipe drops a response the client has
	// not read yet, which loses most replies. The connection is instead left
	// open until the client disconnects, see onDisconnected().
#else
	// async connections reparent
	if (dynamic_cast<IpcServer*>(this->parent()) != nullptr) {
		this->deleteLater();
	}
#endif
}

IpcClient::IpcClient(const QString& path) {
	QObject::connect(&this->socket, &QLocalSocket::connected, this, &IpcClient::connected);
	QObject::connect(&this->socket, &QLocalSocket::disconnected, this, &IpcClient::disconnected);
	QObject::connect(&this->socket, &QLocalSocket::errorOccurred, this, &IpcClient::onError);

	this->socket.connectToServer(path);
	this->stream.setDevice(&this->socket);
}

bool IpcClient::isConnected() const { return this->socket.isValid(); }

void IpcClient::waitForConnected() { this->socket.waitForConnected(); }

void IpcClient::waitForDisconnected() {
	this->waitingForDisconnect = true;
	this->socket.waitForDisconnected();
}

void IpcClient::kill() {
	this->sendMessage(IpcCommand(IpcKillCommand()));
	this->waitForDisconnected();
}

void IpcClient::onError(QLocalSocket::LocalSocketError error) const {
	if (this->waitingForDisconnect && error == QLocalSocket::PeerClosedError) return;
	qCCritical(logIpc) << "Socket Error" << error;
}

int IpcClient::connect(const QString& id, const std::function<void(IpcClient& client)>& callback) {
	auto path = QsPaths::ipcPath(id);
	auto client = IpcClient(path);
	qCDebug(logIpc) << "Connecting to instance" << id << "at" << path;

	client.waitForConnected();
	if (!client.isConnected()) return -1;
	qCDebug(logIpc) << "Connected.";

	callback(client);
	return 0;
}

void IpcKillCommand::exec(IpcServerConnection* /*unused*/) {
	qInfo() << "Exiting due to IPC request.";
	auto* generation = EngineGeneration::currentGeneration();
	if (generation) generation->quit();
	else QCoreApplication::exit(0);
}

} // namespace qs::ipc
