#pragma once

#include <QThread>
#include <QHostAddress>
#include <atomic>
#include <cstdint>

class UdpStreamThread : public QThread
{
    Q_OBJECT

public:
    explicit UdpStreamThread(uint8_t magicByte, uint16_t port, bool requireAck, bool isVideo = false, QObject *parent = nullptr);

    std::atomic<bool> isTransmitting{false};
    std::atomic<bool> streamEnabled{false};
    std::atomic<uint16_t> packetSize{64};
    std::atomic<double> freqHz{15.0};
    std::atomic<quint32> retryCount{0};
    std::atomic<quint32> errorCount{0};
    std::atomic<quint64> bytesSent{0};

signals:
    void retryCountChanged(quint32 count);
    void errorCountChanged(quint32 count);

protected:
    void run() override;

private:
    uint8_t m_magicByte;
    uint16_t m_port;
    bool m_requireAck;
    bool m_isVideo;
    uint32_t m_msgId{0};
};