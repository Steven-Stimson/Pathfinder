#ifndef BANDAGENG_BEDWIDGET_H
#define BANDAGENG_BEDWIDGET_H


#include <QWidget>

class BedDialog;

class BedWidget : public QWidget {
public:
    explicit BedWidget(QWidget *parent);

private:
    BedDialog *m_bedDialog;
};


#endif //BANDAGENG_BEDWIDGET_H
