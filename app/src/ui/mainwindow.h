#pragma once

#include <QMainWindow>
#include <QSettings>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    QSettings settings_;
};
