#include "DiagnosticThread.h"
#include <QUdpSocket>
#include <QElapsedTimer>

DiagnosticThread::DiagnosticThread(const QHostAddress &targetAddr,
                                   uint16_t port,
                                   uint8_t requestType,
                                   QObject *parent)
    : QThread(parent)
    , m_targetAddr(targetAddr)
    , m_port(port)
    , m_requestType(requestType)
{
}

void DiagnosticThread::run()
{
    QUdpSocket socket;
    if (!socket.bind(QHostAddress::AnyIPv4, 0)) {
        emit statusUpdated("Chyba: Nepodařilo se nabindovat lokální UDP socket.");
        return;
    }

    // Očekávaná délka odpovědi podle 2. bytu požadavku (0x11 -> 37 B, 0x22 -> 210 B)
    const int expectedSize = (m_requestType == 0x11) ? 37 :
                                 (m_requestType == 0x22) ? 210 : 0;

    QByteArray request(4, 0x00);
    request[0] = static_cast<char>(0xDD);
    request[1] = static_cast<char>(m_requestType);
    request[2] = 0x00;
    request[3] = 0x00;

    QElapsedTimer loopTimer;

    while (!isInterruptionRequested()) {
        loopTimer.start();

        // Vyčištění případných starých datagramů
        while (socket.hasPendingDatagrams()) {
            socket.readDatagram(nullptr, 0);
        }

        socket.writeDatagram(request, m_targetAddr, m_port);

        bool validPacketReceived = false;
        // Čekáme na odpověď max 450 ms, aby se dodržela 500ms perioda
        if (socket.waitForReadyRead(450)) {
            while (socket.hasPendingDatagrams()) {
                QByteArray response;
                response.resize(static_cast<int>(socket.pendingDatagramSize()));
                socket.readDatagram(response.data(), response.size());

                if (response.size() == expectedSize) {
                    emit diagnosticDataReceived(response);
                    emit statusUpdated(QString("Přijato %1 B (typ 0x%2)")
                                           .arg(response.size())
                                           .arg(m_requestType, 2, 16, QLatin1Char('0')).toUpper());
                    validPacketReceived = true;
                    break;
                } else {
                    emit statusUpdated(QString("Neplatná délka paketu: %1 B (očekáváno %2 B)")
                                           .arg(response.size())
                                           .arg(expectedSize));
                }
            }
        }

        if (!validPacketReceived && !socket.hasPendingDatagrams()) {
            emit statusUpdated("Čekám na odpověď serveru (timeout)...");
        }

        // Doplnění zpoždění do celkové periody 500 ms
        const qint64 remainingMs = 500 - loopTimer.elapsed();
        if (remainingMs > 0) {
            msleep(static_cast<unsigned long>(remainingMs));
        }
    }
}