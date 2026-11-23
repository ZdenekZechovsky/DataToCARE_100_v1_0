#include "UdpStreamThread.h"
#include <QUdpSocket>
#include <QElapsedTimer>
#include <QDateTime>
#include <QtEndian>
#include <QByteArray>

namespace {
uint8_t toBcd(int value)
{
    const int clamped = qBound(0, value, 99);
    return static_cast<uint8_t>(((clamped / 10) << 4) | (clamped % 10));
}
}

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
    QElapsedTimer testTimer;
    bool wasTransmitting = false;
    uint8_t consecutiveRetries = 0;

    while (!isInterruptionRequested()) {
        const bool transmitting = isTransmitting.load();
        if (!transmitting) {
            wasTransmitting = false;
            consecutiveRetries = 0;
            msleep(10);
            continue;
        }

        // Spuštění stopek testu při přechodu do stavu Transmitting
        if (!wasTransmitting) {
            testTimer.start();
            wasTransmitting = true;
        }

        if (!streamEnabled.load()) {
            consecutiveRetries = 0;
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

        int payloadStart = 7;

        // Specifická 9B časová hlavička pro Audio stream (0xAD) od 8. pozice (index 7)
        if (m_magicByte == 0xA1 && currentSize >= 9) {
            // 1) Čas od spuštění testu (3 B binárně: hodiny, minuty, sekundy)
            const qint64 elapsedSecs = testTimer.elapsed() / 1000;
            raw[7] = static_cast<char>((elapsedSecs / 3600) % 256);
            raw[8] = static_cast<char>((elapsedSecs % 3600) / 60);
            raw[9] = static_cast<char>(elapsedSecs % 60);

            // Jednotné načtení systémového času a data
            const QDateTime now = QDateTime::currentDateTime();
            const QTime time = now.time();
            const QDate date = now.date();

            // 2) Reálný čas (3 B binárně: hodiny, minuty, sekundy)
            raw[10] = static_cast<char>(time.hour());
            raw[11] = static_cast<char>(time.minute());
            raw[12] = static_cast<char>(time.second());

            // 3) Datum (3 B v BCD: rok - 2000, den, měsíc)
            raw[13] = static_cast<char>(toBcd(date.year() - 2000));
            raw[14] = static_cast<char>(toBcd(date.day()));
            raw[15] = static_cast<char>(toBcd(date.month()));

            payloadStart = 16;
        }

        // Užitečná data za hlavičkou
        for (int i = payloadStart; i < currentSize + 7; ++i) {
            raw[i] = static_cast<char>(((i - payloadStart) % 255) + 1);
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