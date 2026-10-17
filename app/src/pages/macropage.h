#pragma once

#include "model/macro.h"
#include "ui/page.h"

class MacroRecorder;
class QLabel;
class QPushButton;
class QTabBar;
class QVBoxLayout;

// Macros: each one a list of actions (text, taps, presses, releases, delays), saved to the keyboard together
class MacroPage : public Page {
    Q_OBJECT

public:
    explicit MacroPage(Controller *ctl);
    ~MacroPage() override;

    void keyboardChanged() override;
    void deactivate() override;

private:
    void showMacro(int index);
    void rebuildRows();
    void changed();
    void save();
    void revert();
    void openTextEditor();
    void startRecording(bool append);
    void stopRecording();

    QWidget *rowWidget(int index);
    QByteArray serialized() const;

    QTabBar *tabs_;
    QWidget *rowsHost_;
    QVBoxLayout *rows_;
    QLabel *memory_;
    QPushButton *save_, *revert_, *record_, *stop_;
    QList<QWidget *> editButtons_;
    QList<macro::Macro> macros_;
    int current_ = 0;
    MacroRecorder *recorder_ = nullptr;
    bool append_ = false;
};
