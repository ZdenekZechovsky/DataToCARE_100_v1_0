#pragma once

#include <QThread>
#include <cstdint>

#pragma pack(push, 1)
typedef struct {
    uint8_t system_hour;
    uint8_t system_min;
    uint8_t system_sec;
    uint8_t gps_hour;
    uint8_t gps_min;
    uint8_t gps_sec;
    uint8_t bcd_year;
    uint8_t bcd_month;
    uint8_t bcd_day;
} FlightTime_t;
#pragma pack(pop)

static_assert(sizeof(FlightTime_t) == 9, "FlightTime_t must be exactly 9 bytes");

class TimeReceiverThread : public QThread
{
    Q_OBJECT

public:
    explicit TimeReceiverThread(uint16_t port = 1040, QObject *parent = nullptr);

signals:
    void flightTimeReceived(const FlightTime_t &timeData);

protected:
    void run() override;

private:
    uint16_t m_port;
};