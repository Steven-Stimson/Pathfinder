#include "bedwidget.h"
#include "dialogs/beddialog.h"

#include <QPushButton>
#include <QVBoxLayout>

BedWidget::BedWidget(QWidget *parent) : QWidget(parent) {
    static const QString label = "Load BED file";
    auto *button = new QPushButton(label, this);

    auto *layout = new QVBoxLayout();
    layout->addWidget(button);

    connect(button, &QPushButton::clicked, [this]() {
        auto *dialog = new BedDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });

    setLayout(layout);
}
