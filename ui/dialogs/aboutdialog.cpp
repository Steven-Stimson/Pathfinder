//Copyright 2017 Ryan Wick

//This file is part of Pathfinder.

//Pathfinder is free software: you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.

//Pathfinder is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.

//You should have received a copy of the GNU General Public License
//along with Pathfinder.  If not, see <http://www.gnu.org/licenses/>.


#include "aboutdialog.h"
#include "ui_aboutdialog.h"
#include <QApplication>

AboutDialog::AboutDialog(QWidget *parent) :
    QDialog(parent, Qt::WindowTitleHint | Qt::WindowCloseButtonHint),
    ui(new Ui::AboutDialog)
{
    ui->setupUi(this);

    QPixmap icon = ui->iconLabel->pixmap(Qt::ReturnByValue);
    icon.setDevicePixelRatio(devicePixelRatio());
    ui->iconLabel->setPixmap(icon);

    //Make the word 'Pathfinder' a larger font size.
    QFont font;
    font.setPointSize(font.pointSize() * 2);
    ui->titleLabel->setFont(font);

    QString versionCopyrightText =
            "<html><head/><body><p>Version: " + QApplication::applicationVersion() + "</p>"+
            "<p>Copyright 2021-now Anton Korobeynikov</p>"
            "<p>Copyright 2022 Andrey Zakharov</p>"
            "<p>Copyright 2017-2021 Ryan Wick</p>"
            "</body></html>";
    ui->versionCopyrightLabel->setText(versionCopyrightText);

    layout()->setSizeConstraint(QLayout::SetFixedSize);
}


AboutDialog::~AboutDialog()
{
    delete ui;
}
