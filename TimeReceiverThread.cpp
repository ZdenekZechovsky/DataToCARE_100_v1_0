#include "TimeReceiverThread.h"
#include <QUdpSocket>
#include <QHostAddress>
#include <cstring>

TimeReceiverThread::TimeReceiverThread(uint16_t port, QObject *parent)
    : QThread(parent)
    , m_port(port)
{
}

void TimeReceiverThread::run()
{
    QUdpSocket socket;
    if (!socket.bind(QHostAddress::AnyIPv4, m_port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        qWarning("Nepodarilo se nabindovat UDP port %u pro prijem casu.", m_port);
        return;
    }

    while (!isInterruptionRequested()) {
        // Krátký timeout 50 ms zajistí okamžitou reakci při ukončování vlákna
        if (socket.waitForReadyRead(50)) {
            while (socket.hasPendingDatagrams()) {
                const qint64 size = socket.pendingDatagramSize();
                if (size == sizeof(FlightTime_t)) {
                    FlightTime_t data{};
                    socket.readDatagram(reinterpret_cast<char *>(&data), sizeof(FlightTime_t));
                    emit flightTimeReceived(data);
                } else {
                    // Zahodíme datagram s jinou než 9B délkou
                    socket.readDatagram(nullptr, 0);
                }
            }
        }
    }
}