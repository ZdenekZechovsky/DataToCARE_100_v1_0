#include "mainwindow.h"
#include "ui_mainwindow.h"

#define UDP_FLIGHT_DATA_PORT        1170
#define FD_HEADER_BYTE              0xFD

#define UDP_AUDIO_DATA_PORT         1171
#define AD_HEADER_BYTE              0xA1

namespace {
QString formatDataSize(quint64 bytes)
{
    constexpr double KB = 1024.0;
    constexpr double MB = 1024.0 * 1024.0;
    constexpr double GB = 1024 * 1024.0 * 1024.0;

    if (bytes >= static_cast<quint64>(MB)) {
        return QString("%1 MB").arg(bytes / MB, 0, 'f', 2);
    }
    else if (bytes >= static_cast<quint64>(GB)) {
        return QString("%1 GB").arg(bytes / GB, 0, 'f', 2);
    }
    return QString("%1 kB").arg(bytes / KB, 0, 'f', 2);
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_statusTimer(new QTimer(this))
{
    ui->setupUi(this);

    // Inicializace vláken (testovací cílové porty 5001, 5002, 5003)
    m_threadFd    = new UdpStreamThread(FD_HEADER_BYTE, UDP_FLIGHT_DATA_PORT, true,  false, this);
    m_threadAudio = new UdpStreamThread(AD_HEADER_BYTE, UDP_AUDIO_DATA_PORT, true,  false, this);
    m_threadVideo = new UdpStreamThread(0xDD, 5003, false, true,  this);

    // Propojení tlačítek
    connect(ui->btn_start, &QPushButton::clicked, this, &MainWindow::startTransmission);
    connect(ui->btn_stop,  &QPushButton::clicked, this, &MainWindow::stopTransmission);

    // Propojení všech sliderů a checkboxů na okamžitou aktualizaci parametrů a bitrate
    connect(ui->cb_fd_enable,      &QCheckBox::toggled,    this, &MainWindow::syncUiToThreads);
    connect(ui->slider_fd_size,    &QSlider::valueChanged, this, &MainWindow::syncUiToThreads);
    connect(ui->slider_fd_speed,   &QSlider::valueChanged, this, &MainWindow::syncUiToThreads);
    // V konstruktoru MainWindow::MainWindow(...) doplň:
    connect(m_threadFd, &UdpStreamThread::retryCountChanged, this, [this](quint32 count) {
        ui->lbl_fd_retries->setText(QString::number(count));
    });
    connect(m_threadFd, &UdpStreamThread::errorCountChanged, this, [this](quint32 count) {
        ui->lbl_fd_errors->setText(QString::number(count));
    });

    connect(ui->cb_audio_enable,   &QCheckBox::toggled,    this, &MainWindow::syncUiToThreads);
    connect(ui->slider_audio_size, &QSlider::valueChanged, this, &MainWindow::syncUiToThreads);
    connect(ui->slider_audio_speed,&QSlider::valueChanged, this, &MainWindow::syncUiToThreads);
    connect(m_threadAudio, &UdpStreamThread::retryCountChanged, this, [this](quint32 count) {
        ui->lbl_audio_retries->setText(QString::number(count));
    });
    connect(m_threadAudio, &UdpStreamThread::errorCountChanged, this, [this](quint32 count) {
        ui->lbl_audio_errors->setText(QString::number(count));
    });

    connect(ui->cb_video_enable,   &QCheckBox::toggled,    this, &MainWindow::syncUiToThreads);
    connect(ui->slider_video_size, &QSlider::valueChanged, this, &MainWindow::syncUiToThreads);
    connect(ui->slider_video_speed,&QSlider::valueChanged, this, &MainWindow::syncUiToThreads);

    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::updateStatusBar);

    connect(ui->btn_diag, &QPushButton::clicked, this, &MainWindow::openDiagnosticDialog);

    syncUiToThreads();
    updateStatusBar();

    m_threadFd->start();
    m_threadAudio->start();
    m_threadVideo->start();
}

MainWindow::~MainWindow()
{
    if (m_diagDialog) {
        m_diagDialog->close(); // Zavolá closeEvent dialogu, ukončí jeho vlákno a uvolní paměť
    }

    m_threadFd->requestInterruption();
    m_threadAudio->requestInterruption();
    m_threadVideo->requestInterruption();

    m_threadFd->wait();
    m_threadAudio->wait();
    m_threadVideo->wait();

    delete ui;
}

void MainWindow::openDiagnosticDialog()
{
    if (!m_diagDialog) {
        // Předáním nullptr vznikne nezávislé okno, které lze překrýt hlavním oknem
        m_diagDialog = new DiagnosticDialog(QHostAddress("129.200.99.5"), nullptr);
        m_diagDialog->setAttribute(Qt::WA_DeleteOnClose);
        m_diagDialog->show();
    } else {
        m_diagDialog->raise();
        m_diagDialog->activateWindow();
    }
}
void MainWindow::syncUiToThreads()
{
    // --- Flight Data ---
    const uint16_t fdSize = static_cast<uint16_t>(ui->slider_fd_size->value());
    const double fdPeriod = static_cast<double>(ui->slider_fd_speed->value());
    m_threadFd->streamEnabled.store(ui->cb_fd_enable->isChecked());
    m_threadFd->packetSize.store(fdSize);
    m_threadFd->periodMs.store(fdPeriod);

    const double fdKbps = ((fdSize * 8.0) / (fdPeriod / 1000.0)) / 1000.0;
    ui->lbl_fd_bitrate->setText(QString("%1 kbps").arg(fdKbps, 0, 'f', 2));

    // --- Audio ---
    const uint16_t audSize = static_cast<uint16_t>(ui->slider_audio_size->value());
    const double audPeriod = static_cast<double>(ui->slider_audio_speed->value());
    m_threadAudio->streamEnabled.store(ui->cb_audio_enable->isChecked());
    m_threadAudio->packetSize.store(audSize);
    m_threadAudio->periodMs.store(audPeriod);

    const double audKbps = ((audSize * 8.0) / (audPeriod / 1000.0)) / 1000.0;
    ui->lbl_audio_bitrate->setText(QString("%1 kbps").arg(audKbps, 0, 'f', 2));

    // --- Video (slider 5..10000 -> 0.5..1000.0 ms) ---
    const uint16_t vidSize = static_cast<uint16_t>(ui->slider_video_size->value());
    const double vidPeriod = ui->slider_video_speed->value() / 10.0;
    m_threadVideo->streamEnabled.store(ui->cb_video_enable->isChecked());
    m_threadVideo->packetSize.store(vidSize);
    m_threadVideo->periodMs.store(vidPeriod);

    const double vidMbps = ((vidSize * 8.0) / (vidPeriod / 1000.0)) / 1'000'000.0;
    ui->lbl_video_bitrate->setText(QString("%1 Mbps").arg(vidMbps, 0, 'f', 2));
}

void MainWindow::startTransmission()
{
    m_threadFd->retryCount.store(0);
    m_threadAudio->retryCount.store(0);
    m_threadFd->errorCount.store(0);
    m_threadAudio->errorCount.store(0);
    m_threadFd->bytesSent.store(0);
    m_threadAudio->bytesSent.store(0);
    m_threadVideo->bytesSent.store(0);

    ui->lbl_fd_retries->setText("0");
    ui->lbl_audio_retries->setText("0");
    ui->lbl_fd_errors->setText("0");
    ui->lbl_audio_errors->setText("0");
    ui->lbl_fd_sent->setText("0.00 kB");
    ui->lbl_audio_sent->setText("0.00 kB");
    ui->lbl_video_sent->setText("0.00 kB");

    m_isTransmitting = true;
    m_threadFd->isTransmitting.store(true);
    m_threadAudio->isTransmitting.store(true);
    m_threadVideo->isTransmitting.store(true);

    ui->btn_start->setEnabled(false);
    ui->btn_stop->setEnabled(true);

    m_uptimeSeconds = 0;
    m_statusTimer->start(1000);
    updateStatusBar();
}

void MainWindow::stopTransmission()
{
    m_isTransmitting = false;
    m_threadFd->isTransmitting.store(false);
    m_threadAudio->isTransmitting.store(false);
    m_threadVideo->isTransmitting.store(false);

    ui->btn_start->setEnabled(true);
    ui->btn_stop->setEnabled(false);

    m_statusTimer->stop();
    updateStatusBar();
}

void MainWindow::updateStatusBar()
{
    const QString mode = m_isTransmitting ? "TRANSMITTING" : "STOPPED";
    ui->statusbar->showMessage(QString("Mode: %1 | Uptime: %2 s").arg(mode).arg(m_uptimeSeconds));

    ui->lbl_fd_sent->setText(formatDataSize(m_threadFd->bytesSent.load()));
    ui->lbl_audio_sent->setText(formatDataSize(m_threadAudio->bytesSent.load()));
    ui->lbl_video_sent->setText(formatDataSize(m_threadVideo->bytesSent.load()));

    if (m_isTransmitting) {
        ++m_uptimeSeconds;
    }
}