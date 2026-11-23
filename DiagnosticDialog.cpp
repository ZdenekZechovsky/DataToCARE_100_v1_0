#include "DiagnosticDialog.h"
#include <QVBoxLayout>
#include <QHeaderView>
#include <QCloseEvent>
#include <QFontDatabase>
#include <QtEndian>
#include <QVector>
#include <QStringList>

namespace {
struct ParsedRow {
    QString index;
    QString hex;
    QString bin;
    QString value;
};
}

DiagnosticDialog::DiagnosticDialog(const QHostAddress &targetAddr, QWidget *parent)
    : QDialog(parent, Qt::Window)
    , m_table(new QTableWidget(this))
    , m_statusLabel(new QLabel("Spouštím diagnostický dotaz...", this))
    , m_thread(new DiagnosticThread(targetAddr, 1159, 0x22, this))
{
    setWindowTitle("Diagnostic Data (Port 1159)");
    resize(720, 620);

    // PŘÍKLAD KONFIGURACE POZIC (uprav dle struktury svého paketu):
    m_fields = {
        {37, {FieldType::UInt32}},      // bajty 10..13 jako 32b číslo
        {41, {FieldType::UInt32}},      // bajty 10..13 jako 32b číslo
        {45, {FieldType::UInt32}},      // bajty 10..13 jako 32b číslo
        {49, {FieldType::UInt32}},      // bajty 10..13 jako 32b číslo
        {53, {FieldType::UInt32}},      // bajty 10..13 jako 32b číslo
        {57, {FieldType::UInt32}},      // bajty 10..13 jako 32b číslo
        {61, {FieldType::UInt32}},      // bajty 10..13 jako 32b číslo
        {19, {FieldType::String, 16}}   // bajty 18..33 jako string o délce 16 B
    };

    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({"Pořadí", "Hex", "Binární", "Dec / Text"});
    m_table->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_table);
    layout->addWidget(m_statusLabel);

    connect(m_thread, &DiagnosticThread::diagnosticDataReceived, this, &DiagnosticDialog::updateTable);
    connect(m_thread, &DiagnosticThread::statusUpdated, m_statusLabel, &QLabel::setText);

    m_thread->start();
}

DiagnosticDialog::~DiagnosticDialog()
{
    if (m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->wait();
    }
}

void DiagnosticDialog::closeEvent(QCloseEvent *event)
{
    m_thread->requestInterruption();
    m_thread->wait();
    event->accept();
}

void DiagnosticDialog::updateTable(const QByteArray &data)
{
    QVector<ParsedRow> rows;
    rows.reserve(data.size());

    int offset = 0;
    while (offset < data.size()) {
        FieldType type = FieldType::UInt8;
        int width = 1;

        if (m_fields.contains(offset)) {
            const FieldDef def = m_fields.value(offset);
            type = def.type;
            if (type == FieldType::UInt16)      width = 2;
            else if (type == FieldType::UInt32) width = 4;
            else if (type == FieldType::String) width = def.length;
        }

        // Ochrana proti přetečení na konci paketu nebo neplatné délce
        if (width <= 0 || (offset + width > data.size())) {
            type = FieldType::UInt8;
            width = 1;
        }

        if (type == FieldType::String) {
            QByteArray slice = data.mid(offset, width);

            // 1. Pořadí
            const QString idxStr = QString("%1..%2 (STR %3B)")
                                       .arg(offset).arg(offset + width - 1).arg(width);

            // 2. Hexadecimální výpis jednotlivých bajtů řetězce
            const QString hexStr = QString::fromLatin1(slice.toHex(' ').toUpper());

            // 3. Binární sloupec (vynechán, aby dlouhý text neroztahoval tabulku)
            const QString binStr = "—";

            // 4. Dekódovaný text (oříznutí na případném ukončovacím '\0')
            const int nullPos = slice.indexOf('\0');
            if (nullPos >= 0) {
                slice.truncate(nullPos);
            }

            QString decoded = QString::fromUtf8(slice);
            // Nahrazení netisknutelných řídicích znaků tečkou, aby se nerozbilo UI
            for (QChar &ch : decoded) {
                if (!ch.isPrint()) {
                    ch = QLatin1Char('.');
                }
            }
            const QString valStr = QString("\"%1\"").arg(decoded);

            rows.append({idxStr, hexStr, binStr, valStr});
        } else {
            const auto *rawPtr = reinterpret_cast<const uchar *>(data.constData() + offset);
            uint32_t value = 0;

            if (type == FieldType::UInt8) {
                value = rawPtr[0];
            } else if (type == FieldType::UInt16) {
                value = qFromBigEndian<quint16>(rawPtr);
            } else if (type == FieldType::UInt32) {
                value = qFromBigEndian<quint32>(rawPtr);
            }

            const QString idxStr = (width == 1)
                                       ? QString::number(offset)
                                       : QString("%1..%2 (%3b)").arg(offset).arg(offset + width - 1).arg(width * 8);

            const QString hexStr = QString("0x%1")
                                       .arg(value, width * 2, 16, QLatin1Char('0')).toUpper();

            QStringList binBytes;
            for (int b = 0; b < width; ++b) {
                const uint8_t byteVal = static_cast<uint8_t>(data.at(offset + b));
                binBytes << QString("%1").arg(byteVal, 8, 2, QLatin1Char('0'));
            }
            const QString binStr = binBytes.join(' ');

            const QString valStr = QString::number(value);

            rows.append({idxStr, hexStr, binStr, valStr});
        }

        offset += width;
    }

    if (m_table->rowCount() != rows.size()) {
        m_table->setRowCount(rows.size());
        for (int r = 0; r < rows.size(); ++r) {
            for (int c = 0; c < 4; ++c) {
                auto *item = new QTableWidgetItem();
                item->setTextAlignment(Qt::AlignCenter);
                m_table->setItem(r, c, item);
            }
        }
    }

    for (int r = 0; r < rows.size(); ++r) {
        m_table->item(r, 0)->setText(rows[r].index);
        m_table->item(r, 1)->setText(rows[r].hex);
        m_table->item(r, 2)->setText(rows[r].bin);
        m_table->item(r, 3)->setText(rows[r].value);
    }
}