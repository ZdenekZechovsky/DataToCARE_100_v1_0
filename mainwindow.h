#pragma once

#include <QMainWindow>
#include <QTimer>
#include <QPointer>
#include <QCloseEvent>
#include "UdpStreamThread.h"
#include "DiagnosticDialog.h"
#include "TimeReceiverThread.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void syncUiToThreads();
    void startTransmission();
    void stopTransmission();
    void updateStatusBar();
    void openDiagnosticDialog();
    void onFlightTimeReceived(const FlightTime_t &timeData);

private:
    Ui::MainWindow *ui;
    QTimer *m_statusTimer;
    int m_uptimeSeconds{0};
    bool m_isTransmitting{false};

    UdpStreamThread *m_threadFd;
    UdpStreamThread *m_threadAudio;
    UdpStreamThread *m_threadVideo;

    QPointer<DiagnosticDialog> m_diagDialog;
    TimeReceiverThread *m_threadTime;
};