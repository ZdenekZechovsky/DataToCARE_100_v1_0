#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QLabel>
#include <QMap>
#include "DiagnosticThread.h"

class DiagnosticDialog : public QDialog
{
    Q_OBJECT

public:
    enum class FieldType {
        UInt8,
        UInt16,
        UInt32,
        String
    };

    struct FieldDef {
        FieldType type = FieldType::UInt8;
        int length = 1; // Použije se u typu String (u UInt16/UInt32 se bere pevně 2/4 B)
    };

    explicit DiagnosticDialog(const QHostAddress &targetAddr, QWidget *parent = nullptr);
    ~DiagnosticDialog() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void updateTable(const QByteArray &data);

private:
    QTableWidget *m_table;
    QLabel *m_statusLabel;
    DiagnosticThread *m_thread;

    // Definice složených polí: { počáteční_index, { typ, délka_v_bajtech } }
    QMap<int, FieldDef> m_fields;
};