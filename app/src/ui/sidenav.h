#pragma once

#include <QListWidget>

// Numbered vertical navigation, the keyboard screen's menu style
class SideNav : public QListWidget {
    Q_OBJECT

public:
    explicit SideNav(QWidget *parent = nullptr);
    void setEntries(const QStringList &titles, int current);
};
