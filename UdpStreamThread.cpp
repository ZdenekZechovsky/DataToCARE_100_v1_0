#include "UdpStreamThread.h"
#include <QUdpSocket>
#include <QElapsedTimer>
#include <QtEndian>
#include <QByteArray>

UdpStreamThread::UdpStreamThread(uint8_t magicByte, uint16_t port, bool requireAck, bool isVideo, QObject *parent)
    : QThread(parent)
    , m_magicByte(magicByte)
    , m_port(port)
    , m_requireAck(requireAck)
    , m_isVideo(isVideo)
{
}

void UdpStreamThread::run()
{
    QUdpSocket socket;
    const QHostAddress targetAddr("129.200.99.5");
    socket.bind(QHostAddress::AnyIPv4, 0);
    QElapsedTimer loopTimer;
    uint8_t consecutiveRetries = 0;

    while (!isInterruptionRequested()) {
        if (!isTransmitting.load() || !streamEnabled.load()) {
            uint8_t consecutiveRetries = 0;
            msleep(10);
            continue;
        }

        loopTimer.start();

        const uint16_t currentSize = packetSize.load();
        const double currentPeriodMs = periodMs.load();

        // Sestavení paketu: 1B Magic + 4B MsgID (MSB) + 2B Size (MSB) + Payload
        QByteArray packet;
        packet.resize(currentSize + 7);
        char *raw = packet.data();

        raw[0] = static_cast<char>(m_magicByte);
        qToBigEndian<uint32_t>(m_msgId, reinterpret_cast<uchar *>(raw + 1));
        qToBigEndian<uint16_t>(currentSize, reinterpret_cast<uchar *>(raw + 5));

        for (int i = 7; i < currentSize + 7; ++i) {
            raw[i] = static_cast<char>(((i - 7) % 255) + 1);
        }

        // Vyčištění případných opožděných datagramů před odesláním
        while (socket.hasPendingDatagrams()) {
            socket.readDatagram(nullptr, 0);
        }

        const qint64 sent = socket.writeDatagram(packet, targetAddr, m_port);
        if (sent > 0) {
            bytesSent += static_cast<quint64>(sent);
        }

        if (m_requireAck) {
            bool ackReceived = false;
            const int ackTimeoutMs = qMax(1, static_cast<int>(currentPeriodMs));

            if (socket.waitForReadyRead(ackTimeoutMs)) {
                while (socket.hasPendingDatagrams()) {
                    QByteArray ack;
                    ack.resize(static_cast<int>(socket.pendingDatagramSize()));
                    socket.readDatagram(ack.data(), ack.size());

                    if (ack.size() == 5) {
                        ackReceived = true;
                        break;
                    }
                }
            }

            if (ackReceived) {
                ++m_msgId;
                consecutiveRetries = 0;
            } else if (consecutiveRetries < 5) {
                // Opakujeme stejný paket (max 5 opakování)
                ++consecutiveRetries;
                const quint32 retries = ++retryCount;
                emit retryCountChanged(retries);
            } else {
                // Ani 5. opakování neuspělo -> započítáme chybu a jdeme na další paket
                consecutiveRetries = 0;
                ++m_msgId;
                const quint32 errors = ++errorCount;
                emit errorCountChanged(errors);
            }
        } else {
            ++m_msgId;
        }

        // Výpočet zbývajícího zpoždění do konce periody
        const qint64 targetNs = static_cast<qint64>(currentPeriodMs * 1'000'000.0);
        const qint64 remainingNs = targetNs - loopTimer.nsecsElapsed();

        if (remainingNs > 0) {
            if (m_isVideo && targetNs < 15'000'000) {
                // Hybridní čekání pro sub-milisekundovou přesnost na Windows i Linuxu
                if (remainingNs > 2'000'000) {
                    usleep(static_cast<unsigned long>((remainingNs - 1'500'000) / 1000));
                }
                while (loopTimer.nsecsElapsed() < targetNs) {
                    // Aktivní dotočení posledních mikrosekund
                }
            } else {
                usleep(static_cast<unsigned long>(remainingNs / 1000));
            }
        }
    }
}