#pragma once

#include <QThread>
#include <QHostAddress>
#include <QByteArray>
#include <cstdint>

class DiagnosticThread : public QThread
{
    Q_OBJECT

public:
    explicit DiagnosticThread(const QHostAddress &targetAddr,
                              uint16_t port = 1159,
                              uint8_t requestType = 0x22,
                              QObject *parent = nullptr);

signals:
    void diagnosticDataReceived(const QByteArray &data);
    void statusUpdated(const QString &status);

protected:
    void run() override;

private:
    QHostAddress m_targetAddr;
    uint16_t m_port;
    uint8_t m_requestType;
};