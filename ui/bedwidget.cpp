#include "bedwidget.h"
#include "dialogs/beddialog.h"

#include <QPushButton>
#include <QVBoxLayout>

BedWidget::BedWidget(QWidget *parent) : QWidget(parent), m_bedDialog(nullptr) {
    static const QString label = "Load/view BED file";
    auto *button = new QPushButton(label, this);

    auto *layout = new QVBoxLayout();
    layout->addWidget(button);

    connect(button, &QPushButton::clicked, [this]() {
        if (!m_bedDialog) {
            m_bedDialog = new BedDialog(this);
        }
        m_bedDialog->show();
        m_bedDialog->raise();
        m_bedDialog->activateWindow();
    });

    setLayout(layout);
}
